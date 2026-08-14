/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "boardpane.h"

#include "boardmodel.h"
#include "composer.h"
#include "richtext.h"
#include "theme.h"
#include "widgets.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

namespace {

/**
 * How wide the thread list opens, in logical pixels.
 *
 * Enough for a subject and the name beside it. What a board is read through is this
 * list, so it starts at a width somebody can read one in rather than at whatever the
 * columns happen to insist on.
 */
constexpr int LIST_WIDTH = 380;

/**
 * How long a refusal stays on the screen, in milliseconds.
 *
 * Long enough to be read by somebody who was not looking when it arrived, and gone by
 * itself after that: a hub declining one thing is not a state of the board, and a band
 * that stayed until it was dismissed would still be there an hour later saying
 * something about a post nobody is looking at any more.
 */
constexpr int REFUSAL_MS = 20000;

/** The same mapping the chat feed renders a message with; this module reads no theme. */
Qdc::RichText::RenderColours markup_colours()
{
	const Qdc::Theme& current = Qdc::theme();

	Qdc::RichText::RenderColours colours;
	colours.code = current.contentText;
	colours.codeBack = current.contentAlt;
	colours.quote = current.contentMuted;
	colours.quoteBar = current.border;
	colours.link = current.accent;
	colours.chip = current.contentText;
	colours.chipBack = current.contentAlt;

	return colours;
}

/**
 * A label that disappears when it has nothing to say.
 *
 * An empty label still takes a line's worth of space, and a reading pane with four of
 * them reads as a post with four blank fields rather than as one that is simply not
 * known yet.
 */
void say(QLabel* label, const QString& text)
{
	label->setText(text);
	label->setVisible(!text.isEmpty());
}

/**
 * Who the post is from and when the hub took it, on one line.
 *
 * The hub's claim about the author, always, and never the document's: the hub says it
 * accepted the post from a session that had authenticated as this CID, which is a
 * statement about one event on one hub rather than a proven identity. That is a fact
 * about what this line means and not something to tell the reader every time - a name
 * with a paragraph of epistemology after it is not a byline.
 *
 * The nickname is what a reader recognises, so it is what is drawn; the CID stands in
 * where the hub gave no nickname. A post whose document claims a different author is
 * drawn from here exactly like any other, and the document's claim is not drawn beside
 * it: the honest case of a disagreement is a relayed post, nothing on this machine can
 * tell that from a forgery, and a byline that argued with itself would say only that.
 *
 * The time is the hub's, which is the one that orders the board. The author's own DA is
 * not drawn anywhere: nothing verifies it, nothing may be ordered by it, and a date
 * shown beside a warning that it might be untrue is worse than no date.
 */
QString attribution(const Qdc::BoardEntry& entry)
{
	using Pane = Qdc::BoardPane;

	const QString nick = Qdc::RichText::displayText(entry.nick);
	const QString cid = Qdc::RichText::displayText(entry.cid);
	const QString who = nick.isEmpty() ? cid : nick;

	const QString when = Qdc::BoardModel::describeTimestamp(entry.timestamp);

	if (who.isEmpty()) return when;

	return Pane::tr("%1 — %2", "author, then when the hub accepted the post")
		.arg(who, when);
}

}

Qdc::BoardPane::BoardPane(QWidget* parent)
	: QWidget(parent)
	, m_threads(new BoardModel(this))
{
	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	layout->addWidget(buildBoardHeader());

	/*
	 * The post being read takes the whole of the area beside the list, and what is
	 * being written takes the same area rather than a strip under it: reading and
	 * writing are two things somebody is doing, not two panels they need at once.
	 */
	m_area = new QStackedWidget(this);
	m_area->addWidget(buildReader());
	m_area->addWidget(buildComposer());

	m_split = new QSplitter(Qt::Horizontal, this);
	m_split->addWidget(buildThreadList());
	m_split->addWidget(m_area);
	m_split->setStretchFactor(0, 1);
	m_split->setStretchFactor(1, 3);

	/*
	 * Wide enough to read a subject in, which is what the list is for. A stretch factor
	 * decides how the two share what is left over as the window is resized and says
	 * nothing about where they start, so on its own it opened the list at the width of
	 * its own minimum - a column too narrow to hold a subject, with a scrollbar under it.
	 */
	m_split->setSizes({LIST_WIDTH, LIST_WIDTH * 3});

	layout->addWidget(m_split, 1);

	/*
	 * A rebuild moves rows anywhere in the tree - an orphan is adopted the moment its
	 * parent arrives - so the selection is put back by the post it was on rather than by
	 * the row it was in, and the threads are opened again: a board whose replies are
	 * folded away is a board nobody reads.
	 */
	connect(m_threads, &QAbstractItemModel::modelReset,
	        this, &BoardPane::restoreSelection);

	/*
	 * The reading area follows how much is known about the post it is showing, however
	 * that arrives. A document that turns out to be on this machine is read the moment
	 * somebody opens the post, and what says so is a state set while this pane is
	 * already drawing the entry it had - so without this a reader would be left looking
	 * at "not fetched", with a button offering to fetch it, over a document that is on
	 * the disk in front of them.
	 */
	connect(m_threads, &QAbstractItemModel::dataChanged, this, [this] {
		if (m_reading.isEmpty() || stateOfReading() == m_drawn) return;

		refreshReading();
	});

	refreshBoardHeader();
	refreshReading();
}

QWidget* Qdc::BoardPane::buildBoardHeader()
{
	QWidget* header = new QWidget(this);

	QVBoxLayout* layout = new QVBoxLayout(header);
	layout->setContentsMargins(12, 10, 12, 8);
	layout->setSpacing(4);

	m_title = new QLabel(header);
	m_title->setObjectName(QStringLiteral("boardTitle"));

	/*
	 * The title, the description and every other remote string in this pane are set on a
	 * label as plain text and never left on Qt::AutoText. A label on AutoText asks
	 * Qt::mightBeRichText() what it has been given, so a hub calling a board
	 * "<b>Notices</b>" would be drawn bold and an <img> in a description would make the
	 * label go and fetch a resource. displayText() reduces the text; this decides
	 * outright that it is text.
	 */
	m_title->setTextFormat(Qt::PlainText);
	m_title->setWordWrap(true);

	QFont bigger = m_title->font();
	bigger.setBold(true);
	m_title->setFont(bigger);

	layout->addWidget(m_title);

	m_description = makeNote(QString(), header);
	m_description->setTextFormat(Qt::PlainText);
	layout->addWidget(m_description);

	m_permissions = makeNote(QString(), header);
	m_permissions->setTextFormat(Qt::PlainText);
	layout->addWidget(m_permissions);

	const auto band = [header](QLabel** label, QPushButton** dismiss) {
		QWidget* band = new QWidget(header);
		band->setObjectName(QStringLiteral("connectionBar"));

		/* Without this a plain widget draws no background from the stylesheet at all. */
		band->setAttribute(Qt::WA_StyledBackground, true);

		QHBoxLayout* inside = new QHBoxLayout(band);
		inside->setContentsMargins(10, 5, 10, 5);

		*label = new QLabel(band);
		(*label)->setObjectName(QStringLiteral("connectionBarText"));
		(*label)->setTextFormat(Qt::PlainText);
		(*label)->setWordWrap(true);

		inside->addWidget(*label, 1);

		if (dismiss)
		{
			*dismiss = new QPushButton(QStringLiteral("×"), band);
			(*dismiss)->setObjectName(QStringLiteral("boardRefusalClose"));
			(*dismiss)->setFlat(true);
			(*dismiss)->setCursor(Qt::ArrowCursor);
			(*dismiss)->setToolTip(Qdc::BoardPane::tr("Dismiss"));

			/* Square and small: it is a way out of the band, not a control in the pane. */
			(*dismiss)->setFixedSize(18, 18);

			inside->addWidget(*dismiss, 0, Qt::AlignTop);
		}

		band->hide();

		return band;
	};

	/*
	 * The gap has no way out of it and no timer. It is not an event that happened once -
	 * it is what this view of the board is, for as long as the hub will not replay far
	 * enough back to close it, and a board that quietly stopped saying so would be one
	 * that looked complete.
	 */
	m_gapBand = band(&m_gap, nullptr);
	layout->addWidget(m_gapBand);

	m_refusalBand = band(&m_refusal, &m_refusalClose);
	layout->addWidget(m_refusalBand);

	connect(m_refusalClose, &QPushButton::clicked, this, [this] { clearRefusal(); });

	/*
	 * A refusal is something that happened, so it goes by itself. Single shot and
	 * restarted by each new one, which is what keeps a second refusal from being taken
	 * away by the timer the first one started.
	 */
	m_refusalTimer = new QTimer(this);
	m_refusalTimer->setSingleShot(true);
	m_refusalTimer->setInterval(REFUSAL_MS);

	connect(m_refusalTimer, &QTimer::timeout, this, [this] { clearRefusal(); });

	return header;
}

void Qdc::BoardPane::showBody(bool showing)
{
	m_body->setVisible(showing);
	m_filler->setVisible(!showing);
}

QWidget* Qdc::BoardPane::buildThreadList()
{
	QWidget* side = new QWidget(this);

	QVBoxLayout* layout = new QVBoxLayout(side);
	layout->setContentsMargins(12, 0, 6, 12);
	layout->setSpacing(6);

	QHBoxLayout* actions = new QHBoxLayout;
	actions->setSpacing(6);

	/*
	 * A post rather than a thread. What it starts is a thread and that is the wire's
	 * word for it; what somebody is doing is writing a post, and the one button on a
	 * board that has nothing on it yet should say what pressing it does.
	 */
	m_newPost = new QPushButton(tr("New post"), side);
	m_newPost->setObjectName(QStringLiteral("boardNewPost"));
	connect(m_newPost, &QPushButton::clicked, this, [this] { showCompose(Compose::Thread); });
	actions->addWidget(m_newPost);

	actions->addStretch(1);
	layout->addLayout(actions);

	m_list = new QTreeView(side);
	m_list->setObjectName(QStringLiteral("boardThreads"));
	m_list->setModel(m_threads);
	m_list->setAllColumnsShowFocus(true);
	m_list->setUniformRowHeights(true);
	m_list->setRootIsDecorated(true);
	m_list->setSelectionMode(QAbstractItemView::SingleSelection);
	m_list->setSelectionBehavior(QAbstractItemView::SelectRows);
	/*
	 * The subject takes what is left and the two beside it take what they need. Left
	 * interactive they keep a default width each whatever is in them, which together
	 * with a subject that stretches comes to more than the list is wide - so the list
	 * had a scrollbar under it and a subject cut off at a dozen characters, which is
	 * the one column somebody reads a board by.
	 */
	m_list->header()->setStretchLastSection(false);
	m_list->header()->setSectionResizeMode(BoardModel::SubjectColumn, QHeaderView::Stretch);
	m_list->header()->setSectionResizeMode(BoardModel::AuthorColumn,
	                                      QHeaderView::ResizeToContents);
	m_list->header()->setSectionResizeMode(BoardModel::WhenColumn,
	                                      QHeaderView::ResizeToContents);

	/*
	 * And a floor rather than only a preferred width. A splitter given sizes before it
	 * has one of its own scales them to what it has, which for a pane built into a
	 * stack and shown later is nothing much - so what actually decides how wide the
	 * list opens is this, and it has to be wide enough to hold the three columns
	 * without the subject being scrolled out of the side of them.
	 */
	m_list->setMinimumWidth(LIST_WIDTH);

	connect(m_list->selectionModel(), &QItemSelectionModel::currentRowChanged,
	        this, &BoardPane::selectionChanged);

	layout->addWidget(m_list, 1);

	return side;
}

QWidget* Qdc::BoardPane::buildReader()
{
	QWidget* reader = new QWidget(this);
	m_reader = reader;

	QVBoxLayout* layout = new QVBoxLayout(reader);
	layout->setContentsMargins(6, 0, 12, 12);
	layout->setSpacing(6);

	m_subject = new QLabel(reader);
	m_subject->setObjectName(QStringLiteral("boardSubject"));
	m_subject->setTextFormat(Qt::PlainText);
	m_subject->setWordWrap(true);

	QFont bigger = m_subject->font();
	bigger.setBold(true);
	m_subject->setFont(bigger);

	layout->addWidget(m_subject);

	/*
	 * Who wrote it and when, on one line, and that is the whole of what stands between
	 * a subject and the words underneath it.
	 *
	 * What this client knows about how far a post has been checked decides whether it is
	 * shown at all - see listable() - and is not narrated to the reader. A post that is
	 * on the screen is one whose bytes hashed to the name they were asked for; saying so
	 * beside every one of them tells a reader nothing they can act on, and reads as
	 * doubt about the post they are looking at.
	 */
	m_author = makeNote(QString(), reader);
	m_author->setObjectName(QStringLiteral("boardAuthor"));
	m_author->setTextFormat(Qt::PlainText);
	layout->addWidget(m_author);

	m_body = new QTextBrowser(reader);
	m_body->setObjectName(QStringLiteral("boardBody"));

	/*
	 * Nothing is followed from here. The document's markup is this client's own output
	 * and the addresses in it are a stranger's, so a link leaves as a signal for
	 * somebody who knows what may be opened, exactly as a chat line's does.
	 */
	m_body->setOpenLinks(false);
	m_body->setOpenExternalLinks(false);

	connect(m_body, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
		if (RichText::isLinkable(url) || url.scheme() == QStringLiteral("magnet"))
			emit linkActivated(url);
	});

	layout->addWidget(m_body, 1);

	/*
	 * What holds the post at the top when there are no words under it.
	 *
	 * A box layout hands leftover space out in proportion to the stretch its items
	 * carry, and where none of them carries any it shares it equally instead - so with
	 * the body hidden the subject, the line naming its author and the row of buttons
	 * each took a third of the pane and stood a long way apart. This is the item that
	 * takes the space in that case, and it is there exactly when the body is not.
	 */
	m_filler = new QWidget(reader);
	m_filler->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
	m_filler->hide();

	layout->addWidget(m_filler, 1);

	m_actions = new QWidget(reader);

	QHBoxLayout* actions = new QHBoxLayout(m_actions);
	actions->setContentsMargins(0, 0, 0, 0);
	actions->setSpacing(6);

	m_fetch = new QPushButton(tr("Fetch this post"), m_actions);
	m_fetch->setObjectName(QStringLiteral("boardFetch"));
	connect(m_fetch, &QPushButton::clicked, this, &BoardPane::fetch);
	actions->addWidget(m_fetch);

	m_parent = new QPushButton(tr("Ask the hub for the parent"), m_actions);
	m_parent->setObjectName(QStringLiteral("boardParent"));
	connect(m_parent, &QPushButton::clicked, this, &BoardPane::requestParent);
	actions->addWidget(m_parent);

	m_reply = new QPushButton(tr("Reply"), m_actions);
	m_reply->setObjectName(QStringLiteral("boardReply"));
	connect(m_reply, &QPushButton::clicked, this, [this] { showCompose(Compose::Reply); });
	actions->addWidget(m_reply);

	m_delete = new QPushButton(tr("Withdraw"), m_actions);
	m_delete->setObjectName(QStringLiteral("boardDelete"));
	connect(m_delete, &QPushButton::clicked, this, &BoardPane::askToDelete);
	actions->addWidget(m_delete);

	actions->addStretch(1);
	layout->addWidget(m_actions);

	m_question = new QWidget(reader);

	QHBoxLayout* asking = new QHBoxLayout(m_question);
	asking->setContentsMargins(0, 0, 0, 0);
	asking->setSpacing(6);

	/*
	 * What the hub is being asked to do, and nothing about what becomes of the bytes.
	 * The entry goes out of the index and the withdrawal travels to whoever is watching
	 * the board, which is all that is in this client's gift: a copy is a file on
	 * somebody else's disk, and a line promising it is gone would be a promise made on
	 * their behalf.
	 */
	QLabel* question = makeNote(
		tr("Withdraw this post? It is taken out of this hub's index, and other clients "
		   "are asked to drop their copies."), m_question);

	question->setObjectName(QStringLiteral("boardDeleteQuestion"));
	asking->addWidget(question, 1);

	m_deleteConfirm = new QPushButton(tr("Withdraw"), m_question);
	m_deleteConfirm->setObjectName(QStringLiteral("boardDeleteConfirm"));
	connect(m_deleteConfirm, &QPushButton::clicked, this, &BoardPane::confirmDelete);
	asking->addWidget(m_deleteConfirm);

	QPushButton* keep = new QPushButton(tr("Cancel"), m_question);
	keep->setObjectName(QStringLiteral("boardDeleteCancel"));
	connect(keep, &QPushButton::clicked, this, &BoardPane::dropTheQuestion);
	asking->addWidget(keep);

	m_question->hide();
	layout->addWidget(m_question);

	return reader;
}

QWidget* Qdc::BoardPane::buildComposer()
{
	m_composeBox = new QWidget(this);

	QVBoxLayout* layout = new QVBoxLayout(m_composeBox);
	layout->setContentsMargins(6, 0, 12, 12);
	layout->setSpacing(4);

	QHBoxLayout* top = new QHBoxLayout;
	top->setSpacing(6);

	/*
	 * The subject line above and the editor below, in the space the post being read was
	 * in. What is typed in either becomes the bytes of a document whose hash is the
	 * post's name, so nothing here is reduced on the way in or tidied on the way out -
	 * see prefillReply(), which is the one place text arrives from somewhere else.
	 */
	m_composeSubject = new QLineEdit(m_composeBox);
	m_composeSubject->setObjectName(QStringLiteral("boardComposeSubject"));
	m_composeSubject->setMaxLength(512);
	top->addWidget(m_composeSubject, 1);

	m_composeCancel = new QPushButton(tr("Cancel"), m_composeBox);
	m_composeCancel->setObjectName(QStringLiteral("boardComposeCancel"));
	connect(m_composeCancel, &QPushButton::clicked,
	        this, [this] { showCompose(Compose::None); });
	top->addWidget(m_composeCancel);

	layout->addLayout(top);

	m_composer = new Composer(m_composeBox);

	/*
	 * A post is the only thing on the screen while it is being written, so the box takes
	 * the area the post being read was in. Left as it is in a conversation it grows a few
	 * lines and stops, which is right at the foot of a chat and leaves somebody writing a
	 * post typing into a slot with an empty pane under it.
	 */
	m_composer->setFill(Composer::Fill::Available);

	/*
	 * And emptied here rather than on the way out, because submit() refuses some posts
	 * itself - one with no subject, a reply to something withdrawn while it was being
	 * written - and the box being emptied under those would take the words away as the
	 * reason for keeping them was being written into the note below it.
	 */
	m_composer->setClearOnSend(false);

	connect(m_composer, &Composer::sendRequested, this, &BoardPane::submit);

	connect(m_composer, &Composer::attachRequested, this, &BoardPane::attachRequested);
	connect(m_composer, &Composer::imagePasted, this, &BoardPane::imagePasted);
	connect(m_composer, &Composer::filesDropped, this, &BoardPane::filesDropped);

	layout->addWidget(m_composer, 1);

	m_composeNote = makeNote(QString(), m_composeBox);
	layout->addWidget(m_composeNote);

	return m_composeBox;
}

Qdc::BoardModel* Qdc::BoardPane::threads() const
{
	return m_threads;
}

const Qdc::BoardInfo& Qdc::BoardPane::boardInfo() const
{
	return m_board;
}

QString Qdc::BoardPane::board() const
{
	return m_board.name;
}

QString Qdc::BoardPane::reading() const
{
	return m_reading;
}

void Qdc::BoardPane::setMediaResolver(
	std::function<RichText::LocalMedia(const QString& tth)> media)
{
	m_media = std::move(media);

	refreshReading();
}

void Qdc::BoardPane::mediaChanged()
{
	/*
	 * The post being read and nothing else. What a body draws is decided when it is
	 * drawn, so a post that is not on the screen has nothing to redraw and will ask
	 * again the next time somebody opens it.
	 */
	refreshReading();
}

void Qdc::BoardPane::insertAttachment(const RichText::Attachment& file)
{
	m_composer->insertAttachment(file);

	/*
	 * Warned about, where a conversation would not warn. The rules for an attachment are
	 * the same ones chat uses; its lifetime is not. A post can be read years after it was
	 * written, and every reader of it who wants the attachment has to be able to find
	 * somebody still holding the file.
	 */
	say(m_composeNote, tr("Attached by hash. A post can be read years from now, and this "
	                      "file will only arrive for a reader if somebody is still "
	                      "sharing it then."));
}

void Qdc::BoardPane::setBoard(const BoardInfo& board)
{
	/*
	 * A descriptor for another board is another board. Two hubs, or two boards on one
	 * hub, may use the same subjects and the same threads and they are not the same
	 * board; what is held for the last one is dropped rather than mixed into this one,
	 * which would produce a board that exists nowhere.
	 */
	if (!m_threads->board().isEmpty() && m_threads->board() != board.name) clear();

	m_board = board;
	m_threads->setBoard(board.name);

	refreshBoardHeader();
	refreshActions();
}

void Qdc::BoardPane::refreshBoardHeader()
{
	/*
	 * The title reduced, and the branch on the reduced value rather than on the raw one:
	 * a title of nothing but invisible characters falls back to the board's name, which
	 * is protocol text held to a narrow character set, rather than leaving a heading
	 * that is there and says nothing.
	 */
	const QString title = RichText::displayText(m_board.title);

	say(m_title, title.isEmpty() ? RichText::displayText(m_board.name) : title);
	say(m_description, RichText::displayText(m_board.description));

	QStringList lines;

	/*
	 * What may be done here is said by which buttons are on the screen and not by a
	 * sentence about it: a board that can be posted to has a button that posts to it,
	 * and one that cannot does not. Only the two things a button cannot say are said -
	 * that a board has gone, and how much of it there is.
	 */
	if (m_board.removed)
	{
		lines.append(tr("This board no longer exists. What was read of it is still "
		                "here; nothing more will arrive."));
	}

	if (m_board.posts)
		lines.append(tr("%n post(s) indexed.", nullptr, static_cast<int>(m_board.posts)));

	say(m_permissions, lines.join(QLatin1Char(' ')));
}

void Qdc::BoardPane::addEntry(const BoardEntry& entry)
{
	addEntries(QVector<BoardEntry>{entry});
}

void Qdc::BoardPane::addEntries(const QVector<BoardEntry>& entries)
{
	m_threads->add(entries);

	/*
	 * The post being read may be among them: a tombstone for it arrives the same way as
	 * anything else, and there is nothing else that would tell this pane about it.
	 */
	refreshReading();
}

void Qdc::BoardPane::setPost(const BoardPost& post)
{
	if (post.tth.isEmpty()) return;

	PostState state = post.state;

	/*
	 * The pane holds both halves - the document that was fetched and the entry the hub
	 * sent - so it can see a disagreement about the author for itself, and a document
	 * that arrived called verified is drawn as disputed all the same. Drawing it as
	 * plainly verified would attribute the post to the document's claim, which is the
	 * one thing that must not be done with it.
	 */
	const BoardEntry entry = m_threads->entryOf(post.tth);

	if (state == PostState::Verified && !post.cid.isEmpty() && !entry.cid.isEmpty()
	    && post.cid != entry.cid)
	{
		state = PostState::Disputed;
	}

	m_threads->setState(post.tth, state);

	if (post.tth != m_reading) return;

	m_post = post;
	m_post.state = state;
	m_hasPost = state == PostState::Verified || state == PostState::Disputed;

	refreshReading();
}

void Qdc::BoardPane::setPostUnavailable(const QString& tth, const QString& reason)
{
	if (tth.isEmpty()) return;

	m_threads->setState(tth, PostState::Unavailable);

	if (tth != m_reading) return;

	/*
	 * The document is forgotten and the entry is not. An entry can outlive every copy of
	 * the document it names, and the answer to that is to keep saying what the entry
	 * says and offer to look again - not to drop the post, and not to draw it as an
	 * empty one.
	 */
	m_post = BoardPost();
	m_post.tth = tth;
	m_post.state = PostState::Unavailable;
	m_post.unavailableReason = reason;
	m_hasPost = false;

	refreshReading();
}

void Qdc::BoardPane::setPostUnverifiable(const QString& tth)
{
	if (tth.isEmpty()) return;

	m_threads->setVerificationFailed(tth);

	if (tth != m_reading) return;

	/*
	 * The reader was looking at it when it turned out not to be the post it claimed to
	 * be, so the reading area is emptied along with the row. What was on the screen was
	 * the hub's description of a document that has now been read and was something
	 * else, which is not a thing to leave up.
	 */
	m_post = BoardPost();
	m_hasPost = false;
	m_reading.clear();

	dropTheQuestion();
	refreshReading();

	emit readingChanged(QString());
}

void Qdc::BoardPane::reportRefusal(int code, const QString& description,
                                   const QString& tth, const QString& board)
{
	QStringList parts;

	const QString said = RichText::displayText(description);

	/*
	 * Ended for it where it did not end itself. What the hub said is a stranger's
	 * sentence and nothing obliges it to be punctuated, so the sentences this pane adds
	 * after it ran straight on from the last word of it.
	 */
	QString reason = said;

	if (!reason.isEmpty() && !QStringLiteral(".!?").contains(reason.back()))
		reason.append(QLatin1Char('.'));

	parts.append(reason.isEmpty()
		? tr("The hub refused this, with status %1 and no reason given.").arg(code)
		: tr("The hub refused this, with status %1: %2").arg(code).arg(reason));

	/*
	 * There is no token in this extension, so a refusal is matched to what it refuses by
	 * the hash or the board it names. Where it names a post that is held, the subject is
	 * shown so that a reader can tell which one - and where it names nothing this pane
	 * recognises it is still reported, because the hub declining to do what somebody
	 * asked is not something to keep to oneself.
	 */
	if (!tth.isEmpty())
	{
		const QString subject = RichText::displayText(m_threads->entryOf(tth).subject);

		parts.append(subject.isEmpty()
			? tr("It named the post %1.").arg(RichText::displayText(tth))
			: tr("It named the post \"%1\".").arg(subject));
	}

	if (!board.isEmpty())
		parts.append(tr("It named the board %1.").arg(RichText::displayText(board)));

	m_refusal->setText(parts.join(QLatin1Char(' ')));
	m_refusalBand->show();

	/* Restarted rather than left running, so a second refusal gets its own while. */
	m_refusalTimer->start();
}

void Qdc::BoardPane::clearRefusal()
{
	m_refusalTimer->stop();
	m_refusal->clear();
	m_refusalBand->hide();
}

void Qdc::BoardPane::setGap(bool gapped)
{
	if (gapped)
	{
		m_gap->setText(tr("This board has a gap that cannot be closed: the hub will "
		                  "replay nothing from before %1, and this client's record of "
		                  "the board begins later than that. What is listed is part of "
		                  "the board rather than all of it.")
			.arg(BoardModel::describeTimestamp(m_board.oldest)));
	}

	m_gapBand->setVisible(gapped);
}

void Qdc::BoardPane::setOwnCid(const QString& cid)
{
	if (m_ownCid == cid) return;

	m_ownCid = cid;

	/* Which posts are this user's own decides what is offered for the one being read. */
	refreshActions();
}

void Qdc::BoardPane::clear()
{
	const bool wasReading = !m_reading.isEmpty();

	m_threads->clear();

	m_reading.clear();
	m_post = BoardPost();
	m_hasPost = false;

	m_gapBand->hide();
	m_refusalBand->hide();

	dropTheQuestion();
	showCompose(Compose::None);
	refreshReading();

	/* Nothing is being shown any more, which is a change of what is being read. */
	if (wasReading) emit readingChanged(QString());
}

void Qdc::BoardPane::read(const QString& tth)
{
	const bool changed = tth != m_reading;

	if (changed)
	{
		/* A question about the last post is not a question about this one. */
		dropTheQuestion();

		m_reading = tth;

		/* The document of the post that was being read says nothing about this one. */
		if (m_post.tth != tth)
		{
			m_post = BoardPost();
			m_hasPost = false;
		}
	}

	const QModelIndex row = m_threads->indexOf(tth);

	if (row.isValid() && m_list->selectionModel()->currentIndex() != row)
		m_list->setCurrentIndex(row);

	refreshReading();

	/*
	 * Said after the pane has been drawn, so that whoever acts on it is acting on a post
	 * that is already on the screen. Only where it moved: a reader clicking the row they
	 * are already on has opened nothing new.
	 */
	if (changed) emit readingChanged(m_reading);
}

void Qdc::BoardPane::selectionChanged()
{
	read(m_threads->tthAt(m_list->selectionModel()->currentIndex()));
}

void Qdc::BoardPane::restoreSelection()
{
	m_list->expandAll();

	if (m_reading.isEmpty()) return;

	const QModelIndex row = m_threads->indexOf(m_reading);
	if (row.isValid()) m_list->setCurrentIndex(row);
}

Qdc::PostState Qdc::BoardPane::stateOfReading() const
{
	return m_threads->stateOf(m_reading);
}

void Qdc::BoardPane::refreshReading()
{
	if (m_reading.isEmpty())
	{
		say(m_subject, QString());
		say(m_author, tr("Choose a post from the list to read it."));

		m_body->clear();
		showBody(false);

		refreshActions();
		return;
	}

	const BoardEntry entry = m_threads->entryOf(m_reading);
	const PostState state = stateOfReading();

	m_drawn = state;

	/*
	 * Whether the words on the screen are the post's own. A subject is the hub's until
	 * the document has been read, so the document's is preferred the moment there is
	 * one - and a post the document could not vouch for is not listed at all.
	 */
	const bool fromDocument = m_hasPost
		&& (state == PostState::Verified || state == PostState::Disputed);

	/*
	 * A withdrawn post leaves the list, so nothing can select one and this is only ever
	 * reached by a tombstone arriving for the post already open. There is nothing to
	 * draw: a tombstone carries a root, a board and a time and deliberately not a word
	 * of what was withdrawn.
	 */
	if (state == PostState::Withdrawn)
	{
		say(m_subject, QString());
		say(m_author, tr("Choose a post from the list to read it."));

		m_body->clear();
		showBody(false);

		refreshActions();
		return;
	}

	const QString subject = fromDocument
		? RichText::displayText(m_post.subject)
		: RichText::displayText(entry.subject);

	say(m_subject, subject.isEmpty() ? tr("(no subject)") : subject);
	say(m_author, attribution(entry));

	/*
	 * The one thing a reader is told instead of the post, because it is the one thing
	 * that is neither the post nor a failure of it: an index entry can outlive every
	 * copy of the document it names. One line, no advice - the entry stays in the list
	 * and the document may be on a machine that is merely switched off today, and
	 * neither of those is a sentence worth spending on somebody who wanted to read
	 * something.
	 */
	if (!fromDocument && state == PostState::Unavailable)
	{
		m_body->setPlainText(tr("This post could not be retrieved."));
		showBody(true);

		refreshActions();
		return;
	}

	if (fromDocument)
	{
		RichText::RenderOptions options;
		options.colours = markup_colours();
		options.fontPointSize = m_body->font().pointSizeF();
		options.mediaWidth = qMax(0, m_body->viewport()->width() - 24);

		/*
		 * Asked, and never told: whether a picture can be shown is a question about what
		 * this machine holds and not about what the document says. A file that is not here
		 * is a chip to click, which is what an attachment nobody can find any more looks
		 * like as well - the post is still a post either way.
		 */
		options.media = m_media;

		/*
		 * Only a body that says it is the markdown subset is read as one. Without that
		 * flag the body is literal text and is drawn as it arrived, asterisks and all -
		 * the flag is not a hint to be guessed at from what the text looks like. A body
		 * that does carry it goes through this client's own renderer and never through a
		 * general markdown or HTML one, which BBS0 relaxes nothing about.
		 */
		m_body->setHtml(m_post.richText
			? RichText::renderMarkdown(m_post.body, options)
			: RichText::renderPlain(m_post.body, options));

		showBody(true);
	}
	else
	{
		/*
		 * Hidden rather than emptied. An empty reading area is what a post with no words
		 * in it would look like, and a post nobody could fetch has to be distinguishable
		 * from an empty one and from an error.
		 */
		m_body->clear();
		showBody(false);
	}

	refreshActions();
}

void Qdc::BoardPane::refreshActions()
{
	const bool reading = !m_reading.isEmpty();
	const PostState state = reading ? stateOfReading() : PostState::Announced;
	const BoardEntry entry = m_threads->entryOf(m_reading);

	const bool withdrawn = reading && state == PostState::Withdrawn;

	m_fetch->setVisible(reading && !withdrawn && state != PostState::Verified
	                    && state != PostState::Disputed);

	m_fetch->setEnabled(state != PostState::Fetching);
	m_fetch->setText(state == PostState::Unavailable ? tr("Try again")
	                                                 : tr("Fetch this post"));

	/*
	 * A parent this client does not hold can be asked for one entry at a time. Offered
	 * only where the parent is genuinely absent: a parent held as a tombstone is not
	 * missing, it was withdrawn, and asking for it again would answer nothing.
	 */
	m_parent->setVisible(reading && !entry.parent.isEmpty()
	                     && !m_threads->holds(entry.parent));

	m_reply->setVisible(reading && !withdrawn
	                    && m_board.may(BoardPermission::Reply) && !m_board.removed);

	/*
	 * Whose post this is, as the hub told the board. The entry's client id is the one
	 * the hub accepted the post from and is the same claim the hub will check a
	 * withdrawal against, so comparing it against our own answers the right question -
	 * unlike the document's own claim of an author, which anybody may write.
	 *
	 * Nobody's own while either side is unknown. An installation without a client id
	 * yet, or an entry the hub sent none for, is not grounds for offering to withdraw
	 * somebody else's post.
	 */
	const bool mine = !m_ownCid.isEmpty() && entry.cid == m_ownCid;

	/*
	 * Withdrawing anybody's covers this post whoever wrote it; withdrawing one's own
	 * covers it only where it is. Offered on those terms rather than on the permission
	 * alone, for the reason the read-only board below is given: a button that is there
	 * and refused every time invites the user to find out why, and the answer would be
	 * that the post was never theirs to withdraw.
	 */
	const bool mayDelete = m_board.may(BoardPermission::WithdrawAny)
		|| (m_board.may(BoardPermission::WithdrawOwn) && mine);

	const bool offerDelete = reading && !withdrawn && mayDelete && !m_board.removed;

	m_delete->setVisible(offerDelete);

	/* A question that can no longer be answered goes, rather than waiting to be. */
	if (!offerDelete) dropTheQuestion();

	/*
	 * Gone rather than greyed where the board does not take posts. A control that is
	 * there and cannot be used invites somebody to find out why; a read-only board is
	 * simply one without that button on it.
	 */
	m_newPost->setVisible(m_board.may(BoardPermission::StartThread) && !m_board.removed);

	/*
	 * A reply being written is not taken away when its post leaves the board. The words
	 * are somebody's own and the tombstone arrived from elsewhere, so the box keeps them
	 * and says what has happened instead; the submission is where it is stopped.
	 */
	if (m_compose == Compose::Reply && !m_replyTo.isEmpty()
	    && m_threads->isWithdrawn(m_replyTo))
	{
		say(m_composeNote, tr("The post this replies to has been withdrawn. What you "
		                      "wrote is kept here."));
	}
}

void Qdc::BoardPane::fetch()
{
	if (m_reading.isEmpty()) return;

	m_post.unavailableReason.clear();
	m_threads->setState(m_reading, PostState::Fetching);

	refreshReading();

	emit bodyRequested(m_reading);
}

void Qdc::BoardPane::requestParent()
{
	if (m_reading.isEmpty()) return;

	const QString parent = m_threads->entryOf(m_reading).parent;
	if (parent.isEmpty()) return;

	emit parentRequested(parent);
}

void Qdc::BoardPane::askToDelete()
{
	if (m_reading.isEmpty()) return;

	m_actions->hide();
	m_question->show();
}

void Qdc::BoardPane::confirmDelete()
{
	const QString post = m_reading;

	dropTheQuestion();

	if (post.isEmpty()) return;

	emit withdrawRequested(post);
}

void Qdc::BoardPane::dropTheQuestion()
{
	m_question->hide();
	m_actions->show();
}

void Qdc::BoardPane::showCompose(Compose mode)
{
	m_compose = mode;

	if (mode == Compose::None)
	{
		m_replyTo.clear();
		m_composeSubject->clear();
		m_composer->clear();

		say(m_composeNote, QString());

		/* The post that was being read comes back, exactly as it was left. */
		m_area->setCurrentWidget(m_reader);
		refreshReading();
		return;
	}

	/* A refusal of the last attempt is not an answer about this one. */
	m_refusalBand->hide();

	dropTheQuestion();

	m_replyTo = mode == Compose::Reply ? m_reading : QString();

	m_composeSubject->clear();
	m_composer->clear();

	if (mode == Compose::Thread)
	{
		m_composer->setPlaceholderText(tr("Write the post"));
		m_composeSubject->setPlaceholderText(tr("Subject, which a new post needs"));
		say(m_composeNote, tr("A new post. Its subject and body become a document "
		                      "whose hash is the post's name."));
	}
	else
	{
		m_composer->setPlaceholderText(tr("Write the reply"));
		m_composeSubject->setPlaceholderText(tr("Subject, which a reply may leave empty"));
		say(m_composeNote, tr("A reply to the post you are reading."));

		prefillReply();
	}

	m_area->setCurrentWidget(m_composeBox);
	m_composer->focusInput();
}

void Qdc::BoardPane::prefillReply()
{
	if (m_replyTo.isEmpty()) return;

	/*
	 * The document's words where the document has been read, and the hub's subject
	 * where it has not. A reply begun on a post nobody fetched quotes nothing, because
	 * there is nothing here that the post said - only what the hub says it is about.
	 */
	const BoardEntry entry = m_threads->entryOf(m_replyTo);

	const QString subject = m_hasPost && m_post.tth == m_replyTo
		? m_post.subject : entry.subject;

	/* Once. A reply to "Re: x" is about "Re: x", and "Re: Re: x" says nothing more. */
	const QString prefix = tr("Re: ", "prefix on the subject of a reply");

	m_composeSubject->setText(subject.startsWith(prefix, Qt::CaseInsensitive)
		? subject : prefix + subject);

	if (!m_hasPost || m_post.tth != m_replyTo) return;

	/*
	 * The author's own bytes, and never through displayText(): that is a reduction for
	 * drawing a stranger's text on a screen, and a body on its way into a document would
	 * carry a replacement character or an ellipsis of ours into somebody's post.
	 *
	 * Handed over as a body and a flag rather than as lines with "> " written in front
	 * of them. What the reply is quoting is a post, not the characters that post was
	 * written with, and the composer is where a document is built.
	 */
	m_composer->setQuoted(m_post.body.trimmed(), m_post.richText);
}

void Qdc::BoardPane::submit(const QString& message, bool richText)
{
	if (m_compose == Compose::None) return;

	/*
	 * The subject as it was typed, not reduced and not trimmed of anything a user meant:
	 * it is about to be part of a document whose hash is the post's identity, so the
	 * bytes have to be theirs.
	 */
	const QString subject = m_composeSubject->text();

	if (m_compose == Compose::Thread)
	{
		if (subject.trimmed().isEmpty())
		{
			say(m_composeNote, tr("A new post needs a subject."));
			return;
		}

		emit threadSubmitted(subject, message, richText);
	}
	else
	{
		if (m_replyTo.isEmpty()) return;

		/*
		 * The post being replied to may have gone from the index while the reply was
		 * being written. Its hash is still a valid parent and the hub will very likely
		 * refuse the post for naming an entry it no longer indexes, so saying so here is
		 * better than letting the refusal explain it.
		 */
		if (m_threads->isWithdrawn(m_replyTo))
		{
			say(m_composeNote, tr("The post this replies to has been withdrawn, so the "
			                      "hub will most likely refuse a reply to it. What you "
			                      "wrote is kept here."));
			return;
		}

		emit replySubmitted(m_replyTo, subject, message, richText);
	}

	/*
	 * Handed over, and back to the post that was being read. This extension has no
	 * success message - the entry arriving in the list is the acknowledgement and the
	 * only one there is - so nothing here says the post went up; what says it went up is
	 * its appearing among the others. Staying in the composer until then leaves somebody
	 * looking at what they have already sent with nothing to do in front of them.
	 *
	 * A refusal has already returned above without reaching this, with the words still
	 * in the box, so what is cleared here is only what has actually gone.
	 */
	showCompose(Compose::None);
}

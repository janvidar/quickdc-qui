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
#include <QTextBrowser>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

namespace {

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
 * Who the post is attributed to, and what that is worth.
 *
 * The hub's claim, always: it says it accepted the post from a session that had
 * authenticated as this CID, which is a statement about one event on one hub and not a
 * proven identity - a CID is the hash of a secret rather than a public key, and nothing
 * can be signed with it. The nickname is drawn beside it because it is what a reader
 * recognises, and is never what the post is attributed to.
 *
 * Where the document names a different author the disagreement is shown and the hub's
 * claim is still the one used. There is no automatic resolution: the honest case is a
 * post relayed to a second hub by somebody who did not write it, and it looks exactly
 * like a forgery.
 */
QString attribution(const Qdc::BoardEntry& entry, const Qdc::BoardPost& post,
                    bool hasPost, bool disputed)
{
	using Pane = Qdc::BoardPane;

	const QString cid = Qdc::RichText::displayText(entry.cid);
	const QString nick = Qdc::RichText::displayText(entry.nick);

	if (cid.isEmpty() && nick.isEmpty())
		return Pane::tr("The hub named nobody as having submitted this.");

	QString line = cid.isEmpty()
		? Pane::tr("The hub says it accepted this from a session it called %1.").arg(nick)
		: (nick.isEmpty()
			? Pane::tr("The hub says it accepted this from the session %1.").arg(cid)
			: Pane::tr("The hub says it accepted this from the session %1, which was "
			           "called %2 at the time.").arg(cid, nick));

	line += QLatin1Char(' ');
	line += Pane::tr("Neither is proof of authorship: a CID is not a verified identity, "
	                 "and a nickname attributes nothing.");

	if (disputed)
	{
		line += QLatin1Char('\n');
		line += Pane::tr("The document claims a different author, %1. This post is "
		                 "attributed to the hub's claim and not to the document's, "
		                 "because nothing here can settle which is right.")
			.arg(Qdc::RichText::displayText(post.cid));
	}
	else if (hasPost && !post.cid.isEmpty())
	{
		line += QLatin1Char(' ');
		line += Pane::tr("The document claims the same author.");
	}

	return line;
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

	m_split = new QSplitter(Qt::Horizontal, this);
	m_split->addWidget(buildThreadList());
	m_split->addWidget(buildReader());
	m_split->setStretchFactor(0, 2);
	m_split->setStretchFactor(1, 3);

	layout->addWidget(m_split, 1);

	/*
	 * A rebuild moves rows anywhere in the tree - an orphan is adopted the moment its
	 * parent arrives - so the selection is put back by the post it was on rather than by
	 * the row it was in, and the threads are opened again: a board whose replies are
	 * folded away is a board nobody reads.
	 */
	connect(m_threads, &QAbstractItemModel::modelReset,
	        this, &BoardPane::restoreSelection);

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

	const auto band = [header](QLabel** label) {
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

		band->hide();

		return band;
	};

	m_gapBand = band(&m_gap);
	layout->addWidget(m_gapBand);

	m_refusalBand = band(&m_refusal);
	layout->addWidget(m_refusalBand);

	return header;
}

QWidget* Qdc::BoardPane::buildThreadList()
{
	QWidget* side = new QWidget(this);

	QVBoxLayout* layout = new QVBoxLayout(side);
	layout->setContentsMargins(12, 0, 6, 12);
	layout->setSpacing(6);

	QHBoxLayout* actions = new QHBoxLayout;
	actions->setSpacing(6);

	m_newThread = new QPushButton(tr("New thread"), side);
	m_newThread->setObjectName(QStringLiteral("boardNewThread"));
	connect(m_newThread, &QPushButton::clicked, this, [this] { showCompose(Compose::Thread); });
	actions->addWidget(m_newThread);

	/*
	 * A full re-read rather than a refresh. Resuming from a cursor is what the client
	 * does the rest of the time and it cannot see a hub that reordered or removed
	 * entries; reading the board from the beginning and reconciling the result against
	 * what is held is the only detection there is, so it is offered rather than left to
	 * happen on some schedule nobody can see.
	 */
	m_reread = new QPushButton(tr("Re-read the board"), side);
	m_reread->setObjectName(QStringLiteral("boardReread"));
	connect(m_reread, &QPushButton::clicked, this, [this] { emit rereadRequested(); });
	actions->addWidget(m_reread);

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
	m_list->header()->setStretchLastSection(false);
	m_list->header()->setSectionResizeMode(BoardModel::SubjectColumn, QHeaderView::Stretch);

	connect(m_list->selectionModel(), &QItemSelectionModel::currentRowChanged,
	        this, &BoardPane::selectionChanged);

	layout->addWidget(m_list, 1);

	return side;
}

QWidget* Qdc::BoardPane::buildReader()
{
	QWidget* reader = new QWidget(this);

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

	/* Where each field on the screen came from, which is the whole point of the pane. */
	m_provenance = makeNote(QString(), reader);
	m_provenance->setTextFormat(Qt::PlainText);
	layout->addWidget(m_provenance);

	m_author = makeNote(QString(), reader);
	m_author->setTextFormat(Qt::PlainText);
	layout->addWidget(m_author);

	m_dates = makeNote(QString(), reader);
	m_dates->setTextFormat(Qt::PlainText);
	layout->addWidget(m_dates);

	m_state = new QLabel(reader);
	m_state->setObjectName(QStringLiteral("boardState"));
	m_state->setTextFormat(Qt::PlainText);
	m_state->setWordWrap(true);
	layout->addWidget(m_state);

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

	QHBoxLayout* actions = new QHBoxLayout;
	actions->setSpacing(6);

	m_fetch = new QPushButton(tr("Fetch this post"), reader);
	m_fetch->setObjectName(QStringLiteral("boardFetch"));
	connect(m_fetch, &QPushButton::clicked, this, &BoardPane::fetch);
	actions->addWidget(m_fetch);

	m_parent = new QPushButton(tr("Ask the hub for the parent"), reader);
	m_parent->setObjectName(QStringLiteral("boardParent"));
	connect(m_parent, &QPushButton::clicked, this, &BoardPane::requestParent);
	actions->addWidget(m_parent);

	m_reply = new QPushButton(tr("Reply"), reader);
	m_reply->setObjectName(QStringLiteral("boardReply"));
	connect(m_reply, &QPushButton::clicked, this, [this] { showCompose(Compose::Reply); });
	actions->addWidget(m_reply);

	/*
	 * Withdraw, and never delete. Withdrawing removes an entry from the hub's index; the
	 * document is by then held by everybody who has read it and will go on being served
	 * by them, so an interface offering to delete a post promises something this
	 * extension cannot deliver.
	 */
	m_withdraw = new QPushButton(tr("Withdraw"), reader);
	m_withdraw->setObjectName(QStringLiteral("boardWithdraw"));
	connect(m_withdraw, &QPushButton::clicked, this, &BoardPane::withdraw);
	actions->addWidget(m_withdraw);

	actions->addStretch(1);
	layout->addLayout(actions);

	m_withdrawNote = makeNote(
		tr("Withdrawing takes the post out of this hub's index. It cannot promise the "
		   "post is gone: anyone who has read it may still hold the document and may "
		   "still hand it to somebody else."), reader);
	layout->addWidget(m_withdrawNote);

	layout->addWidget(buildComposer());

	return reader;
}

QWidget* Qdc::BoardPane::buildComposer()
{
	m_composeBox = new QWidget(this);

	QVBoxLayout* layout = new QVBoxLayout(m_composeBox);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);

	QHBoxLayout* top = new QHBoxLayout;
	top->setSpacing(6);

	/*
	 * The subject is not filled in from the post being replied to. What is typed here
	 * becomes bytes of a document whose hash is the post's name, so it must be exactly
	 * what its author wrote and nothing this client tidied on the way - and a subject
	 * copied out of a stranger's post could not be drawn reduced in a field that is
	 * about to be sent.
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
	connect(m_composer, &Composer::sendRequested, this, &BoardPane::submit);
	layout->addWidget(m_composer);

	m_composeNote = makeNote(QString(), m_composeBox);
	layout->addWidget(m_composeNote);

	m_composeBox->hide();

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

void Qdc::BoardPane::setBoard(const BoardInfo& board)
{
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

	QStringList allowed;

	if (m_board.may(BoardPermission::StartThread)) allowed.append(tr("start a thread"));
	if (m_board.may(BoardPermission::Reply)) allowed.append(tr("reply"));

	if (m_board.may(BoardPermission::WithdrawAny))
		allowed.append(tr("withdraw any post"));
	else if (m_board.may(BoardPermission::WithdrawOwn))
		allowed.append(tr("withdraw your own posts"));

	QStringList lines;

	if (m_board.removed)
	{
		lines.append(tr("This board no longer exists. What was read of it is still "
		                "here; nothing more will arrive."));
	}
	else if (allowed.isEmpty())
	{
		lines.append(tr("The hub says you may read this board and nothing else."));
	}
	else
	{
		/*
		 * What the hub says may be done, which is what to offer and not a boundary: the
		 * hub re-checks every operation as it arrives, and one this permitted may still
		 * come back refused.
		 */
		lines.append(tr("The hub says you may %1 here.")
			.arg(allowed.join(QStringLiteral(", "))));
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

void Qdc::BoardPane::reportRefusal(int code, const QString& description,
                                   const QString& tth, const QString& board)
{
	QStringList parts;

	const QString said = RichText::displayText(description);

	parts.append(said.isEmpty()
		? tr("The hub refused this, with status %1 and no reason given.").arg(code)
		: tr("The hub refused this, with status %1: %2").arg(code).arg(said));

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

void Qdc::BoardPane::clear()
{
	m_threads->clear();

	m_reading.clear();
	m_post = BoardPost();
	m_hasPost = false;

	m_gapBand->hide();
	m_refusalBand->hide();

	showCompose(Compose::None);
	refreshReading();
}

void Qdc::BoardPane::read(const QString& tth)
{
	if (tth != m_reading)
	{
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
		say(m_provenance, tr("Choose a post from the list to read it."));
		say(m_author, QString());
		say(m_dates, QString());
		say(m_state, QString());

		m_body->clear();
		m_body->hide();

		refreshActions();
		return;
	}

	const BoardEntry entry = m_threads->entryOf(m_reading);
	const PostState state = stateOfReading();

	/* Whether what is on the screen came out of a document or out of the hub's index. */
	const bool fromDocument = m_hasPost
		&& (state == PostState::Verified || state == PostState::Disputed);

	if (state == PostState::Withdrawn)
	{
		/* A tombstone carries a root, a board and a time, and deliberately nothing else. */
		say(m_subject, tr("Withdrawn post"));

		say(m_provenance, tr("The hub's index no longer carries this post, and a "
		                     "tombstone says nothing about who wrote it or what it "
		                     "said. Replies to it are separate posts and are still "
		                     "listed."));

		say(m_author, QString());
		say(m_dates, tr("Withdrawn %1.")
			.arg(BoardModel::describeTimestamp(entry.timestamp)));

		say(m_state, tr("Withdrawn. That takes the post out of this hub's index and "
		                "promises nothing about the document itself."));

		m_body->clear();
		m_body->hide();

		refreshActions();
		return;
	}

	const QString subject = fromDocument
		? RichText::displayText(m_post.subject)
		: RichText::displayText(entry.subject);

	say(m_subject, subject.isEmpty() ? tr("(no subject)") : subject);

	switch (state)
	{
		case PostState::Announced:
			say(m_provenance, tr("This is the hub's index line and not the post. The "
			                     "subject above is the hub's, and nothing here has been "
			                     "checked against the document."));
			break;

		case PostState::Fetching:
			say(m_provenance, tr("This is still the hub's index line. The subject above "
			                     "is the hub's until the document has been read."));
			break;

		case PostState::Verified:
		case PostState::Disputed:
			say(m_provenance, tr("The subject and the body below came out of the "
			                     "document, which hashed to the root this entry names."));
			break;

		case PostState::Unavailable:
			say(m_provenance, tr("This is the hub's index line. The document itself "
			                     "could not be retrieved, so the subject above is the "
			                     "hub's and the post's own words are not here."));
			break;

		case PostState::Withdrawn:
			break;
	}

	say(m_author, attribution(entry, m_post, fromDocument,
	                          state == PostState::Disputed));

	QStringList dates;
	dates.append(tr("The hub accepted it %1.")
		.arg(BoardModel::describeTimestamp(entry.timestamp)));

	if (fromDocument && m_post.dated)
	{
		/*
		 * The author's own claim about when they wrote it. Nothing verifies it, it is
		 * never what anything is ordered by, and it is presented as a claim rather than
		 * as a time - it is also the only timestamp that survives the post being carried
		 * to another hub.
		 */
		dates.append(tr("Its author claims to have written it %1, which nothing here "
		                "can check.").arg(BoardModel::describeTimestamp(m_post.date)));
	}

	say(m_dates, dates.join(QLatin1Char('\n')));

	switch (state)
	{
		case PostState::Announced:
			/*
			 * Nothing is fetched because an entry arrived: a body costs a transfer, and
			 * fetching one tells whoever serves it that this reader has read it. So the
			 * unread state is a state on the screen with a button in it, rather than
			 * something that quietly resolves itself.
			 */
			say(m_state, tr("Not fetched. Nothing about this post has been verified."));
			break;

		case PostState::Fetching:
			say(m_state, tr("Fetching the document."));
			break;

		case PostState::Verified:
			say(m_state, tr("Verified: the bytes that arrived hashed to the root this "
			                "entry names, so the author, the parent, the subject and "
			                "the body below are the document's own."));
			break;

		case PostState::Disputed:
			say(m_state, tr("Verified bytes, disputed author: the document and the hub "
			                "name different authors. A post relayed to another hub by "
			                "somebody who did not write it looks exactly like a "
			                "forgery."));
			break;

		case PostState::Unavailable:
		{
			const QString reason = RichText::displayText(m_post.unavailableReason);

			say(m_state, reason.isEmpty()
				? tr("The content could not be retrieved. The entry is kept, and the "
				     "document may be on a machine that is switched off today.")
				: tr("The content could not be retrieved: %1. The entry is kept, and "
				     "the document may be on a machine that is switched off today.")
					.arg(reason));
			break;
		}

		case PostState::Withdrawn:
			break;
	}

	if (fromDocument)
	{
		RichText::RenderOptions options;
		options.colours = markup_colours();
		options.fontPointSize = m_body->font().pointSizeF();
		options.mediaWidth = qMax(0, m_body->viewport()->width() - 24);

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

		m_body->show();
	}
	else
	{
		/*
		 * Hidden rather than emptied. An empty reading area is what a post with no words
		 * in it would look like, and a post nobody could fetch has to be distinguishable
		 * from an empty one and from an error.
		 */
		m_body->clear();
		m_body->hide();
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
	 * Withdrawal is offered where either permission exists. Whether the post is this
	 * user's own is not something the pane can tell - it is a comparison against our own
	 * CID, which is not part of a board - and the hub re-checks it anyway.
	 */
	const bool mayWithdraw = m_board.may(BoardPermission::WithdrawOwn)
		|| m_board.may(BoardPermission::WithdrawAny);

	m_withdraw->setVisible(reading && !withdrawn && mayWithdraw && !m_board.removed);
	m_withdrawNote->setVisible(m_withdraw->isVisible());

	m_newThread->setEnabled(m_board.may(BoardPermission::StartThread)
	                        && !m_board.removed);

	if (m_compose == Compose::Reply && (!reading || withdrawn))
		showCompose(Compose::None);
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

void Qdc::BoardPane::withdraw()
{
	if (m_reading.isEmpty()) return;

	emit withdrawRequested(m_reading);
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
		m_composeBox->hide();
		return;
	}

	/* A refusal of the last attempt is not an answer about this one. */
	m_refusalBand->hide();

	m_replyTo = mode == Compose::Reply ? m_reading : QString();

	m_composeSubject->clear();
	m_composer->clear();

	if (mode == Compose::Thread)
	{
		m_composer->setPlaceholderText(tr("Write the post"));
		m_composeSubject->setPlaceholderText(tr("Subject, which a new thread needs"));
		say(m_composeNote, tr("A new thread. Its subject and body become a document "
		                      "whose hash is the post's name."));
	}
	else
	{
		m_composer->setPlaceholderText(tr("Write the reply"));
		m_composeSubject->setPlaceholderText(tr("Subject, which a reply may leave empty"));
		say(m_composeNote, tr("A reply to the post you are reading."));
	}

	m_composeBox->show();
	m_composer->focusInput();
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
			say(m_composeNote, tr("A new thread needs a subject."));
			return;
		}

		emit threadSubmitted(subject, message, richText);
	}
	else
	{
		if (m_replyTo.isEmpty()) return;

		emit replySubmitted(m_replyTo, subject, message, richText);
	}

	/*
	 * Handed over, and not published. This extension has no success message - the index
	 * entry coming back is the acknowledgement and the only one there is - so nothing
	 * here says the post went up, and what was written stays in the box until an entry
	 * or a refusal settles it. A composer emptied on submission loses the words when the
	 * hub declines.
	 */
	say(m_composeNote, tr("Handed to the hub. It is not published until the hub's entry "
	                      "for it arrives in the list; there is no other acknowledgement, "
	                      "so what you wrote is kept here until then."));
}

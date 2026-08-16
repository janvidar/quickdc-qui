/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "hubsview.h"

#include "fileactions.h"
#include "boardmodel.h"
#include "boardpane.h"
#include "composer.h"
#include "glyphs.h"
#include "hubheader.h"
#include "hubpane.h"
#include "richtext.h"
#include "theme.h"
#include "usermenu.h"
#include "usermodel.h"

#include <core/boardbridge.h>
#include <core/completed.h>
#include <core/corethread.h>
#include <core/configbridge.h>
#include <core/favourites.h>
#include <core/transferbridge.h>
#include <core/hubbridge.h>
#include <core/mediabridge.h>
#include <core/recenthubs.h>

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QImageReader>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QActionGroup>
#include <QMenu>
#include <QMessageBox>
#include <QApplication>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QSplitter>
#include <QTimer>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

/** Where the pictures-in-chat settings live; see HubsView::fetchesMediaOn(). */
const QString CHAT_MEDIA = QStringLiteral("Chat media");
const QString MEDIA_FETCH = QStringLiteral("Fetch automatically");
const QString MEDIA_LIMIT = QStringLiteral("Maximum size");
const QString MEDIA_KEEP = QStringLiteral("Keep pasted for days");

/** Where the folder a fetched picture lands in is named. */
const QString DOWNLOADS_GROUP = QStringLiteral("Downloads");

/**
 * Where the bulletin board settings live; see HubsView::prefetchesOn().
 *
 * The two policies are the ones a hub may answer for itself, under the same names in
 * its own group of the session store, which is how the per hub choices in the tree's
 * menu are written. Both are read once and kept in hand, because a post arriving is
 * no time to be reading a preference file.
 */
const QString BOARDS_GROUP = QStringLiteral("Boards");

/** Where this installation's own client id lives; see HubsView::ownCid(). */
const QString USER_GROUP = QStringLiteral("User");

/**
 * The hub this program can run itself; see HubsView::reloadLocalHub().
 *
 * The group and the key are the core's own, so that the switch on the settings page,
 * the row in this tree and the hub in the core are all reading one thing. Its address
 * is not configured anywhere: the hub answers on the port this client already listens
 * on, and a connection is sorted onto it by what the peer says first, so the port is
 * read from the network settings rather than from a setting of the hub's own.
 */
const QString LOCAL_HUB_GROUP = QStringLiteral("Local hub");
const QString LOCAL_HUB_ENABLED = QStringLiteral("Enabled");
constexpr bool LOCAL_HUB_ENABLED_DEFAULT = false;

const QString NETWORK_GROUP = QStringLiteral("Network");
const QString NETWORK_PORT = QStringLiteral("Port");
constexpr int NETWORK_PORT_DEFAULT = 1411;

const QString BOARD_PREFETCH = QStringLiteral("Fetch a thread ahead");
const QString BOARD_SERVE = QStringLiteral("Offer what I have read");
const QString BOARD_KEEP = QStringLiteral("Keep posts for days");
const QString BOARD_CACHE = QStringLiteral("Cache size");

/**
 * What was open last time, so that it can be opened again.
 *
 * The hubs go in the application's own settings as a numbered list, which is the
 * shape [Favourites] and [Shares] already use so a hand-edited file stays
 * predictable. The conversations on a hub go in the session store under that
 * hub's address, beside the other things remembered per hub.
 */
const QString WINDOWS_GROUP = QStringLiteral("Windows");
const QString WINDOWS_TOTAL = QStringLiteral("Total");
const QString WINDOWS_HUB = QStringLiteral("Hub ");
const QString DMS_TOTAL = QStringLiteral("Conversations");
const QString DMS_ENTRY = QStringLiteral("Conversation ");

/** And the boards open on a hub, in the same shape and in the same group. */
const QString BOARDS_TOTAL = QStringLiteral("Boards");
const QString BOARDS_ENTRY = QStringLiteral("Board ");

/**
 * How much of a transcript is put back on screen.
 *
 * The file keeps everything; this is what a pane is filled with, and it matches
 * ChatModel's own capacity - the model would drop anything past it the moment it
 * was appended, so reading more would be work thrown away.
 */
constexpr int RESTORED_LINES = 4000;

/** Two megabytes: enough for a photograph, small enough to arrive unnoticed. */
constexpr int MEDIA_LIMIT_DEFAULT = 2 * 1024 * 1024;

/**
 * How long a pasted picture is kept: about six months.
 *
 * A round number rather than a considered one. It is long enough that following a link
 * in a conversation somebody comes back to is expected to work, and short enough that
 * the folder does not grow for the life of the installation. Nothing depends on the
 * exact value, and zero keeps everything.
 */
constexpr int MEDIA_KEEP_DEFAULT = 180;

/**
 * What the board settings say when nothing has been written; the same figures the
 * settings page offers, which is where they are explained.
 *
 * Fetching ahead is off, and that one is the extension's own default rather than a
 * preference: a body is fetched from whoever has it, which at first is the person who
 * wrote it, so a client that fetched without being asked would tell an author who read
 * their post and when.
 */
constexpr bool BOARD_PREFETCH_DEFAULT = false;
constexpr bool BOARD_SERVE_DEFAULT = true;
constexpr int BOARD_KEEP_DEFAULT = 90;
constexpr int BOARD_CACHE_DEFAULT = 256 * 1024 * 1024;

constexpr int SECONDS_IN_A_DAY = 24 * 60 * 60;

/**
 * The refusal that says a board is not there.
 *
 * The one status code here worth acting on rather than reporting: a board that is gone
 * can be taken out of the interface, where a rate limit or a post that was too large
 * are things to tell somebody about and leave alone.
 */
constexpr int BOARD_GONE = 71;

/** Where a pasted picture is kept; see HubsView::offerPastedImage(). */
QString pasted_media_folder()
{
	return QDir::homePath() + QStringLiteral("/.quickdc/media");
}

/**
 * Where a post written here is kept.
 *
 * A post is a file and somebody has to be able to send it: until the first reader has
 * fetched it the author is its only source, so the document is written down before the
 * hub is told about it and stays there for as long as anybody may ask. Nothing here is
 * named after anything a hub chose.
 */
QString composed_post_folder()
{
	return QDir::homePath() + QStringLiteral("/.quickdc/outbox");
}

/**
 * One parameter value, escaped as the ADC message grammar requires.
 *
 * The shortest form and nothing more, because the header of a post is inside its hash:
 * a value escaped any other way is a different post, and a reading client is required
 * to refuse a document that is not in the canonical form rather than repair it. A
 * carriage return has no escape and no place in a header, so it is dropped rather than
 * written as something that would be refused.
 */
QString adc_value(const QString& text)
{
	QString escaped;
	escaped.reserve(text.size());

	for (const QChar one : text)
	{
		if (one == QLatin1Char('\\')) escaped += QStringLiteral("\\\\");
		else if (one == QLatin1Char(' ')) escaped += QStringLiteral("\\s");
		else if (one == QLatin1Char('\n')) escaped += QStringLiteral("\\n");
		else if (one != QLatin1Char('\r')) escaped += one;
	}

	return escaped;
}

/**
 * The bytes of a post: one ADC message, a newline, and then the body raw.
 *
 * The parameters are written in the order the extension lists them, none of them twice
 * and none of them empty. Those rules are not tidiness: the hash is taken over every
 * byte of this, so two clients that agree on what a post says and disagree on how to
 * write it down have made two posts.
 *
 * Only the header is ADC and the first newline is where ADC stops. The body follows
 * exactly as it was typed: it may contain newlines and whatever else its format
 * admits.
 */
QByteArray post_document(const QString& cid, const QString& parent,
                         const QString& subject, const QString& body,
                         bool richText, quint64 composed)
{
	QString header = QStringLiteral("IBB0 ID") + adc_value(cid);

	if (!parent.isEmpty()) header += QStringLiteral(" PA") + adc_value(parent);
	if (!subject.isEmpty()) header += QStringLiteral(" SJ") + adc_value(subject);

	if (composed > 0) header += QStringLiteral(" DA") + QString::number(composed);

	if (richText) header += QStringLiteral(" RT1");

	QByteArray document = header.toUtf8();
	document.append('\n');
	document.append(body.toUtf8());

	return document;
}

/** The roots drawn under one row of a board, that row included, depth first. */
QStringList roots_under(const Qdc::BoardModel* model, const QModelIndex& row)
{
	QStringList roots;

	if (row.isValid())
	{
		const QString tth = model->tthAt(row);
		if (!tth.isEmpty()) roots.append(tth);
	}

	for (int n = 0; n < model->rowCount(row); ++n)
		roots += roots_under(model, model->index(n, 0, row));

	return roots;
}

constexpr int TREE_WIDTH = 220;
constexpr int TREE_MINIMUM = 150;

constexpr int NameColumn = 0;

/** Carries a row's kind; see RowKind and row_kind(). */
constexpr int KindRole = Qt::UserRole + 1;

/** Carries the padlock for a row, for HubRowDelegate to draw; see refreshItem(). */
constexpr int LockRole = Qt::UserRole + 2;

/**
 * What a row of the tree stands for.
 *
 * Asked wherever a row is drawn or right-clicked, because each kind carries its own
 * glyph and offers its own menu, and none of them is simply the absence of the
 * others - a board is not a conversation that happens to be with nobody.
 */
enum class RowKind {
	Hub,
	Conversation,

	/**
	 * A bulletin board on a hub.
	 *
	 * Named apart so that every site asking what a row is has to say which kind it
	 * means: a board is not a conversation with nobody in it, and taking one for a
	 * conversation would offer a person's menu for a list of posts and ask a pane
	 * that is not a chat pane how many lines nobody has read.
	 */
	Board
};

/**
 * What 'item' stands for.
 *
 * A row carrying nothing under the role is read as a conversation. Every row is given
 * its kind where it is built, so there should be none; and of the three that is the
 * one which asks nothing of the hub, so an unexpected row draws a figure and offers a
 * person's menu rather than reaching for a bridge that is not there.
 */
RowKind row_kind(const QTreeWidgetItem* item)
{
	if (!item) return RowKind::Conversation;

	const QVariant stored = item->data(NameColumn, KindRole);
	if (!stored.isValid()) return RowKind::Conversation;

	return stored.value<RowKind>();
}

/** The glyph beside a row, and the padlock after it, in logical pixels. */
constexpr int ROW_GLYPH = 15;
constexpr int LOCK_GLYPH = 12;

/** How far the padlock sits in from the end of the row. */
constexpr int LOCK_MARGIN = 6;

/** The glyph that names each kind of row. */
Qdc::Glyph kind_glyph(RowKind kind)
{
	switch (kind)
	{
		case RowKind::Hub:          return Qdc::Glyph::Hubs;
		case RowKind::Conversation: return Qdc::Glyph::People;
		case RowKind::Board:        return Qdc::Glyph::Board;
	}

	return Qdc::Glyph::People;
}

/**
 * A glyph for a tree row, in the two inks a row is ever drawn in.
 *
 * Both states are put in the one icon rather than the colour being chosen when the
 * row is built, because a row changes ink when it is selected and an item's icon is
 * not asked again. Qt requests the Selected pixmap for the row under the pill, so
 * giving it one is all it takes for the glyph to follow the words.
 */
QIcon row_icon(Qdc::Glyph glyph, int size, qreal ratio)
{
	const auto paint = [glyph, size, ratio](const QColor& ink) {
		QPixmap pixmap(QSize(size, size) * ratio);
		pixmap.setDevicePixelRatio(ratio);
		pixmap.fill(Qt::transparent);

		QPainter painter(&pixmap);
		Qdc::paint_glyph(painter, glyph, QRectF(0, 0, size, size), ink);
		painter.end();

		return pixmap;
	};

	QIcon icon;
	icon.addPixmap(paint(Qdc::theme().sidebarText), QIcon::Normal);
	icon.addPixmap(paint(Qdc::theme().sidebarTextActive), QIcon::Selected);

	return icon;
}

/**
 * Draws a tree row, with the padlock at the end of it.
 *
 * The padlock cannot be a column of its own. A cell is what the stylesheet rounds
 * into the selection pill, so a second column gives a selected row two pills with a
 * seam down the middle, which reads as two things selected rather than one - the same
 * trouble the branch indentation caused, and noted where that was cured in
 * widgets.qss.
 *
 * So one column, and the lock is drawn over the end of the row it belongs to. The
 * name is elided before it rather than left to run underneath.
 */
class HubRowDelegate final : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	void paint(QPainter* painter, const QStyleOptionViewItem& option,
	           const QModelIndex& index) const override
	{
		QStyleOptionViewItem opt = option;
		initStyleOption(&opt, index);

		const QIcon lock = qvariant_cast<QIcon>(index.data(LockRole));

		if (!lock.isNull())
		{
			const int reserve = LOCK_GLYPH + LOCK_MARGIN * 2;

			/*
			 * Elided here rather than by leaving the text less room, because the
			 * room is also what the pill is drawn to and the pill wants the whole
			 * row.
			 */
			opt.text = opt.fontMetrics.elidedText(opt.text, Qt::ElideRight,
				qMax(0, opt.rect.width() - reserve - ROW_GLYPH * 2));
		}

		/*
		 * drawControl rather than the base paint(), which would run
		 * initStyleOption again and put the full text back.
		 */
		QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
		style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

		if (lock.isNull()) return;

		const QRect box(opt.rect.right() - LOCK_MARGIN - LOCK_GLYPH,
		                opt.rect.center().y() - LOCK_GLYPH / 2,
		                LOCK_GLYPH, LOCK_GLYPH);

		lock.paint(painter, box, Qt::AlignCenter,
		           (opt.state & QStyle::State_Selected) ? QIcon::Selected
		                                                : QIcon::Normal);
	}
};

QString decorate(const QString& title, int unread)
{
	return unread > 0 ? QStringLiteral("%1 (%2)").arg(title).arg(unread) : title;
}

}

Qdc::HubsView::HubsView(CoreThread* thread, Favourites* favourites,
                        FilelistBridge* filelists, TransferBridge* transfers,
                        ConfigBridge* config, Completed* completed,
                        RecentHubs* recent, QWidget* parent)
	: View(parent)
	, m_thread(thread)
	, m_favourites(favourites)
	, m_filelists(filelists)
	, m_transfers(transfers)
	, m_config(config)
	, m_completed(completed)
	, m_recent(recent)
	, m_split(new QSplitter(Qt::Horizontal, this))
	, m_tree(new QTreeWidget(this))
	, m_stack(new QStackedWidget(this))
	, m_empty(new QWidget(this))
	, m_connect(new QPushButton(tr("Connect to a hub…"), this))
{
	m_tree->setObjectName(QStringLiteral("hubTree"));
	m_tree->setHeaderHidden(true);

	m_tree->setColumnCount(1);

	/* Draws the glyph, the name and the padlock as one row; see HubRowDelegate. */
	m_tree->setItemDelegate(new HubRowDelegate(m_tree));

	m_tree->setFrameShape(QFrame::NoFrame);
	m_tree->setMinimumWidth(TREE_MINIMUM);
	m_tree->setAccessibleName(tr("Open hubs"));
	m_tree->setSelectionMode(QAbstractItemView::SingleSelection);

	/*
	 * No disclosure arrows in front of the hubs. Conversations under a hub are
	 * opened and closed from the conversation itself and are always shown while they
	 * are open, so the arrow has nothing to do - and without it a hub row starts at
	 * the edge, which is what lets the selection be one unbroken pill.
	 */
	m_tree->setRootIsDecorated(false);

	/* Which leaves a double click as the way to fold a hub away. */
	m_tree->setExpandsOnDoubleClick(true);

	connect(m_tree, &QTreeWidget::itemSelectionChanged,
	        this, &HubsView::onTreeSelectionChanged);

	QLabel* nothing = new QLabel(tr("No hubs are open."), m_empty);
	nothing->setAlignment(Qt::AlignCenter);

	nothing->setObjectName(QStringLiteral("mutedText"));

	QPushButton* openFromEmpty = new QPushButton(tr("Connect to a hub…"), m_empty);
	connect(openFromEmpty, &QPushButton::clicked, this, &HubsView::promptForHub);

	QVBoxLayout* emptyLayout = new QVBoxLayout(m_empty);
	emptyLayout->addStretch(1);
	emptyLayout->addWidget(nothing);
	emptyLayout->addSpacing(10);
	emptyLayout->addWidget(openFromEmpty, 0, Qt::AlignHCenter);
	emptyLayout->addStretch(2);

	m_stack->addWidget(m_empty);

	connect(m_connect, &QPushButton::clicked, this, &HubsView::promptForHub);

	reloadMediaPolicy();
	reloadBoardPolicy();
	sweepPastedMedia();

	/*
	 * The change rather than the save, and the group it happened in rather than any
	 * change at all. A save is reported once the file has been written, which is a
	 * moment after the setting took effect; and re-reading every hub's policy because
	 * some unrelated store wrote a numbered list was work nobody asked for.
	 */
	if (m_config)
	{
		connect(m_config, &ConfigBridge::changed, this,
		        [this](ConfigStore store, const QString& group, const QString&) {
			if (store == ConfigStore::Application)
			{
				if (group == CHAT_MEDIA || group == DOWNLOADS_GROUP)
					reloadMediaPolicy();

				if (group == BOARDS_GROUP) reloadBoardPolicy();

				/*
				 * The switch, and the port the hub answers on, which lives with the
				 * client's own network settings because it is the same port.
				 */
				if (group == LOCAL_HUB_GROUP || group == NETWORK_GROUP)
					reloadLocalHub();

				/*
				 * Which posts are this user's own, which the identity page can change
				 * with a board open in front of it.
				 */
				if (group == USER_GROUP)
				{
					const QString cid = ownCid();

					for (const Entry* entry : m_entries)
						for (BoardPane* pane : entry->boards)
							pane->setOwnCid(cid);
				}

				return;
			}

			/* A session group is named after the hub it belongs to. */
			loadHubMediaPolicy(group);
			applyBoardPolicy(group);
		});
	}

	m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_tree, &QTreeWidget::customContextMenuRequested,
	        this, &HubsView::showHubMenu);

	/*
	 * A download finishing can change what a message looks like: a picture nobody had
	 * was a chip, and now it is the picture. Every conversation is told, because a
	 * message naming that file may be in any of them.
	 */
	if (m_completed)
	{
		connect(m_completed, &Completed::changed, this, [this] {
			for (ChatPane* pane : m_panes) pane->mediaChanged();

			/*
			 * And every board, for the same reason and a longer one: an attachment to a
			 * post may be asked for years after the post was written, so a reader who
			 * fetches one is looking at that post at the time. Left to be redrawn when
			 * the post is next opened, the picture they asked for would arrive to an
			 * empty pane.
			 */
			for (BoardPane* pane : m_boards) pane->mediaChanged();
		});
	}

	/*
	 * The star follows the store rather than only the click that set it: a hub can be
	 * unbookmarked from the list on the Home view while its conversation is open here,
	 * and a filled star on a hub that is no longer kept is simply wrong.
	 *
	 * Setting it does not report a change back, so this cannot loop.
	 */
	if (m_favourites)
	{
		connect(m_favourites, &Favourites::changed, this, [this] {
			for (Entry* entry : m_entries)
			{
				entry->pane->setFavourite(m_favourites->contains(entry->bridge->url()));

				/*
				 * And whether posts read here are answered for, which follows the
				 * same store: keeping a hub is what says it was joined rather than
				 * visited, and that is what decides it where nothing else has.
				 */
				applyBoardPolicy(entry->bridge->url());
			}
		});
	}


	QWidget* side = new QWidget(this);

	/*
	 * Named for the stylesheet: the sidebar sits a step lighter than the rail and
	 * a long way darker than the content, which is what gives the window its
	 * depth.
	 */
	side->setObjectName(QStringLiteral("secondaryNav"));
	side->setAutoFillBackground(true);
	QVBoxLayout* sideLayout = new QVBoxLayout(side);
	sideLayout->setContentsMargins(0, 0, 0, 6);
	sideLayout->setSpacing(6);
	sideLayout->addWidget(m_tree, 1);
	sideLayout->addWidget(m_connect);

	m_split->addWidget(side);
	m_split->addWidget(m_stack);
	m_split->setStretchFactor(0, 0);
	m_split->setStretchFactor(1, 1);
	m_split->setSizes({TREE_WIDTH, 800});
	m_split->setChildrenCollapsible(false);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_split);
}

Qdc::HubsView::~HubsView()
{
	/*
	 * The panes hold connections to the bridges, and a bridge's destructor blocks
	 * on the core thread. Deleting the panes first means nothing is left connected
	 * to a bridge that is going away.
	 */
	for (Entry* entry : m_entries)
	{
		delete entry->pane;
		qDeleteAll(entry->dms);
		qDeleteAll(entry->boards);
		delete entry->bridge;
		delete entry;
	}

	m_entries.clear();
}

QString Qdc::HubsView::title() const
{
	return tr("Hubs");
}

Qdc::Glyph Qdc::HubsView::glyph() const
{
	return Glyph::Hubs;
}

Qdc::HubsView::Entry* Qdc::HubsView::entryFor(const QString& url)
{
	for (Entry* entry : m_entries)
	{
		if (entry->bridge && entry->bridge->url() == url) return entry;
	}

	return nullptr;
}

Qdc::HubsView::Entry* Qdc::HubsView::entryFor(HubBridge* bridge)
{
	for (Entry* entry : m_entries)
	{
		if (entry->bridge == bridge) return entry;
	}

	return nullptr;
}

bool Qdc::HubsView::openHub(const QString& url)
{
	const QString wanted = url.trimmed();
	if (wanted.isEmpty()) return false;

	/*
	 * Compared in the normalised form, so that "example.org" and
	 * "dchub://example.org:411" are recognised as the same hub rather than opening
	 * two panes onto it. Asked before a bridge is built, because building one
	 * registers a session with the core.
	 */
	const QString normalised = HubBridge::normalisedUrl(wanted);
	if (normalised.isEmpty()) return false;

	if (Entry* existing = entryFor(normalised))
	{
		showPane(existing->pane);
		return true;
	}

	HubBridge* bridge = new HubBridge(m_thread, normalised);

	Entry* entry = new Entry;
	entry->bridge = bridge;
	entry->pane = new HubPane(bridge, m_config);

	entry->item = new QTreeWidgetItem(m_tree);
	entry->item->setData(NameColumn, KindRole, QVariant::fromValue(RowKind::Hub));
	entry->item->setExpanded(true);

	m_entries.append(entry);
	adopt(entry->pane, entry->item);

	loadHubMediaPolicy(bridge->url());

	/*
	 * Once per hub, and before anything the hub says can arrive: every entry it
	 * reports is written down through this and every descriptor is recorded, which is
	 * what a cursor and a gap are answered from later.
	 */
	bulletins()->follow(bridge);
	applyBoardPolicy(bridge->url());

	/*
	 * What was said here before, put back before the hub has a chance to say
	 * anything new. The answer arrives later and out of order with everything
	 * else, which is why each pane is found again by the conversation it names
	 * rather than captured here.
	 */
	connect(bridge, &HubBridge::historyRestored, this,
	        [this, bridge](const QString& conversation, const QVector<ChatMessage>& lines) {
		Entry* found = entryFor(bridge);
		if (!found || lines.isEmpty()) return;

		if (conversation.isEmpty())
		{
			found->pane->restore(lines.toList());
			return;
		}

		for (DmPane* dm : conversationsOf(*found))
		{
			if (dm->peerId() == conversation) { dm->restore(lines.toList()); return; }
		}
	});

	bridge->requestHistory(QString(), RESTORED_LINES);

	if (m_favourites) entry->pane->setFavourite(m_favourites->contains(bridge->url()));

	connect(entry->pane, &HubPane::favouriteToggled, this,
	        [this](const QString& hubUrl, bool favourite) {
		if (!m_favourites) return;

		if (favourite)
			m_favourites->add(hubUrl);
		else
			m_favourites->remove(hubUrl);
	});

	connect(entry->pane, &HubPane::directMessageRequested, this,
	        [this, bridge](const UserInfo& user) {
		if (Entry* found = entryFor(bridge)) showPane(openDm(*found, user));
	});

	connect(entry->pane, &HubPane::filesRequested, this,
	        [this, bridge](const UserInfo& user) {
		Entry* found = entryFor(bridge);
		if (!found) return;

		/* The same conversation, opened on its Files tab rather than its messages. */
		DmPane* pane = openDm(*found, user);
		showPane(pane);
		pane->showFiles();
	});

	connect(entry->pane, &ChatPane::closeRequested, this, [this, bridge] {
		closeHub(bridge->url());
	});

	connect(bridge, &HubBridge::chatReceived, this,
	        [this, bridge](const ChatMessage& message) {
		if (Entry* found = entryFor(bridge)) route(*found, message);
	});

	connect(bridge, &HubBridge::systemError, this,
	        [this, bridge](const QString& text) {
		if (Entry* found = entryFor(bridge)) found->pane->postNotice(text);
	});

	connect(bridge, &HubBridge::authenticationRequired, this,
	        [this, bridge](){
		if (Entry* found = entryFor(bridge)) promptForPassword(*found);
	});

	/* Straight out again: nothing here wants a search reply, only passes it on. */
	connect(bridge, &HubBridge::searchResultsReceived,
	        this, &HubsView::searchResultsReceived);

	connect(bridge, &HubBridge::boardDescribed, this,
	        [this, bridge](const BoardInfo& board) {
		if (Entry* found = entryFor(bridge)) describeBoard(*found, board);
	});

	/*
	 * The hub answering for its boards, either way, and both answers are about this
	 * connection only. Losing them cancels every subscription on it and is the only
	 * thing that says so; gaining them is a session that holds none, so what somebody
	 * had open is asked for again rather than assumed to be still arriving.
	 */
	connect(bridge, &HubBridge::bulletinSupportChanged, this,
	        [this, bridge](bool supported) {
		Entry* found = entryFor(bridge);
		if (!found) return;

		found->subscribed.clear();

		/*
		 * And what somebody had finished with, because that was about the boards this
		 * connection was carrying. Being told again what a hub has is where its boards
		 * come from, and holding a refusal from an hour ago against one of them would
		 * make a board left once a board left for good.
		 */
		found->left.clear();

		if (!supported) return;

		/*
		 * Every board this hub has said it has, and not only the ones somebody has
		 * looked at: a subscription ends with the session, and what a board is for is
		 * to be arriving before anybody opens it. The descriptors for a fresh session
		 * arrive after this, and each of those subscribes as it lands; this is for the
		 * boards already known when a hub takes the feature back and gives it again.
		 */
		for (auto at = found->boards.constBegin(); at != found->boards.constEnd(); ++at)
		{
			if (at.value() && at.value()->boardInfo().may(BoardPermission::Subscribe))
				subscribeBoard(*found, at.key());
		}
	});

	connect(bridge, &HubBridge::boardRefused, this,
	        [this, bridge](int code, const QString& message, const QString& board,
	                       const QString& tth) {
		Entry* found = entryFor(bridge);
		if (!found) return;

		/*
		 * Reported wherever it can be matched, and in every open board on the hub
		 * where it cannot. This extension has no token: a refusal names the board or
		 * the hash it refuses and nothing else, so one that names neither is still
		 * the hub declining to do what somebody asked, and swallowing it would leave
		 * them watching for an entry that will never arrive.
		 */
		QList<BoardPane*> told;

		if (BoardPane* named = found->boards.value(board, nullptr))
			told.append(named);
		else if (!tth.isEmpty())
			told = panesHolding(tth);

		if (told.isEmpty())
		{
			for (BoardPane* pane : found->boards) told.append(pane);
		}

		for (BoardPane* pane : told) pane->reportRefusal(code, message, tth, board);

		/* A board that is not there can be dropped from the interface, and is. */
		if (code == BOARD_GONE && !board.isEmpty()) closeBoard(*found, board);
	});

	connect(bridge, &HubBridge::hubNameChanged, this, [this, bridge] {
		if (Entry* found = entryFor(bridge))
			refreshItem(found->item, found->pane);

		/*
		 * What the hub calls itself is worth remembering, and this is the only
		 * moment it is known. Recorded as a name rather than as a visit, so the row
		 * does not report having been visited whenever a hub gets round to sending
		 * its topic.
		 */
		if (m_recent) m_recent->setName(bridge->url(), bridge->hubName());
	});

	/*
	 * Both of these move the padlock on the row: whether the connection is up at all,
	 * and whether it turned out to be encrypted. They arrive separately - the socket
	 * is connected before anything is known about its encryption - so the row is
	 * rebuilt on either.
	 */
	const auto followConnection = [this, bridge] {
		if (Entry* found = entryFor(bridge))
			refreshItem(found->item, found->pane);
	};

	connect(bridge, &HubBridge::netStatusChanged, this, followConnection);
	connect(bridge, &HubBridge::secureChanged, this, followConnection);

	/* The row's label is the hub's name once it has one, and the URL until then. */
	refreshItem(entry->item, entry->pane);

	/*
	 * Recorded as it is opened rather than once the login succeeds: a hub that
	 * refused the connection is one somebody may well want to find again to see why,
	 * and by the time it has refused there is nothing left to record it from.
	 */
	if (m_recent) m_recent->note(bridge->url());

	showPane(entry->pane);
	bridge->connectToHub();

	saveSession();

	return true;
}

QList<Qdc::DmPane*> Qdc::HubsView::conversationsOf(const Entry& entry) const
{
	QList<DmPane*> found;
	if (!entry.item) return found;

	/*
	 * Taken from the tree rather than from the map they are filed in: a QHash has
	 * no order, and the order they are listed in is the order to put them back.
	 */
	for (int n = 0; n < entry.item->childCount(); ++n)
	{
		ChatPane* pane = m_panes.value(entry.item->child(n), nullptr);
		if (DmPane* dm = qobject_cast<DmPane*>(pane)) found.append(dm);
	}

	return found;
}

QStringList Qdc::HubsView::openConversations(const QString& hubUrl) const
{
	QStringList ids;

	for (const Entry* entry : m_entries)
	{
		if (!entry->bridge || entry->bridge->url() != hubUrl) continue;

		for (const DmPane* dm : conversationsOf(*entry))
		{
			const QString id = dm->peerId();
			if (!id.isEmpty() && !ids.contains(id)) ids.append(id);
		}

		break;
	}

	return ids;
}

QStringList Qdc::HubsView::openBoards(const QString& hubUrl) const
{
	QStringList names;

	for (const Entry* entry : m_entries)
	{
		if (!entry->bridge || entry->bridge->url() != hubUrl || !entry->item) continue;

		/*
		 * From the tree rather than from the map, for the reason conversationsOf()
		 * takes them from there: a QHash has no order, and the order they are listed
		 * in is the order to put them back.
		 */
		for (int n = 0; n < entry->item->childCount(); ++n)
		{
			BoardPane* pane = m_boards.value(entry->item->child(n), nullptr);
			if (!pane || pane->board().isEmpty()) continue;

			if (!names.contains(pane->board())) names.append(pane->board());
		}

		break;
	}

	return names;
}

void Qdc::HubsView::saveSession()
{
	if (!m_config || m_restoring) return;

	const QStringList hubs = openHubs();

	/*
	 * All but the hub this program runs itself. Its row is there because the setting
	 * says so, so recording it here would have it opened again at the next start
	 * whether or not the setting still said so - and then closed a moment later, which
	 * reads as a hub that failed. What is open *on* it is still written down below,
	 * under its own address like any other hub's.
	 */
	QStringList remembered = hubs;
	if (!m_localHubUrl.isEmpty()) remembered.removeAll(m_localHubUrl);

	/*
	 * The old entries go first. A list that has grown shorter would otherwise
	 * leave the tail of the longer one behind it, and those hubs would be opened
	 * again every time for ever.
	 */
	const int had = m_config->getNumber(ConfigStore::Application, WINDOWS_GROUP,
	                                    WINDOWS_TOTAL, 0);

	for (int n = 0; n < had; ++n)
	{
		m_config->remove(ConfigStore::Application, WINDOWS_GROUP,
		                 WINDOWS_HUB + QString::number(n));
	}

	m_config->setNumber(ConfigStore::Application, WINDOWS_GROUP, WINDOWS_TOTAL,
	                    static_cast<int>(remembered.size()));

	for (int n = 0; n < remembered.size(); ++n)
	{
		m_config->setString(ConfigStore::Application, WINDOWS_GROUP,
		                    WINDOWS_HUB + QString::number(n), remembered.at(n));
	}

	m_config->save(ConfigStore::Application);

	for (const QString& hubUrl : hubs)
	{
		const QStringList dms = openConversations(hubUrl);

		const int before = m_config->getNumber(ConfigStore::Session, hubUrl,
		                                       DMS_TOTAL, 0);

		for (int n = 0; n < before; ++n)
			m_config->remove(ConfigStore::Session, hubUrl, DMS_ENTRY + QString::number(n));

		m_config->setNumber(ConfigStore::Session, hubUrl, DMS_TOTAL,
		                    static_cast<int>(dms.size()));

		for (int n = 0; n < dms.size(); ++n)
		{
			m_config->setString(ConfigStore::Session, hubUrl,
			                    DMS_ENTRY + QString::number(n), dms.at(n));
		}

		/* The boards in the same shape, and shrinking without leaving a tail either. */
		const QStringList boards = openBoards(hubUrl);

		const int listed = m_config->getNumber(ConfigStore::Session, hubUrl,
		                                       BOARDS_TOTAL, 0);

		for (int n = 0; n < listed; ++n)
		{
			m_config->remove(ConfigStore::Session, hubUrl,
			                 BOARDS_ENTRY + QString::number(n));
		}

		m_config->setNumber(ConfigStore::Session, hubUrl, BOARDS_TOTAL,
		                    static_cast<int>(boards.size()));

		for (int n = 0; n < boards.size(); ++n)
		{
			m_config->setString(ConfigStore::Session, hubUrl,
			                    BOARDS_ENTRY + QString::number(n), boards.at(n));
		}
	}

	m_config->save(ConfigStore::Session);
}

void Qdc::HubsView::restoreConversations(Entry& entry)
{
	if (!m_config || !entry.bridge) return;

	const QString hubUrl = entry.bridge->url();
	const int total = m_config->getNumber(ConfigStore::Session, hubUrl, DMS_TOTAL, 0);

	for (int n = 0; n < total; ++n)
	{
		const QString peer = m_config->getString(ConfigStore::Session, hubUrl,
			DMS_ENTRY + QString::number(n), QString());

		if (peer.isEmpty()) continue;

		/*
		 * All that was kept is who it was with. The session id they will next
		 * appear under cannot be known until they appear, so the lasting id
		 * stands in for it - and openDm() moves the conversation onto the real
		 * one the moment the hub announces them, which is what isWith() is for.
		 */
		UserInfo user;
		user.id = peer;
		user.sourceId = peer;
		user.nick = peer;

		if (DmPane* pane = openDm(entry, user)) pane->restoreOffline();
	}
}

void Qdc::HubsView::restoreBoards(Entry& entry)
{
	if (!m_config || !entry.bridge) return;

	const QString hubUrl = entry.bridge->url();
	const int total = m_config->getNumber(ConfigStore::Session, hubUrl, BOARDS_TOTAL, 0);

	for (int n = 0; n < total; ++n)
	{
		const QString board = m_config->getString(ConfigStore::Session, hubUrl,
			BOARDS_ENTRY + QString::number(n), QString());

		if (board.isEmpty()) continue;

		/*
		 * The row and what is cached behind it, and nothing said to the hub. All that
		 * was kept is that somebody had this board open, which is a good enough reason
		 * to put the row back and no reason at all to ask a hub that has not even
		 * introduced itself yet to start sending. The descriptor fills in the rest
		 * when it arrives, and opening the row is what resumes the subscription.
		 */
		openBoard(entry, board);
	}
}

bool Qdc::HubsView::restoreSession()
{
	if (!m_config) return false;

	/*
	 * The hub this program runs itself, before the hubs somebody left open and not
	 * counted among them: its row is there because the setting says so, which is not
	 * by itself a reason to bring this view forward over whatever the window is
	 * showing.
	 *
	 * Here rather than in the constructor because opening a hub builds the board
	 * bridge, which makes directories - and setBoardDirectory() is allowed to be
	 * called after this view is built and before a hub is opened.
	 */
	reloadLocalHub();

	const int total = m_config->getNumber(ConfigStore::Application, WINDOWS_GROUP,
	                                      WINDOWS_TOTAL, 0);
	if (total <= 0) return false;

	m_restoring = true;
	int opened = 0;

	for (int n = 0; n < total; ++n)
	{
		const QString url = m_config->getString(ConfigStore::Application,
			WINDOWS_GROUP, WINDOWS_HUB + QString::number(n), QString());

		if (url.isEmpty() || !openHub(url)) continue;

		opened++;

		if (Entry* entry = entryFor(HubBridge::normalisedUrl(url)))
		{
			restoreConversations(*entry);
			restoreBoards(*entry);
		}
	}

	m_restoring = false;

	/* Once, now that everything is open, rather than partly on the way through. */
	saveSession();

	return opened > 0;
}

void Qdc::HubsView::closeHub(const QString& url)
{
	Entry* entry = entryFor(url);
	if (!entry) return;

	m_entries.removeOne(entry);

	for (DmPane* dm : entry->dms)
	{
		m_stack->removeWidget(dm);
		m_panes.remove(m_items.take(dm));
	}

	/*
	 * The board rows go the same way, and they have to go here: deleting the hub's
	 * row below deletes its children with it, and a row taken out from under these
	 * maps would leave them keyed on a QTreeWidgetItem that no longer exists.
	 */
	for (BoardPane* board : entry->boards)
	{
		m_stack->removeWidget(board);
		m_boards.remove(m_boardItems.take(board));
	}

	m_stack->removeWidget(entry->pane);
	m_panes.remove(m_items.take(entry->pane));

	/* Deleting the row takes its children with it. */
	delete entry->item;

	/*
	 * Panes before the bridge, as in the destructor: the pane is connected to it
	 * and the bridge's own destructor blocks on the core thread.
	 */
	delete entry->pane;
	qDeleteAll(entry->dms);
	qDeleteAll(entry->boards);
	delete entry->bridge;
	delete entry;

	if (m_stack->count() <= 1) m_stack->setCurrentWidget(m_empty);

	refreshBadge();

	/*
	 * The transcripts are left where they are. Closing a hub says it is not
	 * wanted open, not that what was said on it never happened - and opening it
	 * again is expected to pick the conversation up.
	 */
	saveSession();
}

QStringList Qdc::HubsView::openHubs() const
{
	QStringList urls;

	for (const Entry* entry : m_entries)
	{
		if (entry->bridge) urls.append(entry->bridge->url());
	}

	return urls;
}

int Qdc::HubsView::paneCount() const
{
	return static_cast<int>(m_panes.size());
}

Qdc::ChatPane* Qdc::HubsView::currentPane() const
{
	QWidget* current = m_stack->currentWidget();
	if (!current || current == m_empty) return nullptr;

	return qobject_cast<ChatPane*>(current);
}

void Qdc::HubsView::route(Entry& entry, const ChatMessage& message)
{
	fetchMediaIn(message);

	if (!message.privateMessage)
	{
		entry.pane->post(message);
		return;
	}

	/*
	 * The context names the conversation. A message from someone with no
	 * conversation open starts one, which is the only way an incoming private
	 * message can ever be seen.
	 */
	const QString context = message.context.isEmpty() ? message.from.id
	                                                  : message.context;

	if (context.isEmpty())
	{
		entry.pane->post(message);
		return;
	}

	DmPane* pane = entry.dms.value(context, nullptr);

	if (!pane)
	{
		UserInfo user = message.from;

		/*
		 * An echo of something we sent names us as the sender, so the context is
		 * the only reliable identity for the conversation. Whoever it names is
		 * looked up among the people on the hub, since all the context carries is
		 * an id - on ADC a session id, which is no kind of title for a window.
		 */
		if (user.id != context)
		{
			user = entry.pane->users()->byId(context);

			if (user.id.isEmpty())
			{
				user = UserInfo();
				user.id = context;
				user.nick = context;
				user.sourceId = context;
			}
		}

		pane = openDm(entry, user);
	}

	if (!pane) return;

	pane->post(message);
}

Qdc::DmPane* Qdc::HubsView::dmFor(Entry& entry, const UserInfo& user) const
{
	for (DmPane* pane : entry.dms)
	{
		if (pane && pane->isWith(user)) return pane;
	}

	return nullptr;
}

Qdc::DmPane* Qdc::HubsView::openDm(Entry& entry, const UserInfo& user)
{
	const QString context = user.id;
	if (context.isEmpty()) return nullptr;

	if (DmPane* existing = entry.dms.value(context, nullptr))
	{
		/* Details may have improved since; the pane follows them. */
		existing->setUser(user);
		return existing;
	}

	/*
	 * Not filed under this id, but possibly the same person: somebody who left and
	 * came back has a new session id, and on ADC a new nickname with it. Asked of the
	 * conversations themselves, since what counts as the same person is theirs to
	 * decide - see DmPane::isThisUser().
	 *
	 * Without this, whichever arrived first - the hub announcing them or a message
	 * from them - would decide whether the conversation carried on or a second one
	 * opened beside it.
	 */
	if (DmPane* known = dmFor(entry, user))
	{
		const QString was = known->context();

		known->setUser(user);

		if (was != user.id)
		{
			entry.dms.remove(was);
			entry.dms.insert(user.id, known);
		}

		refreshItem(m_items.value(known, nullptr), known);
		return known;
	}

	DmPane* pane = new DmPane(entry.bridge, user, m_filelists);

	/*
	 * A file chosen in the browser goes into the download queue with the TTH the
	 * list carried. Without one the queue can never match a source to it, so an
	 * entry would sit there for ever - better to say so than to add it.
	 */
	connect(pane, &DmPane::downloadRequested, this,
	        [this](const QString& name, quint64 size, const QString& tth) {
		if (!m_transfers) return;

		if (tth.isEmpty())
		{
			/*
			 * The name is reduced where it is put into a sentence of ours, and nowhere
			 * else: what is queued below is the name as the file list gave it. A name
			 * carrying a right-to-left override rewrites everything after it, so
			 * interpolating one raw lets somebody else finish our own sentences - which
			 * is the half of the requirement that is not about the length of a row.
			 */
			QMessageBox::information(this, tr("Download"),
				tr("'%1' cannot be downloaded: the file list gave no hash for it, "
				   "and without one there is no way to find anyone who has it.")
					.arg(RichText::displayText(name)));
			return;
		}

		/*
		 * Into the configured download directory under its own name. The core
		 * resolves a leading "~" itself, so the default may stay in that form.
		 */
		const QString target = downloadDirectory() + QLatin1Char('/') + name;
		m_transfers->enqueue(target, tth, size);
	});

	QTreeWidgetItem* item = new QTreeWidgetItem(entry.item);
	item->setData(NameColumn, KindRole, QVariant::fromValue(RowKind::Conversation));

	entry.dms.insert(context, pane);
	adopt(pane, item);

	/*
	 * What was said to this person before, whether the conversation is being put
	 * back or opened afresh - somebody picked out of the user list today may well
	 * have been talked to last week, and starting blank would hide it.
	 */
	if (entry.bridge) entry.bridge->requestHistory(pane->peerId(), RESTORED_LINES);

	saveSession();

	/*
	 * The pane keeps its own name for the conversation and tells us when it changes;
	 * the key it is filed under here has to move with it or the next message from
	 * them would open a second conversation.
	 */
	connect(pane, &DmPane::contextChanged, this,
	        [this, &entry, pane](const QString& from, const QString& to) {
		if (from == to || to.isEmpty()) return;
		if (entry.dms.value(from, nullptr) != pane) return;

		entry.dms.remove(from);
		entry.dms.insert(to, pane);

		/* The row says who it is with, and who it is with has just changed. */
		refreshItem(m_items.value(pane, nullptr), pane);
	});

	/* By pane rather than by the key it was opened under, which may have moved. */
	connect(pane, &ChatPane::closeRequested, this, [this, &entry, pane] {
		DmPane* closing = entry.dms.take(entry.dms.key(pane));
		if (!closing) return;

		QTreeWidgetItem* row = m_items.take(closing);

		m_stack->removeWidget(closing);
		m_panes.remove(row);
		delete row;

		saveSession();
		delete closing;

		showPane(entry.pane);
		refreshBadge();
	});

	entry.item->setExpanded(true);
	refreshItem(item, pane);

	return pane;
}

void Qdc::HubsView::setBoardDirectory(const QString& directory)
{
	m_boardDirectory = directory;
}

Qdc::BoardBridge* Qdc::HubsView::bulletins()
{
	if (m_bulletins) return m_bulletins;

	m_bulletins = new BoardBridge(m_thread, m_transfers, m_boardDirectory, this);

	/*
	 * What this client is willing to hold, and a sweep of what it holds already. Here
	 * as well as when the settings change, because the bridge is built long after they
	 * were read and a bound nobody applied is no bound at all.
	 */
	m_bulletins->setCacheBounds(m_postKeepSeconds, m_postCacheBytes);
	m_bulletins->evictNow();

	/*
	 * What a board shows is what the index kept, and not what arrived: a resumed
	 * subscription replays the second it resumed from, so the same entries arrive
	 * twice and are recognised there by their hash. Reading the board back off the
	 * disk answers the same way, which is why both land here.
	 */
	const auto arrived = [this](const QString& hubKey, const QString& board,
	                            const QVector<BoardEntry>& entries) {
		for (const BoardEntry& entry : entries) keepComposed(hubKey, entry);

		BoardPane* pane = boardPaneFor(hubKey, board);
		if (!pane) return;

		pane->addEntries(entries);

		refreshBoardItem(m_boardItems.value(pane, nullptr), pane);
		refreshBadge();
	};

	connect(m_bulletins, &BoardBridge::entriesStored, this, arrived);
	connect(m_bulletins, &BoardBridge::entriesLoaded, this, arrived);

	connect(m_bulletins, &BoardBridge::boardStateReady, this,
	        [this](const QString& hubKey, const QString& board, quint64,
	               BoardBridge::View view) {
		/*
		 * A hub replays no further back than it says it will, so a cursor older than
		 * that leaves a hole nothing will close. The board is still worth reading and
		 * is still shown; saying so is what keeps it from looking complete.
		 */
		if (BoardPane* pane = boardPaneFor(hubKey, board))
			pane->setGap(view == BoardBridge::View::Gap);
	});

	/*
	 * What a board counts is the index's answer and not this side's, so the row and the
	 * rail badge follow the bridge saying it moved. That is what makes a post read in
	 * one session still read in the next: nothing here remembers it, and the count is
	 * asked for again as soon as the board has been read back off the disk.
	 */
	connect(m_bulletins, &BoardBridge::readStateChanged, this,
	        [this](const QString& hubKey, const QString& board) {
		BoardPane* pane = boardPaneFor(hubKey, board);
		if (!pane) return;

		refreshBoardItem(m_boardItems.value(pane, nullptr), pane);
		refreshBadge();
	});

	connect(m_bulletins, &BoardBridge::postReady, this,
	        [this](const QString& tth, const BoardPost& post) {
		for (BoardPane* pane : panesHolding(tth)) pane->setPost(post);
	});

	/*
	 * Not this time: nobody answered, or it was too large, or the copy that is here
	 * would not open. The entry keeps its place and can be asked for again - the copy
	 * that is missing today may be on a client that is switched off.
	 */
	connect(m_bulletins, &BoardBridge::postUnavailable, this,
	        [this](const QString& tth, const QString& reason) {
		for (BoardPane* pane : panesHolding(tth)) pane->setPostUnavailable(tth, reason);
	});

	/*
	 * And not ever: the bytes hash to the name the entry gave and are not a post
	 * document, so every copy of it anywhere is these same bytes. Nothing asking again
	 * could produce, and nothing true left to draw, so the post goes off the board.
	 */
	connect(m_bulletins, &BoardBridge::postUnreadable, this,
	        [this](const QString& tth, const QString&) {
		for (BoardPane* pane : panesHolding(tth)) pane->setPostUnverifiable(tth);
	});

	connect(m_bulletins, &BoardBridge::postStateChanged, this,
	        [this](const QString& tth, PostState state) {
		for (BoardPane* pane : panesHolding(tth)) pane->threads()->setState(tth, state);
	});

	return m_bulletins;
}

Qdc::HubsView::Entry* Qdc::HubsView::entryForBoard(BoardPane* pane)
{
	if (!pane) return nullptr;

	for (Entry* entry : m_entries)
	{
		for (BoardPane* held : entry->boards)
		{
			if (held == pane) return entry;
		}
	}

	return nullptr;
}

Qdc::BoardPane* Qdc::HubsView::boardPaneFor(const QString& hubKey,
                                            const QString& board) const
{
	for (const Entry* entry : m_entries)
	{
		if (!entry->bridge) continue;
		if (BoardBridge::hubKey(entry->bridge->url()) != hubKey) continue;

		return entry->boards.value(board, nullptr);
	}

	return nullptr;
}

QString Qdc::HubsView::ownCid() const
{
	if (!m_config) return QString();

	return m_config->getString(ConfigStore::Application, USER_GROUP,
	                           QStringLiteral("Client ID")).trimmed();
}

QString Qdc::HubsView::hubKeyOf(BoardPane* pane) const
{
	if (!pane) return QString();

	for (const Entry* entry : m_entries)
	{
		if (!entry->bridge) continue;

		for (BoardPane* held : entry->boards)
		{
			if (held == pane) return BoardBridge::hubKey(entry->bridge->url());
		}
	}

	return QString();
}

QList<Qdc::BoardPane*> Qdc::HubsView::panesHolding(const QString& tth) const
{
	QList<BoardPane*> found;
	if (tth.isEmpty()) return found;

	/*
	 * Every board that holds an entry for the hash, on any hub. A document is named by
	 * its own bytes and is the same post wherever it is indexed, so one that has been
	 * read is read on all of them at once - while the entry beside it stays each hub's
	 * own, which is why the panes are asked rather than merged.
	 */
	for (BoardPane* pane : m_boards)
	{
		if (pane->threads()->holds(tth)) found.append(pane);
	}

	return found;
}

void Qdc::HubsView::describeBoard(Entry& entry, const BoardInfo& board)
{
	if (board.name.isEmpty()) return;

	if (board.removed)
	{
		closeBoard(entry, board.name);
		return;
	}

	/*
	 * A board somebody has left stays left while this connection lasts. A hub sends a
	 * fresh descriptor whenever a field of the board changes, and the newest timestamp
	 * and the post count are fields - so on a board anybody is posting to a descriptor
	 * arrives every few seconds, and without this each one would put back the row that
	 * had just been closed.
	 */
	if (entry.left.contains(board.name)) return;

	BoardPane* pane = entry.boards.value(board.name, nullptr);
	if (!pane) pane = openBoard(entry, board.name);

	if (!pane) return;

	/*
	 * What may be done here is stated per session and may change under somebody - on
	 * registration, on promotion, on a ban - so the pane is told again each time
	 * rather than only the first time.
	 */
	pane->setBoard(board);

	/*
	 * And followed from the moment the hub says it is there, rather than when somebody
	 * opens it: a board is a thing that arrives, and one that only fills in once it has
	 * been looked at has nothing to show at the moment it is first looked at. What that
	 * costs is a replay per board at login, which the hub bounds by refusing to go
	 * further back than its own oldest timestamp - and after the first time each board
	 * resumes from the cursor it left off at, so the cost is paid once.
	 *
	 * Only where the hub says this session may. A descriptor is not sent at all for a
	 * board it may not subscribe to, so this is the belt to that braces.
	 */
	if (board.may(BoardPermission::Subscribe)) subscribeBoard(entry, board.name);

	refreshBoardItem(m_boardItems.value(pane, nullptr), pane);
}

Qdc::BoardPane* Qdc::HubsView::openBoard(Entry& entry, const QString& board)
{
	if (board.isEmpty() || !entry.item) return nullptr;

	if (BoardPane* existing = entry.boards.value(board, nullptr)) return existing;

	BoardPane* pane = new BoardPane;

	/*
	 * Named before the hub has described it, so that the row has something to say and
	 * so that the model refuses an entry belonging to another board. Everything else -
	 * the title, the description, what may be done here - is the hub's to say and is
	 * absent until it does.
	 */
	BoardInfo known;
	known.name = board;
	pane->setBoard(known);
	pane->setOwnCid(ownCid());

	QTreeWidgetItem* item = new QTreeWidgetItem(entry.item);
	item->setData(NameColumn, KindRole, QVariant::fromValue(RowKind::Board));

	entry.boards.insert(board, pane);

	m_stack->addWidget(pane);
	m_boards.insert(item, pane);
	m_boardItems.insert(pane, item);

	const QString hubKey = entry.bridge ? BoardBridge::hubKey(entry.bridge->url())
	                                    : QString();

	/*
	 * A body is fetched on somebody asking for it and never on an entry arriving. The
	 * request goes to whoever has the document, which at first is only its author - so
	 * fetching one says "I have read this" to the person who wrote it, and on a quiet
	 * board they can count the people it was said by.
	 */
	connect(pane, &BoardPane::bodyRequested, this,
	        [this, pane, hubKey](const QString& tth) {
		const BoardEntry wanted = pane->threads()->entryOf(tth);
		if (wanted.tth.isEmpty()) return;

		markRead(pane, tth);
		bulletins()->fetch(hubKey, wanted);

		/*
		 * And the rest of the thread, where this hub has been told to. Off unless it
		 * has been: the same disclosure, made for posts nobody has asked to see.
		 */
		if (!prefetchesOn(hubKey)) return;

		QVector<BoardEntry> thread;

		const QString root = pane->threads()->threadOf(tth);

		for (const QString& one : roots_under(pane->threads(),
		                                      pane->threads()->indexOf(root)))
		{
			const BoardEntry held = pane->threads()->entryOf(one);
			if (!held.tth.isEmpty() && held.tth != tth) thread.append(held);
		}

		if (!thread.isEmpty()) bulletins()->prefetchThread(hubKey, thread);
	});

	connect(pane, &BoardPane::parentRequested, this,
	        [this, pane, board](const QString& tth) {
		Entry* owner = entryForBoard(pane);
		if (!owner || !owner->bridge) return;

		/*
		 * A question and not a subscription: the hub answers with the one entry or
		 * with a refusal, and either way what this session is subscribed to is
		 * untouched.
		 */
		owner->bridge->requestBoardEntry(board, tth);
	});

	connect(pane, &BoardPane::withdrawRequested, this,
	        [this, pane, board](const QString& tth) {
		Entry* owner = entryForBoard(pane);
		if (owner && owner->bridge) owner->bridge->withdrawPost(tth, board);
	});

	connect(pane, &BoardPane::rereadRequested, this, [this, pane, board] {
		Entry* owner = entryForBoard(pane);
		if (!owner || !owner->bridge) return;

		/*
		 * From the beginning rather than from the cursor, which is the only way a hub
		 * that reordered or removed entries in violation of the extension becomes
		 * visible at all. What arrives is reconciled against what is held - an entry
		 * is recognised by its hash - so nothing is thrown away first.
		 */
		owner->bridge->subscribeBoard(board, 0);
		owner->subscribed.insert(board);
	});

	connect(pane, &BoardPane::threadSubmitted, this,
	        [this, pane](const QString& subject, const QString& body, bool richText) {
		if (Entry* owner = entryForBoard(pane))
			submitPost(*owner, pane, QString(), subject, body, richText);
	});

	connect(pane, &BoardPane::replySubmitted, this,
	        [this, pane](const QString& parent, const QString& subject,
	                     const QString& body, bool richText) {
		if (Entry* owner = entryForBoard(pane))
			submitPost(*owner, pane, parent, subject, body, richText);
	});

	connect(pane, &BoardPane::linkActivated, this, &HubsView::followLink);

	/*
	 * The same answer a conversation gets: a picture named in a body is drawn where this
	 * client already holds the file and is a chip to click otherwise. Nothing is fetched
	 * because a post mentions it - that would ask a stranger's client for a file the
	 * moment somebody read a post, which is the disclosure a board is careful about.
	 */
	pane->setMediaResolver([this](const QString& tth) {
		return localMediaFor(tth);
	});

	connect(pane, &BoardPane::attachRequested, this, [this, pane] {
		attachFile(pane);
	});

	connect(pane, &BoardPane::imagePasted, this,
	        [this, pane](const QByteArray& data, const QString& suffix) {
		offerPastedImage(pane, data, suffix);
	});

	connect(pane, &BoardPane::filesDropped, this, [this, pane](const QStringList& paths) {
		offerDroppedFiles(pane, paths);
	});

	/*
	 * Read means opened by somebody in the reading pane, which is not the same as
	 * fetched: a post whose body nobody could be found for was still read, and one
	 * fetched ahead of a reader was not. The pane says so itself, however the post came
	 * to be on the screen - a selection, or a caller asking for one by hash.
	 */
	connect(pane, &BoardPane::readingChanged, this,
	        [this, pane, hubKey](const QString& tth) {
		markRead(pane, tth);

		/*
		 * And where the document is already here, it is read straight off the disk. A
		 * post opened by its reader is where a body would be fetched anyway; the only
		 * thing being skipped is the press, and it is skipped because reading a local
		 * file says nothing to anybody.
		 */
		readHeldPost(pane, hubKey, tth);
	});

	entry.item->setExpanded(true);
	refreshBoardItem(item, pane);

	/* What is cached for this board, so it can be read before the hub says anything. */
	if (entry.bridge) bulletins()->loadEntries(hubKey, board);

	saveSession();

	return pane;
}

void Qdc::HubsView::subscribeBoard(Entry& entry, const QString& board)
{
	if (!entry.bridge || board.isEmpty()) return;
	if (entry.subscribed.contains(board)) return;

	entry.subscribed.insert(board);

	/*
	 * Through the bridge, because the cursor is on the disk that belongs to the core
	 * thread and this one may not wait for it. A hub that has not offered boards drops
	 * the command; the feature arriving is what asks again.
	 */
	bulletins()->resume(entry.bridge, board);
}

void Qdc::HubsView::showBoard(BoardPane* pane)
{
	if (!pane) return;

	m_stack->setCurrentWidget(pane);

	QTreeWidgetItem* item = m_boardItems.value(pane, nullptr);

	if (item)
	{
		/* The tree follows the stack, as it does for a conversation. */
		const QSignalBlocker blocker(m_tree);
		m_tree->setCurrentItem(item);
	}

	/*
	 * Already subscribed, from the descriptor that put the row there - opening a board
	 * is reading what has been arriving rather than the thing that asks for it. Asked
	 * for again all the same, since this is also the path a board reopened after being
	 * closed comes back along, and asking twice for one already held costs nothing.
	 */
	if (Entry* owner = entryForBoard(pane))
	{
		owner->left.remove(pane->board());
		subscribeBoard(*owner, pane->board());
	}

	refreshBoardItem(item, pane);
	refreshBadge();
}

void Qdc::HubsView::closeBoard(Entry& entry, const QString& board)
{
	BoardPane* pane = entry.boards.take(board);

	entry.subscribed.remove(board);

	if (!pane) return;

	QTreeWidgetItem* item = m_boardItems.take(pane);
	const bool showing = m_stack->currentWidget() == pane;

	m_stack->removeWidget(pane);
	m_boards.remove(item);

	delete item;
	delete pane;

	/*
	 * The documents stay. They are named by hash and were checked against it, a board
	 * being gone from a hub says nothing about them, and one of them may be the last
	 * copy anybody has.
	 */
	if (showing) showPane(entry.pane);

	saveSession();
	refreshBadge();
}

void Qdc::HubsView::markRead(BoardPane* pane, const QString& tth)
{
	if (!pane || tth.isEmpty()) return;

	const QString hubKey = hubKeyOf(pane);
	if (hubKey.isEmpty()) return;

	/*
	 * Written down rather than remembered here, because the question outlives the
	 * window: a board nobody has opened for a week has to still say so after a restart.
	 * The row is redrawn when the bridge says the count moved, and a post that was
	 * already read moves nothing.
	 */
	bulletins()->markRead(hubKey, pane->board(), tth);
}

void Qdc::HubsView::readHeldPost(BoardPane* pane, const QString& hubKey,
                                 const QString& tth)
{
	if (!pane || tth.isEmpty()) return;

	/*
	 * The bridge as it is rather than built to be asked: without one there is nothing
	 * held, and a post nobody holds is left alone here. Answered from memory, because
	 * this is reached while the pane is laying the post out.
	 */
	if (!m_bulletins || !m_bulletins->isHeld(tth)) return;

	const BoardEntry wanted = pane->threads()->entryOf(tth);
	if (wanted.tth.isEmpty()) return;

	m_bulletins->readHeld(hubKey, wanted);
}

int Qdc::HubsView::unreadOn(BoardPane* pane) const
{
	if (!pane || !m_bulletins) return 0;

	/*
	 * The bridge's own copy of what the index counted, which is answered out of memory:
	 * this is asked while a row is being painted, once per board, and nothing that
	 * draws may wait for the core thread. A count over the entries that are held is
	 * also what keeps it from drifting - a post read and later withdrawn stops being
	 * counted on either side, and one whose entry never arrived was never counted.
	 */
	return m_bulletins->unread(hubKeyOf(pane), pane->board());
}

void Qdc::HubsView::keepComposed(const QString& hubKey, const BoardEntry& entry)
{
	const QString path = m_composed.value(entry.tth);
	if (path.isEmpty() || entry.removed) return;

	m_composed.remove(entry.tth);

	/*
	 * A copy, because adopting a document takes the file it is handed into the store -
	 * and this client is still offering the original by its hash, which is what makes
	 * the post readable to whoever asks for it first.
	 *
	 * Named after the hash, which here is one this client computed itself: nothing
	 * reaches this line unless the hub named a post that was composed here, so the name
	 * is ours rather than a stranger's. A board name would not do - '..' is a legal one.
	 */
	const QString copy = QDir(composed_post_folder()).filePath(entry.tth);

	QFile::remove(copy);

	if (!QFile::copy(path, copy)) return;

	bulletins()->adopt(hubKey, entry, copy);
}

void Qdc::HubsView::submitPost(Entry& entry, BoardPane* pane, const QString& parent,
                               const QString& subject, const QString& body,
                               bool richText)
{
	if (!pane || !entry.bridge) return;

	/*
	 * The author's own client id, which the hub will compare against the one it
	 * accepted the post from. Written into the document because it is the one thing
	 * the hub must not be able to change about a post; nothing proves it, and a reader
	 * is never shown it as a proven identity.
	 */
	const QString cid = ownCid();

	if (cid.isEmpty())
	{
		QMessageBox::information(this, tr("Post"),
			tr("This post cannot be written yet: a post says who wrote it, and this "
			   "installation has no client ID until it has connected to a hub once."));
		return;
	}

	/*
	 * One line, because the subject is one parameter of one ADC message and a newline
	 * inside it is not something the format admits. Reduced before the document is
	 * built rather than after, since the hub is told the same subject and the two have
	 * to be the same words.
	 */
	const QString oneLine = subject.simplified();

	if (oneLine.isEmpty() && parent.isEmpty())
	{
		QMessageBox::information(this, tr("Post"),
			tr("A post that starts a thread needs a subject."));
		return;
	}

	const quint64 composed =
		static_cast<quint64>(QDateTime::currentSecsSinceEpoch());

	const QByteArray bytes = post_document(cid, parent, oneLine, body, richText,
	                                       composed);

	/*
	 * Measured against what the hub said it takes, before anything is written or sent.
	 * The limit is on the descriptor and the hub enforces it against the size declared
	 * at submission, so there is no reason to learn it by being refused.
	 */
	const quint64 allowed = pane->boardInfo().maxSize;

	if (allowed > 0 && static_cast<quint64>(bytes.size()) > allowed)
	{
		QMessageBox::information(this, tr("Post"),
			tr("This post is %1 bytes and this board takes at most %2, so it would be "
			   "refused. It has to be shorter.").arg(bytes.size()).arg(allowed));
		return;
	}

	const QString folder = composed_post_folder();

	if (!QDir().mkpath(folder))
	{
		QMessageBox::information(this, tr("Post"),
			tr("This post cannot be kept: '%1' cannot be created, and a post nobody "
			   "can send is not published.").arg(folder));
		return;
	}

	/*
	 * Named for when it was written, because its own name is its hash and that is not
	 * known until the last byte has been written. The copy under the hash is made when
	 * the entry comes back; see keepComposed().
	 */
	const QString path = QStringLiteral("%1/post-%2").arg(folder,
		QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")));

	QFile document(path);

	if (!document.open(QIODevice::WriteOnly))
	{
		QMessageBox::information(this, tr("Post"),
			tr("This post could not be written to '%1'.").arg(path));
		return;
	}

	if (document.write(bytes) != bytes.size())
	{
		document.close();

		QMessageBox::information(this, tr("Post"),
			tr("This post could not be written to '%1'.").arg(path));
		return;
	}

	document.close();

	if (!m_media) m_media = new MediaBridge(m_thread, m_config, this);

	const QString board = pane->board();
	const QPointer<HubBridge> hub(entry.bridge);

	/*
	 * Hashed and offered first, and submitted only once that has happened: the hash is
	 * the post's name and there is nothing to submit until it is known, and until the
	 * first reader has fetched the document its author is its only source. The
	 * connections are made for this one post and taken down with it.
	 */
	QObject* pending = new QObject(this);

	connect(m_media, &MediaBridge::attached, pending,
	        [this, hub, pending, path, board, oneLine, parent](const QString& hashed,
	                                                          const QString& tth,
	                                                          quint64 size) {
		if (hashed != path) return;

		m_composed.insert(tth, path);

		if (hub) hub->submitPost(tth, size, board, oneLine, parent);

		pending->deleteLater();
	});

	connect(m_media, &MediaBridge::attachFailed, pending,
	        [this, pending, path](const QString& hashed, const QString& reason) {
		if (hashed != path) return;

		QMessageBox::information(this, tr("Post"), reason);
		pending->deleteLater();
	});

	/*
	 * As a post and not as an attachment. A file picked for a message is remembered so
	 * that a magnet URI in it still works next year; this one must not be, because
	 * whether a post is served at all is worked out again at every start-up from what
	 * the post store holds and what the user said about this hub - and a copy left in
	 * the configuration would go on being offered underneath that.
	 */
	m_media->attach(path, MediaBridge::Offer::Post);
}

void Qdc::HubsView::setPasswordPrompt(PasswordPrompt prompt)
{
	m_passwordPrompt = std::move(prompt);
}

void Qdc::HubsView::promptForPassword(Entry& entry)
{
	if (!entry.bridge || !entry.pane) return;

	/* A second request while one is outstanding is the same question. */
	if (entry.prompting) return;

	entry.prompting = true;

	const QString hubUrl = entry.bridge->url();

	std::optional<QString> password;

	if (m_passwordPrompt)
	{
		password = m_passwordPrompt(hubUrl);
	}
	else
	{
		bool accepted = false;

		const QString typed = QInputDialog::getText(this, tr("Hub password"),
			tr("%1 wants a password.\n\n"
			   "It will be remembered for this hub.")
				.arg(RichText::displayText(hubUrl)),
			QLineEdit::Password, QString(), &accepted);

		if (accepted) password = typed;
	}

	entry.prompting = false;

	if (!password || password->isEmpty())
	{
		entry.pane->postNotice(
			tr("No password was given, so this hub will refuse the login."));
		return;
	}

	/*
	 * The core writes it to the session store itself, so the next connection to
	 * this hub will not ask. It goes in as plain text, which is why the prompt
	 * says it will be remembered.
	 */
	entry.bridge->sendPassword(*password);
	entry.pane->postNotice(tr("Password sent."));
}

void Qdc::HubsView::adopt(ChatPane* pane, QTreeWidgetItem* item)
{
	m_stack->addWidget(pane);
	m_panes.insert(item, pane);
	m_items.insert(pane, item);

	connect(pane, &ChatPane::unreadChanged, this, [this, item, pane](int) {
		refreshItem(item, pane);
		refreshBadge();
	});

	connect(pane, &ChatPane::linkActivated, this, &HubsView::followLink);

	connect(pane, &ChatPane::attachRequested, this, [this, pane] {
		attachFile(pane);
	});

	connect(pane, &ChatPane::imagePasted, this,
	        [this, pane](const QByteArray& data, const QString& suffix) {
		offerPastedImage(pane, data, suffix);
	});

	connect(pane, &ChatPane::filesDropped, this, [this, pane](const QStringList& paths) {
		offerDroppedFiles(pane, paths);
	});

	/*
	 * A picture in a message is drawn only if this client already has the file, which is
	 * something only this view can answer - it knows what was offered from here and what
	 * has finished downloading.
	 */
	pane->setMediaResolver([this](const QString& tth) {
		return localMediaFor(tth);
	});
}

Qdc::RichText::LocalMedia Qdc::HubsView::localMediaFor(const QString& tth) const
{
	RichText::LocalMedia here;

	const QString path = localFileFor(tth);
	if (path.isEmpty()) return here;

	/* The header only, so this stays cheap enough to ask while laying out a row. */
	const QSize size = QImageReader(path).size();
	if (!size.isValid()) return here;

	here.path = path;
	here.size = size;

	return here;
}

void Qdc::HubsView::tellPane(QWidget* pane, const QString& text)
{
	if (!pane || text.isEmpty()) return;

	if (ChatPane* chat = qobject_cast<ChatPane*>(pane))
	{
		chat->postNotice(text);
		return;
	}

	QMessageBox::information(this, tr("Attach a file"), text);
}

void Qdc::HubsView::attachFile(QWidget* pane)
{
	if (!pane) return;

	if (!m_media) m_media = new MediaBridge(m_thread, m_config, this);

	const QPointer<QWidget> waiting(pane);

	/* Which control asked decides both what may be chosen and what the message says. */
	Composer* composer = pane->findChild<Composer*>();
	const bool wantsMedia = composer && composer->wantsMedia();

	/*
	 * On the next turn of the event loop, and drawn by Qt rather than by the platform.
	 *
	 * Both halves of that were arrived at the hard way, and each failure looked like a
	 * button that does nothing. A chooser opened from inside the press that asked for it
	 * is dismissed again by that same press. A window modal one becomes a sheet on macOS
	 * that never appears, while its window stops taking input, which reads as a hang.
	 * And the platform's own panel is refused outright here: asked for modally it returns
	 * at once with nothing chosen, and shown without modality it leaves nothing but an
	 * empty placeholder a hundred pixels wide.
	 *
	 * Qt's chooser is a widget like any other, so it lays itself out and draws.
	 */
	QTimer::singleShot(0, this, [this, waiting, wantsMedia] {
		/*
		 * Only the formats Qt can draw are offered for a picture. A file it cannot
		 * decode would arrive as an image that never appears, which is worse than
		 * sending it as a plain attachment.
		 */
		const QString filter = wantsMedia
			? tr("Pictures (*.png *.jpg *.jpeg *.gif *.bmp *.webp)") : QString();

		const QString chosen = QFileDialog::getOpenFileName(window(),
			wantsMedia ? tr("Send a picture") : tr("Attach a file"), QString(),
			filter, nullptr, QFileDialog::DontUseNativeDialog);

		if (chosen.isEmpty() || !waiting) return;

		offerFile(waiting, chosen, wantsMedia);
	});
}

void Qdc::HubsView::offerDroppedFiles(QWidget* pane, const QStringList& paths)
{
	if (!pane || paths.isEmpty()) return;

	if (!m_media) m_media = new MediaBridge(m_thread, m_config, this);

	for (const QString& path : paths)
	{
		/*
		 * Shown if this machine can decode it, offered as a file otherwise. Asked of the
		 * file rather than of its name, because an extension is a guess and the answer
		 * decides whether the far end is told to draw something.
		 */
		const bool picture = !QImageReader::imageFormat(path).isEmpty();

		offerFile(QPointer<QWidget>(pane), path, picture);
	}
}

void Qdc::HubsView::sweepPastedMedia()
{
	if (m_mediaKeepDays <= 0) return;

	const QDir folder(pasted_media_folder());
	if (!folder.exists()) return;

	const QDateTime cutoff =
		QDateTime::currentDateTime().addDays(-m_mediaKeepDays);

	/*
	 * Only what this client wrote there, matched by the name it gives them: the folder is
	 * QuickDC's, but deleting whatever happens to be in it would be somebody else's file
	 * gone.
	 */
	const QFileInfoList kept = folder.entryInfoList({QStringLiteral("pasted-*")},
	                                                QDir::Files);

	for (const QFileInfo& one : kept)
	{
		if (one.lastModified() >= cutoff) continue;

		if (!QFile::remove(one.absoluteFilePath()))
			qWarning() << "a pasted picture could not be removed:" << one.fileName();
	}
}

void Qdc::HubsView::offerPastedImage(QWidget* pane, const QByteArray& data,
                                    const QString& suffix)
{
	if (!pane || data.isEmpty()) return;

	if (!m_media) m_media = new MediaBridge(m_thread, m_config, this);

	const QString folder = pasted_media_folder();

	if (!QDir().mkpath(folder))
	{
		tellPane(pane, tr("A pasted picture could not be kept: '%1' cannot be "
		                  "created.").arg(folder));
		return;
	}

	/*
	 * Named for when it was pasted, because the clipboard says nothing about where a
	 * picture came from. Seconds are enough to tell two apart, and a name that already
	 * exists is a picture pasted twice in the same second - which is the same picture
	 * often enough that reusing the name is the friendlier answer.
	 */
	const QString name = QStringLiteral("pasted-%1.%2")
		.arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss")),
		     suffix);

	const QString path = folder + QLatin1Char('/') + name;

	QFile kept(path);
	if (!kept.open(QIODevice::WriteOnly) || kept.write(data) != data.size())
	{
		tellPane(pane, tr("A pasted picture could not be written to '%1'.").arg(path));
		return;
	}

	kept.close();

	offerFile(QPointer<QWidget>(pane), path, true);
}

void Qdc::HubsView::offerFile(const QPointer<QWidget>& pane, const QString& path,
                              bool asMedia)
{
	if (path.isEmpty() || !m_media) return;

	/*
	 * Hashing reads the whole file, so the answer comes back later. The connections are
	 * made for this one file and taken down with it: several may be waiting at once, and
	 * each belongs to the conversation that asked.
	 */
	QObject* pending = new QObject(this);

	connect(m_media, &MediaBridge::attached, pending,
	        [pane, path, pending, asMedia](const QString& hashed, const QString& tth,
	                                       quint64 size) {
		if (hashed != path) return;

		if (pane)
		{
			RichText::Attachment file;
			file.tth = tth;
			file.name = QFileInfo(hashed).fileName();
			file.size = size;
			file.inlineMedia = asMedia;

			/*
			 * Whichever asked for it. A link to a hash is the same thing in a message
			 * and in a post, and the two panes take it the same way - what differs is
			 * only that a board warns about it, because an attachment to a post may
			 * be asked for years after it was written.
			 */
			if (ChatPane* chat = qobject_cast<ChatPane*>(pane.data()))
				chat->insertAttachment(file);
			else if (BoardPane* board = qobject_cast<BoardPane*>(pane.data()))
				board->insertAttachment(file);
		}

		pending->deleteLater();
	});

	connect(m_media, &MediaBridge::attachFailed, pending,
	        [this, pane, path, pending](const QString& hashed, const QString& reason) {
		if (hashed != path) return;

		if (pane) tellPane(pane.data(), reason);
		pending->deleteLater();
	});

	m_media->attach(path);
}

void Qdc::HubsView::followLink(const QUrl& url)
{
	/* Nothing else may be followed, whatever a message claimed. */
	if (!RichText::isLinkable(url)) return;

	/*
	 * A file somebody offered. Handled before anything else, because handing a magnet to
	 * the desktop would open whatever else on this machine claims the scheme - and the
	 * file is on the hub, not on the web.
	 */
	if (const RichText::Attachment file = RichText::readMagnet(url); file.isValid())
	{
		/*
		 * Here already, so there is nothing to fetch. Asked first because the obvious
		 * thing to click is the file one has just sent, and queueing that would ask the
		 * hub to find somebody with a file this client is itself the only source of -
		 * which never finishes and sits in the queue looking broken.
		 */
		if (const QString here = localFileFor(file.tth); !here.isEmpty())
		{
			if (!FileActions::open(here))
			{
				QMessageBox::information(this, tr("Open"),
					tr("'%1' is here but could not be opened.")
						.arg(RichText::displayText(file.name)));
			}

			return;
		}

		if (!m_transfers)
		{
			QMessageBox::information(this, tr("Download"),
				tr("'%1' cannot be downloaded: there is no transfer manager.")
					.arg(RichText::displayText(file.name)));
			return;
		}

		/*
		 * Into the download directory under the name the sender gave, which readMagnet()
		 * has already reduced to a name. The queue finds somebody who has the hash the
		 * same way it does for a file chosen from a file list.
		 */
		const QString target = downloadDirectory() + QLatin1Char('/') + file.name;
		m_transfers->enqueue(target, file.tth, file.size);
		return;
	}

	if (RichText::isHubUrl(url))
	{
		if (!openHub(url.toString()))
		{
			QMessageBox::warning(this, tr("Connect to a hub"),
				tr("'%1' is not an address I can use.")
					.arg(RichText::displayText(url.toString())));
			return;
		}

		if (Entry* entry = entryFor(url.toString())) showPane(entry->pane);
		return;
	}

	QDesktopServices::openUrl(url);
}

void Qdc::HubsView::showPane(ChatPane* pane)
{
	if (!pane) return;

	m_stack->setCurrentWidget(pane);

	/*
	 * The tree follows the stack. Blocked, because selecting a row is what
	 * normally drives this and letting it fire here would re-enter.
	 */
	QTreeWidgetItem* item = m_items.value(pane, nullptr);
	if (item)
	{
		const QSignalBlocker blocker(m_tree);
		m_tree->setCurrentItem(item);
	}

	/*
	 * Showing the pane clears its unread count through showEvent, but the tree row
	 * and the badge are ours to bring up to date.
	 */
	refreshItem(item, pane);
	refreshBadge();
}

void Qdc::HubsView::onTreeSelectionChanged()
{
	QTreeWidgetItem* item = m_tree->currentItem();
	if (!item) return;

	if (ChatPane* pane = m_panes.value(item, nullptr))
	{
		m_stack->setCurrentWidget(pane);
		refreshItem(item, pane);
		refreshBadge();
		return;
	}

	/*
	 * And a board, which is in a map of its own. Without this the row would take the
	 * selection and leave whatever was on screen before it there, which reads as a row
	 * that cannot be opened.
	 */
	if (BoardPane* board = m_boards.value(item, nullptr)) showBoard(board);
}

void Qdc::HubsView::refreshItem(QTreeWidgetItem* item, ChatPane* pane)
{
	if (!item || !pane) return;

	/*
	 * A hub's own name or somebody's nickname, in the densest list in the window, so it
	 * is reduced to something drawable first - see RichText::displayText().
	 *
	 * Reduced before it is decorated rather than after, because the count decorate()
	 * adds is our own text: a name that reaches the bound would otherwise have the
	 * "(3)" cut off it, and a stranger would get to decide whether this row admits to
	 * having anything unread. The tooltip is the same value, since hovering a doctored
	 * name must not be the way to see it undoctored - and it is wrapped, because a
	 * tooltip is drawn as markup whenever Qt guesses it might be; see
	 * RichText::literalHtml().
	 */
	const QString shown = RichText::displayText(pane->paneTitle());

	item->setText(NameColumn, decorate(shown, pane->unreadCount()));

	QFont face = item->font(NameColumn);
	face.setBold(pane->unreadCount() > 0);
	item->setFont(NameColumn, face);

	const RowKind kind = row_kind(item);
	const qreal ratio = devicePixelRatioF();

	HubBridge* bridge = kind == RowKind::Hub ? pane->hub() : nullptr;

	/*
	 * The hub this program runs itself, which is the one row in this list that is not
	 * somewhere else. Drawn as a machine rather than as the speech bubbles every other
	 * hub gets, and said in the tooltip as well, because a shape says which row and
	 * only words say why.
	 *
	 * Decided by the address rather than by anything the row carries, so that the mark
	 * follows the setting: a row cannot be left claiming to be the built-in hub after
	 * that hub has been switched off or moved to another port.
	 */
	const bool ownHub = bridge && !m_localHubUrl.isEmpty()
	                 && bridge->url() == m_localHubUrl;

	item->setIcon(NameColumn, row_icon(ownHub ? Glyph::LocalHub : kind_glyph(kind),
	                                   ROW_GLYPH, ratio));

	const QString about = ownHub ? tr("%1\nRunning on this computer").arg(shown)
	                             : shown;

	item->setToolTip(NameColumn, RichText::literalHtml(about));

	/*
	 * A padlock only while the connection is up, and it says what is true of that
	 * connection rather than what the address asked for. A hub that is not connected
	 * gets none: an open padlock there would read as "this hub is insecure" when all
	 * it means is that there is nothing to secure yet.
	 */
	const bool live = bridge
		&& (bridge->netStatus() == NetStatus::Connected
		    || bridge->netStatus() == NetStatus::TlsConnected);

	if (!live)
	{
		item->setData(NameColumn, LockRole, QVariant());
		return;
	}

	const bool secure = bridge->isSecure();

	item->setData(NameColumn, LockRole, QVariant::fromValue(
		row_icon(secure ? Glyph::Lock : Glyph::LockOpen, LOCK_GLYPH, ratio)));

	/*
	 * On the row's own tooltip, since the padlock is painted over the row rather than
	 * being an item of its own with somewhere to put one.
	 */
	item->setToolTip(NameColumn, RichText::literalHtml(secure
		? tr("%1\nEncrypted with TLS").arg(about)
		: tr("%1\nNot encrypted").arg(about)));
}

void Qdc::HubsView::changeEvent(QEvent* event)
{
	View::changeEvent(event);

	if (event->type() != QEvent::PaletteChange
	    && event->type() != QEvent::StyleChange)
	{
		return;
	}

	/* Every row, since each one carries a glyph painted in the old ink. */
	for (auto row = m_panes.cbegin(); row != m_panes.cend(); ++row)
		refreshItem(row.key(), row.value());

	for (auto row = m_boards.cbegin(); row != m_boards.cend(); ++row)
		refreshBoardItem(row.key(), row.value());
}

void Qdc::HubsView::refreshBadge()
{
	int unread = 0;
	for (ChatPane* pane : m_panes) unread += pane->unreadCount();

	/* A board counts posts nobody has opened, which is the same kind of waiting. */
	for (BoardPane* board : m_boards) unread += unreadOn(board);

	setBadgeCount(unread);
}

void Qdc::HubsView::refreshBoardItem(QTreeWidgetItem* item, BoardPane* pane)
{
	if (!item || !pane) return;

	/*
	 * The hub's title for the board where there is one, and the board's name where
	 * there is not - the name is protocol text held to a narrow character set, while
	 * the title is prose somebody else chose, so it is reduced to something drawable
	 * first and reduced before it is decorated: the count is our own text and a title
	 * that reached the bound would otherwise have it cut off.
	 */
	const QString title = RichText::displayText(pane->boardInfo().title);
	const QString shown = title.isEmpty() ? pane->board() : title;

	const int unread = unreadOn(pane);

	item->setText(NameColumn, decorate(shown, unread));
	item->setToolTip(NameColumn, RichText::literalHtml(shown));

	QFont face = item->font(NameColumn);
	face.setBold(unread > 0);
	item->setFont(NameColumn, face);

	item->setIcon(NameColumn, row_icon(kind_glyph(RowKind::Board), ROW_GLYPH,
	                                   devicePixelRatioF()));

	/* No padlock: a board is not a connection, and this row has none of its own. */
	item->setData(NameColumn, LockRole, QVariant());
}

bool Qdc::HubsView::fetchesMediaOn(const QString& hubUrl) const
{
	/* A hub that has decided for itself answers for itself; the rest follow the general
	   setting. Both are already in hand - see reloadMediaPolicy(). */
	return m_hubFetchMedia.value(hubUrl, m_fetchMedia);
}

quint64 Qdc::HubsView::mediaSizeLimit() const
{
	return m_mediaLimit;
}

void Qdc::HubsView::reloadMediaPolicy()
{
	if (!m_config) return;

	m_fetchMedia = m_config->getBool(ConfigStore::Application, CHAT_MEDIA, MEDIA_FETCH);

	const int limit = m_config->getNumber(ConfigStore::Application, CHAT_MEDIA,
	                                     MEDIA_LIMIT, MEDIA_LIMIT_DEFAULT);
	m_mediaLimit = static_cast<quint64>(qMax(0, limit));

	const QString configured = m_config->getString(ConfigStore::Application,
		DOWNLOADS_GROUP, QStringLiteral("Directory")).trimmed();

	m_downloadDir = configured.isEmpty() ? QStringLiteral("~/.quickdc/downloads")
	                                     : configured;

	m_mediaKeepDays = qMax(0, m_config->getNumber(ConfigStore::Application, CHAT_MEDIA,
	                                              MEDIA_KEEP, MEDIA_KEEP_DEFAULT));

	/* What each open hub decided, so a message needs no reading at all. */
	for (const Entry* entry : m_entries)
		if (entry->bridge) loadHubMediaPolicy(entry->bridge->url());
}

void Qdc::HubsView::loadHubMediaPolicy(const QString& url)
{
	if (!m_config || url.isEmpty()) return;

	/*
	 * Cached under the canonical form, because that is what a message carries when
	 * fetchesMediaOn() comes to look it up - ChatMessage::hubUrl is the core's own
	 * canonical URL. Stored under the address as given, the entry for a hub opened
	 * with a keyprint in its address was never found again.
	 */
	const QString hubUrl = HubBridge::settingsUrl(url);

	if (!m_config->contains(ConfigStore::Session, hubUrl, MEDIA_FETCH))
	{
		m_hubFetchMedia.remove(hubUrl);
		return;
	}

	m_hubFetchMedia.insert(hubUrl,
		m_config->getBool(ConfigStore::Session, hubUrl, MEDIA_FETCH));
}

bool Qdc::HubsView::prefetchesOn(const QString& hubUrl) const
{
	return m_hubPrefetch.value(HubBridge::settingsUrl(hubUrl), m_prefetchPosts);
}

bool Qdc::HubsView::servesOn(const QString& hubUrl) const
{
	const QString key = HubBridge::settingsUrl(hubUrl);

	if (const auto decided = m_hubServe.constFind(key); decided != m_hubServe.constEnd())
		return *decided;

	/*
	 * A hub nobody bookmarked answers for nothing until they say so on that hub.
	 * Offering a post says *I have read this* to anybody who asks for it, and the
	 * extension asks that a hub the user has not explicitly joined be left out of that
	 * - so a hub kept is taken as one joined, and a hub visited once is not.
	 */
	const bool joined = m_favourites
		&& (m_favourites->contains(hubUrl) || m_favourites->contains(key));

	if (!joined) return false;

	return m_servePosts;
}

void Qdc::HubsView::reloadBoardPolicy()
{
	if (!m_config) return;

	m_prefetchPosts = m_config->getBool(ConfigStore::Application, BOARDS_GROUP,
	                                    BOARD_PREFETCH, BOARD_PREFETCH_DEFAULT);

	m_servePosts = m_config->getBool(ConfigStore::Application, BOARDS_GROUP,
	                                 BOARD_SERVE, BOARD_SERVE_DEFAULT);

	const int days = qMax(0, m_config->getNumber(ConfigStore::Application, BOARDS_GROUP,
	                                             BOARD_KEEP, BOARD_KEEP_DEFAULT));

	const int bytes = qMax(0, m_config->getNumber(ConfigStore::Application, BOARDS_GROUP,
	                                              BOARD_CACHE, BOARD_CACHE_DEFAULT));

	m_postKeepSeconds = static_cast<quint64>(days) * SECONDS_IN_A_DAY;
	m_postCacheBytes = static_cast<quint64>(bytes);

	/*
	 * Only where there is something to apply them to. Reading these costs nothing and
	 * building the bridge makes directories, so a window that has not opened a hub
	 * stays out of the user's filesystem.
	 */
	if (!m_bulletins) return;

	m_bulletins->setCacheBounds(m_postKeepSeconds, m_postCacheBytes);

	for (const Entry* entry : m_entries)
		if (entry->bridge) applyBoardPolicy(entry->bridge->url());

	/*
	 * Chosen now and carried out as soon as nothing is reading what has to go. A
	 * document is thrown away in one place and released in another, because discarding
	 * one withdraws this client as a source for a post that may have no other copy.
	 */
	m_bulletins->evictNow();
}

QString Qdc::HubsView::localHubUrl() const
{
	const int port = m_config
		? m_config->getNumber(ConfigStore::Application, NETWORK_GROUP, NETWORK_PORT,
		                      NETWORK_PORT_DEFAULT)
		: NETWORK_PORT_DEFAULT;

	return HubBridge::normalisedUrl(QStringLiteral("adc://127.0.0.1:%1").arg(port));
}

void Qdc::HubsView::reloadLocalHub()
{
	if (!m_config) return;

	const bool enabled = m_config->getBool(ConfigStore::Application, LOCAL_HUB_GROUP,
	                                       LOCAL_HUB_ENABLED,
	                                       LOCAL_HUB_ENABLED_DEFAULT);

	const QString wanted = enabled ? localHubUrl() : QString();
	if (wanted == m_localHubUrl) return;

	/*
	 * The old row goes first even when there is a new one to open. The two are the
	 * same hub answering on a different port, and a list with both in it would say
	 * this computer runs two.
	 */
	if (!m_localHubUrl.isEmpty())
	{
		const QString had = m_localHubUrl;

		/* Cleared first, so that what closeHub() writes down is already the truth. */
		m_localHubUrl.clear();
		closeHub(had);
	}

	if (wanted.isEmpty()) return;

	/*
	 * And set before the hub is opened, because opening one draws its row - and a row
	 * is marked by comparing against this, so a row drawn before it was set would be
	 * drawn as somebody else's hub.
	 */
	m_localHubUrl = wanted;

	if (!openHub(wanted))
	{
		m_localHubUrl.clear();
		return;
	}

	/*
	 * What was open on it last time. Done here rather than by restoreSession(), which
	 * does this for the hubs it opens and does not open this one.
	 */
	if (Entry* entry = entryFor(wanted))
	{
		restoreConversations(*entry);
		restoreBoards(*entry);
	}
}

void Qdc::HubsView::applyBoardPolicy(const QString& hubUrl)
{
	if (!m_config || hubUrl.isEmpty()) return;

	const QString key = HubBridge::settingsUrl(hubUrl);

	/*
	 * An absent value is how a hub says it has no opinion, which is not the same as
	 * having decided against - so the entry is removed rather than written false.
	 */
	if (m_config->contains(ConfigStore::Session, key, BOARD_PREFETCH))
	{
		m_hubPrefetch.insert(key,
			m_config->getBool(ConfigStore::Session, key, BOARD_PREFETCH));
	}
	else
	{
		m_hubPrefetch.remove(key);
	}

	if (m_config->contains(ConfigStore::Session, key, BOARD_SERVE))
	{
		m_hubServe.insert(key,
			m_config->getBool(ConfigStore::Session, key, BOARD_SERVE));
	}
	else
	{
		m_hubServe.remove(key);
	}

	if (!m_bulletins) return;

	m_bulletins->setPrefetch(key, prefetchesOn(key));
	m_bulletins->setServing(key, servesOn(key));
}

void Qdc::HubsView::showHubMenu(const QPoint& at)
{
	QTreeWidgetItem* item = m_tree->itemAt(at);
	if (!item) return;

	const RowKind kind = row_kind(item);

	/* A conversation is about a person; only a hub row is about the hub. */
	if (kind == RowKind::Conversation)
	{
		showDmMenu(item, m_tree->viewport()->mapToGlobal(at));
		return;
	}

	/* A board is about what has been posted on it, and offers its own. */
	if (kind == RowKind::Board)
	{
		showBoardMenu(item, m_tree->viewport()->mapToGlobal(at));
		return;
	}

	if (kind != RowKind::Hub) return;

	if (!m_config) return;

	ChatPane* pane = m_panes.value(item, nullptr);
	if (!pane || !pane->hub()) return;

	const QString hubUrl = HubBridge::settingsUrl(pane->hub()->url());

	QMenu menu(this);

	addHubChoice(menu, tr("Fetch pictures on this hub"), hubUrl, MEDIA_FETCH,
	             [this, hubUrl] { loadHubMediaPolicy(hubUrl); });

	menu.addSeparator();

	/*
	 * Both of these are the same disclosure said twice - fetching a post tells whoever
	 * has it that it is being read, and answering for one tells whoever asks that it
	 * has been - and both are decided per hub, because that is the scope the
	 * disclosure has.
	 */
	addHubChoice(menu, tr("Fetch a thread ahead on this hub"), hubUrl, BOARD_PREFETCH,
	             [this, hubUrl] { applyBoardPolicy(hubUrl); });

	menu.addSeparator();

	addHubChoice(menu, tr("Offer posts read on this hub"), hubUrl, BOARD_SERVE,
	             [this, hubUrl] { applyBoardPolicy(hubUrl); });

	menu.exec(m_tree->viewport()->mapToGlobal(at));
}

void Qdc::HubsView::addHubChoice(QMenu& menu, const QString& title,
                                 const QString& hubUrl, const QString& key,
                                 const std::function<void()>& applied)
{
	if (!m_config) return;

	menu.addAction(title)->setEnabled(false);

	const bool decided = m_config->contains(ConfigStore::Session, hubUrl, key);
	const bool chosen = decided && m_config->getBool(ConfigStore::Session, hubUrl, key);

	QActionGroup* choices = new QActionGroup(&menu);

	const auto choice = [&](const QString& text, bool ticked, bool answer,
	                        bool opinion) {
		QAction* action = menu.addAction(text);
		action->setCheckable(true);
		action->setChecked(ticked);
		choices->addAction(action);

		connect(action, &QAction::triggered, this, [this, hubUrl, key, answer, opinion,
		                                            applied] {
			/*
			 * Nothing written is what "follow the general setting" means: an absent
			 * value is how a hub says it has no opinion, so removing it is not the
			 * same as writing false.
			 */
			if (opinion)
				m_config->setBool(ConfigStore::Session, hubUrl, key, answer);
			else
				m_config->remove(ConfigStore::Session, hubUrl, key);

			m_config->save(ConfigStore::Session);
			applied();
		});
	};

	choice(tr("Follow the general setting"), !decided, false, false);
	choice(tr("Always"), decided && chosen, true, true);
	choice(tr("Never"), decided && !chosen, false, true);
}

void Qdc::HubsView::showBoardMenu(QTreeWidgetItem* item, const QPoint& globalPos)
{
	BoardPane* pane = m_boards.value(item, nullptr);
	if (!pane) return;

	Entry* entry = entryForBoard(pane);
	if (!entry) return;

	const QString board = pane->board();

	QMenu menu(this);

	QAction* readAll = menu.addAction(tr("Mark everything read"));
	readAll->setEnabled(unreadOn(pane) > 0);

	QAction* reread = menu.addAction(tr("Read the board again from the beginning"));

	menu.addSeparator();

	QAction* leave = menu.addAction(tr("Leave this board"));

	QAction* picked = menu.exec(globalPos);
	if (!picked) return;

	if (picked == readAll)
	{
		/*
		 * One statement about the board rather than one about each post on it, written
		 * down where it survives the window closing: see BoardBridge::markAllRead(),
		 * which is also where the price of the watermark it writes is set out.
		 */
		bulletins()->markAllRead(hubKeyOf(pane), board);
		return;
	}

	if (picked == reread)
	{
		/*
		 * The whole board rather than the part that is missing, which is the only
		 * detection a client has of a hub that reordered or removed entries: what
		 * arrives is reconciled against what is held rather than replacing it.
		 */
		if (entry->bridge)
		{
			entry->bridge->subscribeBoard(board, 0);
			entry->subscribed.insert(board);
		}

		return;
	}

	if (picked != leave) return;

	leaveBoard(entry->bridge ? entry->bridge->url() : QString(), board);
}

void Qdc::HubsView::leaveBoard(const QString& url, const QString& board)
{
	Entry* entry = entryFor(url);
	if (!entry || board.isEmpty()) return;

	/*
	 * Said to the hub as well as done here: a subscription lasts until it is cancelled,
	 * and a row taken away without a word would leave the hub sending a board nobody is
	 * reading for as long as the connection lasts.
	 */
	if (entry->bridge) entry->bridge->unsubscribeBoard(board);

	/*
	 * Remembered as left, and only here - this is somebody saying they have finished
	 * with the board, where closeBoard() is also how a board the hub has removed goes.
	 * Marking that one would mean a board taken away and put back never came back.
	 */
	entry->left.insert(board);

	closeBoard(*entry, board);
}

void Qdc::HubsView::showDmMenu(QTreeWidgetItem* item, const QPoint& globalPos)
{
	DmPane* dm = qobject_cast<DmPane*>(m_panes.value(item, nullptr));
	if (!dm) return;

	QTreeWidgetItem* above = item->parent();
	HubPane* hub = above ? qobject_cast<HubPane*>(m_panes.value(above, nullptr))
	                     : nullptr;

	const UserInfo remembered = dm->peer();

	/*
	 * A conversation outlasts the person being on the hub, so who they are now is
	 * asked of the hub's own list rather than taken from the pane.
	 */
	const UserInfo here = hub ? hub->users()->resolve(remembered) : UserInfo();

	UserMenuOptions options;

	if (here.id.isEmpty())
	{
		const QString who = RichText::displayText(remembered.nick);

		options.unavailable = who.isEmpty()
			? tr("They are no longer on this hub")
			: tr("%1 is no longer on this hub").arg(who);
	}

	const UserInfo& about = here.id.isEmpty() ? remembered : here;

	/*
	 * Talking is offered and means what it means everywhere else: opening a
	 * conversation with somebody there is already one with brings it forward.
	 */
	switch (Qdc::showUserMenu(this, about, globalPos, options))
	{
		case UserChoice::Talk:
			showPane(dm);
			break;

		case UserChoice::Browse:
			showPane(dm);
			dm->showFiles();
			break;

		case UserChoice::None:
			break;
	}
}

void Qdc::HubsView::fetchMediaIn(const ChatMessage& message)
{
	if (!message.richText || !m_transfers) return;
	if (!fetchesMediaOn(message.hubUrl)) return;

	const quint64 limit = mediaSizeLimit();

	for (const RichText::Attachment& file : RichText::attachmentsIn(message.text))
	{
		/* Only what the sender meant to be shown; a file offered is for a person to
		   decide about. */
		if (!file.inlineMedia) continue;

		/* Already here, so it is drawn as it is. */
		if (!localFileFor(file.tth).isEmpty()) continue;

		/*
		 * A size the sender did not give is a size nobody can check, so it is not
		 * fetched without asking either.
		 */
		if (file.size == 0 || (limit > 0 && file.size > limit)) continue;

		const QString target = downloadDirectory() + QLatin1Char('/') + file.name;
		m_transfers->enqueue(target, file.tth, file.size);
	}
}

QString Qdc::HubsView::localFileFor(const QString& tth) const
{
	if (tth.isEmpty()) return QString();

	QString path;

	/* Offered from here, which covers a click on one's own attachment. */
	if (m_media) path = m_media->pathFor(tth);

	if (path.isEmpty() && m_completed)
	{
		for (const CompletedDownload& done : m_completed->entries())
		{
			if (done.tth != tth) continue;

			path = done.filename;
			break;
		}
	}

	if (path.isEmpty()) return QString();

	/*
	 * Resolved before it is used: a completed download is recorded under the name it was
	 * configured with, tilde and all, and nothing in Qt expands that.
	 */
	const QString here = FileActions::resolve(path);

	/* A record is not the file: it may have been moved or deleted since. */
	return QFileInfo(here).isFile() ? here : QString();
}

QString Qdc::HubsView::downloadDirectory() const
{
	/* Read when the settings change rather than per download; see reloadMediaPolicy(). */
	return m_downloadDir.isEmpty() ? QStringLiteral("~/.quickdc/downloads")
	                               : m_downloadDir;
}

void Qdc::HubsView::promptForHub()
{
	bool accepted = false;

	const QString url = QInputDialog::getText(this, tr("Connect to a hub"),
		tr("Hub address:"), QLineEdit::Normal,
		QStringLiteral("dchub://"), &accepted);

	if (!accepted) return;

	if (!openHub(url))
	{
		/*
		 * Only an unusable address gets here. Everything that can go wrong later -
		 * no such host, refused, a hub that will not have us - is reported by the
		 * bridge into the pane, where there is somewhere to put it.
		 */
		QMessageBox::warning(this, tr("Connect to a hub"),
			tr("'%1' is not an address I can use.")
				.arg(RichText::displayText(url.trimmed())));
	}
}

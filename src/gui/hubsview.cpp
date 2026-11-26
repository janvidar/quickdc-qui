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

namespace S = Qdc::Settings;

/*
 * The groups this view follows, and the keys a hub may answer for itself under the
 * same names in its own group of the session store - which is how the per hub choices
 * in the tree's menu are written. Read once and kept in hand, because a post or a
 * picture arriving is no time to be reading a preference file.
 */
const QString MEDIA_FETCH = S::keyOf(S::ChatMediaFetch);
const QString DOWNLOADS_GROUP = S::groupOf(S::DownloadDirectory);
const QString BOARD_PREFETCH = S::keyOf(S::BoardPrefetch);
const QString BOARD_SERVE = S::keyOf(S::BoardServe);
const QString USER_GROUP = S::groupOf(S::ClientId);

/*
 * The local hub answers on the port this client already listens on, so its address
 * is read from the network settings rather than from a setting of the hub's own.
 */

/**
 * What was open last time, so that it can be opened again.
 *
 * The hubs go in the application's own settings as a numbered list, which is the
 * shape [Favourites] and [Shares] already use so a hand-edited file stays
 * predictable. The conversations on a hub go in the session store under that
 * hub's address, beside the other things remembered per hub.
 */
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
                        QWidget* parent)
	: View(parent)
	, m_thread(thread)
	, m_favourites(favourites)
	, m_filelists(filelists)
	, m_transfers(transfers)
	, m_config(config)
	, m_completed(completed)
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

	/*
	 * Built here rather than when something is first attached: it reads back what
	 * this client already offers, and that is what tells a picture in an arriving
	 * message that the file is already on this disk.
	 */
	m_media = new MediaBridge(m_thread, m_config, this);

	reloadDownloadDirectory();

	/*
	 * The change rather than the save, and the group it happened in rather than any
	 * change at all. A save is reported once the file has been written, which is a
	 * moment after the setting took effect; and re-reading every hub's policy because
	 * some unrelated store wrote a numbered list was work nobody asked for.
	 */
	m_localHub = new LocalHubWatch(m_thread, this);
	connect(m_localHub, &LocalHubWatch::changed, this, &HubsView::syncLocalJoin);

	if (m_config)
	{
		connect(m_config, &ConfigBridge::changed, this,
		        [this](ConfigStore store, const QString& group, const QString&) {
			if (store != ConfigStore::Application) return;

			if (group == DOWNLOADS_GROUP) reloadDownloadDirectory();


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
				entry->pane->setFavourite(m_favourites->contains(entry->bridge->url()));
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
	/* The hubs this closes are closing with the window, so the core keeps them for next time. */
	HubBridge::endSession(m_thread);

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

bool Qdc::HubsView::openHub(const QString& url, bool remember)
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

	addHub(new HubBridge(m_thread, normalised, nullptr, remember), true);
	saveSession();
	return true;
}

void Qdc::HubsView::addHub(HubBridge* bridge, bool connectNow)
{
	Entry* entry = new Entry;
	entry->bridge = bridge;
	entry->pane = new HubPane(bridge, m_config);

	entry->item = new QTreeWidgetItem(m_tree);
	entry->item->setData(NameColumn, KindRole, QVariant::fromValue(RowKind::Hub));
	entry->item->setExpanded(true);

	m_entries.append(entry);
	adopt(entry->pane, entry->item);

	/* Before anything the hub says can arrive, so no board it describes is missed. */
	bulletins();

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

	connect(bridge, &HubBridge::chatMediaReady, this,
	        [this](const QString& tth, const QString& path) {
		if (m_mediaHere.value(tth) == path) return;

		m_mediaHere.insert(tth, path);
		for (ChatPane* pane : std::as_const(m_panes)) pane->mediaChanged();
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

	connect(bridge, &HubBridge::hubNameChanged, this, [this, bridge] {
		if (Entry* found = entryFor(bridge))
			refreshItem(found->item, found->pane);
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

	showPane(entry->pane);
	if (connectNow) bridge->connectToHub();
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

	/* Which hubs are open the core keeps itself; what is kept here is the panes. */
	const QStringList hubs = openHubs();

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

	syncLocalJoin();

	const QStringList urls = HubBridge::sessionHubs(m_thread);
	if (urls.isEmpty()) return false;

	m_restoring = true;
	int opened = 0;

	for (const QString& url : urls)
	{
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

Qdc::BoardBridge* Qdc::HubsView::bulletins()
{
	if (m_bulletins) return m_bulletins;

	m_bulletins = new BoardBridge(m_thread, this);

	/*
	 * What a board shows is what the index kept, and not what arrived: a resumed
	 * subscription replays the second it resumed from, so the same entries arrive
	 * twice and are recognised there by their hash. Reading the board back off the
	 * disk answers the same way, which is why both land here.
	 */
	const auto arrived = [this](const QString& hubKey, const QString& board,
	                            const QVector<BoardEntry>& entries) {
		BoardPane* pane = boardPaneFor(hubKey, board);
		if (!pane) return;

		pane->addEntries(entries);

		refreshBoardItem(m_boardItems.value(pane, nullptr), pane);
		refreshBadge();
	};

	connect(m_bulletins, &BoardBridge::entriesStored, this, arrived);
	connect(m_bulletins, &BoardBridge::entriesLoaded, this, arrived);

	connect(m_bulletins, &BoardBridge::boardRefused, this,
	        [this](const QString& hubKey, int code, const QString& message, const QString& board,
	               const QString& tth) {
		Entry* found = nullptr;
		for (Entry* entry : m_entries)
			if (entry->bridge && BoardBridge::hubKey(entry->bridge->url()) == hubKey) found = entry;
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
	});

	connect(m_bulletins, &BoardBridge::boardStateReady, this,
	        [this](const QString& hubKey, const BoardInfo& board, bool described, quint64,
	               BoardBridge::View view, bool left) {
		for (Entry* entry : m_entries)
		{
			if (!entry->bridge || BoardBridge::hubKey(entry->bridge->url()) != hubKey) continue;
			boardState(*entry, board, described, left);
		}

		/*
		 * A hub replays no further back than it says it will, so a cursor older than
		 * that leaves a hole nothing will close. The board is still worth reading and
		 * is still shown; saying so is what keeps it from looking complete.
		 */
		if (BoardPane* pane = boardPaneFor(hubKey, board.name))
			pane->setGap(view == BoardBridge::View::Gap);
	});

	connect(m_bulletins, &BoardBridge::boardGone, this,
	        [this](const QString& hubKey, const QString& board) {
		for (Entry* entry : m_entries)
			if (entry->bridge && BoardBridge::hubKey(entry->bridge->url()) == hubKey)
				closeBoard(*entry, board);
	});

	connect(m_bulletins, &BoardBridge::postRefused, this, [this](const QString& reason) {
		QMessageBox::information(this, tr("Post"), reason);
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

	return m_config->read(S::ClientId).trimmed();
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

void Qdc::HubsView::boardState(Entry& entry, const BoardInfo& board, bool described, bool left)
{
	if (board.name.isEmpty()) return;

	/* A board somebody left on this connection is not put back by its next descriptor. */
	BoardPane* pane = entry.boards.value(board.name, nullptr);
	if (!pane && described && !left) pane = openBoard(entry, board.name);
	if (!pane) return;

	/*
	 * What may be done here is stated per session and may change under somebody - on
	 * registration, on promotion, on a ban - so the pane is told again each time
	 * rather than only the first time.
	 */
	if (described) pane->setBoard(board);

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

	/* Asked for by somebody; the core decides whether the rest of the thread comes too. */
	connect(pane, &BoardPane::bodyRequested, this,
	        [this, pane, hubKey](const QString& tth) {
		bulletins()->read(hubKey, pane->board(), tth, true);
	});

	connect(pane, &BoardPane::parentRequested, this,
	        [this, pane, board](const QString& tth) {
		/*
		 * A question and not a subscription: the hub answers with the one entry or
		 * with a refusal, and either way what this session is subscribed to is
		 * untouched.
		 */
		bulletins()->requestEntry(hubKeyOf(pane), board, tth);
	});

	connect(pane, &BoardPane::withdrawRequested, this,
	        [this, pane, board](const QString& tth) {
		bulletins()->withdraw(hubKeyOf(pane), board, tth);
	});

	connect(pane, &BoardPane::rereadRequested, this, [this, pane, board] {
		/* What arrives is reconciled against what is held, so nothing is thrown away first. */
		bulletins()->subscribe(hubKeyOf(pane), board, true);
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
	 * fetched, however the post came to be on the screen. A document already here is
	 * shown straight off the disk, which tells nobody anything.
	 */
	connect(pane, &BoardPane::readingChanged, this,
	        [this, pane, hubKey](const QString& tth) {
		bulletins()->read(hubKey, pane->board(), tth, false);
	});

	entry.item->setExpanded(true);
	refreshBoardItem(item, pane);

	/* What is cached for this board, so it can be read before the hub says anything. */
	if (entry.bridge) bulletins()->loadEntries(hubKey, board);

	saveSession();

	return pane;
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
	 * Followed already where the hub described it; asked all the same, because a row put
	 * back from the last session may be for a board the hub has not mentioned, and a
	 * board followed already is not asked of the hub twice.
	 */
	if (entryForBoard(pane)) bulletins()->subscribe(hubKeyOf(pane), pane->board());

	refreshBoardItem(item, pane);
	refreshBadge();
}

void Qdc::HubsView::closeBoard(Entry& entry, const QString& board)
{
	BoardPane* pane = entry.boards.take(board);
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

void Qdc::HubsView::submitPost(Entry& entry, BoardPane* pane, const QString& parent,
                               const QString& subject, const QString& body,
                               bool richText)
{
	if (!pane || !entry.bridge) return;

	bulletins()->write(BoardBridge::hubKey(entry.bridge->url()), pane->board(), parent, subject, body, richText);
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

void Qdc::HubsView::offerPastedImage(QWidget* pane, const QByteArray& data,
                                    const QString& suffix)
{
	if (!pane || data.isEmpty() || !m_media) return;

	/* The core says where it kept them before it says they are offered. */
	const QPointer<QWidget> waiting(pane);
	const quint64 request = m_media->attachPasted(data, suffix);

	QObject* pending = new QObject(this);
	connect(m_media, &MediaBridge::pasted, pending,
	        [this, waiting, request, pending](quint64 answered, const QString& path,
	                                          const QString& reason) {
		if (answered != request) return;

		pending->deleteLater();
		if (!waiting) return;

		if (path.isEmpty())
			tellPane(waiting.data(), reason);
		else
			awaitOffer(waiting, path, true);
	});
}

void Qdc::HubsView::offerFile(const QPointer<QWidget>& pane, const QString& path,
                              bool asMedia)
{
	if (path.isEmpty() || !m_media) return;

	awaitOffer(pane, path, asMedia);
	m_media->attach(path);
}

void Qdc::HubsView::awaitOffer(const QPointer<QWidget>& pane, const QString& path,
                               bool asMedia)
{
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

		/* The core fetches it into the download directory and offers it again once here. */
		if (m_media) m_media->fetch(file.tth, file.name, file.size);
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

	const bool ownHub = bridge && bridge->isLocalJoin();

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

void Qdc::HubsView::reloadDownloadDirectory()
{
	if (!m_config) return;

	const QString configured = m_config->read(S::DownloadDirectory).trimmed();
	m_downloadDir = configured.isEmpty() ? QString::fromUtf8(S::DownloadDirectory.fallback)
	                                     : configured;
}

Qdc::HubsView::Entry* Qdc::HubsView::localJoinEntry()
{
	for (Entry* entry : std::as_const(m_entries))
		if (entry->bridge && entry->bridge->isLocalJoin()) return entry;
	return nullptr;
}

void Qdc::HubsView::syncLocalJoin()
{
	const auto [handle, url] = HubBridge::localHubJoin(m_thread);

	Entry* current = localJoinEntry();
	if (current && current->bridge->handle() == handle) return;

	/* The core has already let go of it, so what closing it tells the core changes nothing. */
	if (current) closeHub(current->bridge->url());
	if (!handle) return;

	/* Somebody opened that address by hand: one row onto a hub is all there is room for. */
	if (entryFor(url))
	{
		HubBridge::forgetUnadopted(m_thread, handle);
		return;
	}

	addHub(new HubBridge(m_thread, handle, url), false);

	if (Entry* entry = entryFor(url))
	{
		restoreConversations(*entry);
		restoreBoards(*entry);
	}
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
	             [this, hubUrl](std::optional<bool> fetch) {
		if (m_media) m_media->setFetchOn(hubUrl, fetch);
	});

	menu.addSeparator();

	/*
	 * Both of these are the same disclosure said twice - fetching a post tells whoever
	 * has it that it is being read, and answering for one tells whoever asks that it
	 * has been - and both are decided per hub, because that is the scope the
	 * disclosure has.
	 */
	addHubChoice(menu, tr("Fetch a thread ahead on this hub"), hubUrl, BOARD_PREFETCH,
	             [this, hubUrl](std::optional<bool> fetch) {
		bulletins()->setPolicy(hubUrl, BoardBridge::Policy::FetchAhead, fetch);
	});

	menu.addSeparator();

	addHubChoice(menu, tr("Offer posts read on this hub"), hubUrl, BOARD_SERVE,
	             [this, hubUrl](std::optional<bool> serve) {
		bulletins()->setPolicy(hubUrl, BoardBridge::Policy::Serve, serve);
	});

	menu.exec(m_tree->viewport()->mapToGlobal(at));
}

void Qdc::HubsView::addHubChoice(QMenu& menu, const QString& title,
                                 const QString& hubUrl, const QString& key,
                                 const std::function<void(std::optional<bool>)>& decide)
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

		connect(action, &QAction::triggered, this, [answer, opinion, decide] {
			decide(opinion ? std::optional<bool>(answer) : std::nullopt);
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
		 * One statement about the board rather than one about each post on it; the
		 * core writes it as a watermark (see BoardIndex::markAllRead()).
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
		if (entry->bridge) bulletins()->subscribe(BoardBridge::hubKey(entry->bridge->url()), board, true);

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
	 * Said to the core as well as done here: a subscription lasts until it is cancelled,
	 * and a row taken away without a word would leave the hub sending a board nobody is
	 * reading for as long as the connection lasts.
	 */
	if (entry->bridge) bulletins()->leave(BoardBridge::hubKey(entry->bridge->url()), board);

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

QString Qdc::HubsView::localFileFor(const QString& tth) const
{
	if (tth.isEmpty()) return QString();

	QString path;

	/* Offered from here, which covers a click on one's own attachment, or fetched for a message. */
	if (m_media) path = m_media->pathFor(tth);
	if (path.isEmpty()) path = m_mediaHere.value(tth);

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
	/* Read when the settings change rather than per download; see reloadDownloadDirectory(). */
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

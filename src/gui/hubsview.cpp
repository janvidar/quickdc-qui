/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "hubsview.h"

#include "hubheader.h"
#include "hubpane.h"
#include "usermodel.h"

#include <core/corethread.h>
#include <core/favourites.h>
#include <core/transferbridge.h>
#include <core/hubbridge.h>

#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int TREE_WIDTH = 220;
constexpr int TREE_MINIMUM = 150;

/** Marks a tree row as a hub rather than a conversation. */
constexpr int HubRole = Qt::UserRole + 1;

QString decorate(const QString& title, int unread)
{
	return unread > 0 ? QStringLiteral("%1 (%2)").arg(title).arg(unread) : title;
}

}

Qdc::HubsView::HubsView(CoreThread* thread, Favourites* favourites,
                        FilelistBridge* filelists, TransferBridge* transfers,
                        QWidget* parent)
	: View(parent)
	, m_thread(thread)
	, m_favourites(favourites)
	, m_filelists(filelists)
	, m_transfers(transfers)
	, m_split(new QSplitter(Qt::Horizontal, this))
	, m_tree(new QTreeWidget(this))
	, m_stack(new QStackedWidget(this))
	, m_empty(new QWidget(this))
	, m_connect(new QPushButton(tr("Connect to a hub…"), this))
{
	m_tree->setHeaderHidden(true);
	m_tree->setColumnCount(1);
	m_tree->setFrameShape(QFrame::NoFrame);
	m_tree->setMinimumWidth(TREE_MINIMUM);
	m_tree->setAccessibleName(tr("Open hubs"));
	m_tree->setSelectionMode(QAbstractItemView::SingleSelection);

	/*
	 * Expanding on a single click as well as on the arrow, because the rows are
	 * short and hitting a disclosure triangle is fiddly.
	 */
	m_tree->setExpandsOnDoubleClick(true);

	connect(m_tree, &QTreeWidget::itemSelectionChanged,
	        this, &HubsView::onTreeSelectionChanged);

	QLabel* nothing = new QLabel(tr("No hubs are open."), m_empty);
	nothing->setAlignment(Qt::AlignCenter);

	QPalette muted = nothing->palette();
	muted.setColor(QPalette::WindowText, muted.color(QPalette::PlaceholderText));
	nothing->setPalette(muted);

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

	QWidget* side = new QWidget(this);
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
	entry->pane = new HubPane(bridge);

	entry->item = new QTreeWidgetItem(m_tree);
	entry->item->setData(0, HubRole, true);
	entry->item->setExpanded(true);

	m_entries.append(entry);
	adopt(entry->pane, entry->item);

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
		if (Entry* found = entryFor(bridge))
		{
			found->pane->postNotice(
				tr("This hub wants a password. Set one under the hub's "
				   "entry in session.conf; asking here is not wired up yet."));
		}
	});

	connect(bridge, &HubBridge::hubNameChanged, this, [this, bridge] {
		if (Entry* found = entryFor(bridge))
			refreshItem(found->item, found->pane);
	});

	/* The row's label is the hub's name once it has one, and the URL until then. */
	refreshItem(entry->item, entry->pane);

	showPane(entry->pane);
	bridge->connectToHub();

	return true;
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
	delete entry->bridge;
	delete entry;

	if (m_stack->count() <= 1) m_stack->setCurrentWidget(m_empty);

	refreshBadge();
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
		 * the only reliable identity for the conversation.
		 */
		if (user.id != context)
		{
			user = UserInfo();
			user.id = context;
			user.nick = context;
			user.sourceId = context;
		}

		pane = openDm(entry, user);
	}

	pane->post(message);
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
			QMessageBox::information(this, tr("Download"),
				tr("'%1' cannot be downloaded: the file list gave no hash for it, "
				   "and without one there is no way to find anyone who has it.")
					.arg(name));
			return;
		}

		/*
		 * Into the configured download directory under its own name. The queue
		 * resolves a leading "~" itself.
		 */
		const QString target = QStringLiteral("~/.quickdc/downloads/") + name;
		m_transfers->enqueue(target, tth, size);
	});

	QTreeWidgetItem* item = new QTreeWidgetItem(entry.item);
	item->setData(0, HubRole, false);

	entry.dms.insert(context, pane);
	adopt(pane, item);

	connect(pane, &ChatPane::closeRequested, this, [this, &entry, context] {
		DmPane* closing = entry.dms.take(context);
		if (!closing) return;

		QTreeWidgetItem* row = m_items.take(closing);

		m_stack->removeWidget(closing);
		m_panes.remove(row);
		delete row;
		delete closing;

		showPane(entry.pane);
		refreshBadge();
	});

	entry.item->setExpanded(true);
	refreshItem(item, pane);

	return pane;
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
	}
}

void Qdc::HubsView::refreshItem(QTreeWidgetItem* item, ChatPane* pane)
{
	if (!item || !pane) return;

	const QString label = decorate(pane->paneTitle(), pane->unreadCount());

	item->setText(0, label);
	item->setToolTip(0, pane->paneTitle());

	QFont face = item->font(0);
	face.setBold(pane->unreadCount() > 0);
	item->setFont(0, face);
}

void Qdc::HubsView::refreshBadge()
{
	int unread = 0;
	for (ChatPane* pane : m_panes) unread += pane->unreadCount();

	setBadgeCount(unread);
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
			tr("'%1' is not an address I can use.").arg(url.trimmed()));
	}
}

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "hubsview.h"

#include "fileactions.h"
#include "composer.h"
#include "hubheader.h"
#include "hubpane.h"
#include "richtext.h"
#include "theme.h"
#include "usermodel.h"

#include <core/completed.h>
#include <core/corethread.h>
#include <core/configbridge.h>
#include <core/favourites.h>
#include <core/transferbridge.h>
#include <core/hubbridge.h>
#include <core/mediabridge.h>

#include <QDateTime>
#include <QDir>
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
#include <QPointer>
#include <QPushButton>
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

/** Two megabytes: enough for a photograph, small enough to arrive unnoticed. */
constexpr int MEDIA_LIMIT_DEFAULT = 2 * 1024 * 1024;

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
                        ConfigBridge* config, Completed* completed, QWidget* parent)
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

	QPalette muted = nothing->palette();
	muted.setColor(QPalette::WindowText, theme().contentMuted);
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

	reloadMediaPolicy();

	/*
	 * Either store: the general settings live in one and what a single hub decided lives
	 * in the other, and reloadMediaPolicy() picks up both.
	 */
	if (m_config)
	{
		connect(m_config, &ConfigBridge::saved,
		        this, [this](ConfigStore) { reloadMediaPolicy(); });
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

	loadHubMediaPolicy(bridge->url());

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
		if (Entry* found = entryFor(bridge)) promptForPassword(*found);
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
		 * Into the configured download directory under its own name. The core
		 * resolves a leading "~" itself, so the default may stay in that form.
		 */
		const QString target = downloadDirectory() + QLatin1Char('/') + name;
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
			   "It will be remembered for this hub.").arg(hubUrl),
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

	/*
	 * A picture in a message is drawn only if this client already has the file, which is
	 * something only this view can answer - it knows what was offered from here and what
	 * has finished downloading.
	 */
	pane->setMediaResolver([this](const QString& tth) {
		RichText::LocalMedia here;

		const QString path = localFileFor(tth);
		if (path.isEmpty()) return here;

		/* The header only, so this stays cheap enough to ask while laying out a row. */
		const QSize size = QImageReader(path).size();
		if (!size.isValid()) return here;

		here.path = path;
		here.size = size;

		return here;
	});
}

void Qdc::HubsView::attachFile(ChatPane* pane)
{
	if (!pane) return;

	if (!m_media) m_media = new MediaBridge(m_thread, m_config, this);

	const QPointer<ChatPane> waiting(pane);

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

void Qdc::HubsView::offerPastedImage(ChatPane* pane, const QByteArray& data,
                                    const QString& suffix)
{
	if (!pane || data.isEmpty()) return;

	if (!m_media) m_media = new MediaBridge(m_thread, m_config, this);

	const QString folder = QDir::homePath() + QStringLiteral("/.quickdc/media");

	if (!QDir().mkpath(folder))
	{
		pane->postNotice(tr("A pasted picture could not be kept: '%1' cannot be "
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
		pane->postNotice(tr("A pasted picture could not be written to '%1'.").arg(path));
		return;
	}

	kept.close();

	offerFile(QPointer<ChatPane>(pane), path, true);
}

void Qdc::HubsView::offerFile(const QPointer<ChatPane>& pane, const QString& path,
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

			pane->insertAttachment(file);
		}

		pending->deleteLater();
	});

	connect(m_media, &MediaBridge::attachFailed, pending,
	        [pane, path, pending](const QString& hashed, const QString& reason) {
		if (hashed != path) return;

		if (pane) pane->postNotice(reason);
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
					tr("'%1' is here but could not be opened.").arg(file.name));
			}

			return;
		}

		if (!m_transfers)
		{
			QMessageBox::information(this, tr("Download"),
				tr("'%1' cannot be downloaded: there is no transfer manager.")
					.arg(file.name));
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
				tr("'%1' is not an address I can use.").arg(url.toString()));
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
		QStringLiteral("Downloads"), QStringLiteral("Directory")).trimmed();

	m_downloadDir = configured.isEmpty() ? QStringLiteral("~/.quickdc/downloads")
	                                     : configured;

	/* What each open hub decided, so a message needs no reading at all. */
	for (const Entry* entry : m_entries)
		if (entry->bridge) loadHubMediaPolicy(entry->bridge->url());
}

void Qdc::HubsView::loadHubMediaPolicy(const QString& hubUrl)
{
	if (!m_config || hubUrl.isEmpty()) return;

	if (!m_config->contains(ConfigStore::Session, hubUrl, MEDIA_FETCH))
	{
		m_hubFetchMedia.remove(hubUrl);
		return;
	}

	m_hubFetchMedia.insert(hubUrl,
		m_config->getBool(ConfigStore::Session, hubUrl, MEDIA_FETCH));
}

void Qdc::HubsView::showHubMenu(const QPoint& at)
{
	QTreeWidgetItem* item = m_tree->itemAt(at);
	if (!item || !m_config) return;

	/* Only a hub has settings of its own; a conversation belongs to the hub above it. */
	if (!item->data(0, HubRole).toBool()) item = item->parent();
	if (!item) return;

	ChatPane* pane = m_panes.value(item, nullptr);
	if (!pane || !pane->hub()) return;

	const QString hubUrl = pane->hub()->url();

	QMenu menu(this);
	menu.addAction(tr("Fetch pictures on this hub"))->setEnabled(false);

	const bool decided = m_config->contains(ConfigStore::Session, hubUrl, MEDIA_FETCH);
	const bool fetches = decided
		&& m_config->getBool(ConfigStore::Session, hubUrl, MEDIA_FETCH);

	QActionGroup* choices = new QActionGroup(&menu);

	const auto choice = [&](const QString& text, bool chosen) {
		QAction* action = menu.addAction(text);
		action->setCheckable(true);
		action->setChecked(chosen);
		choices->addAction(action);

		return action;
	};

	QAction* general = choice(tr("Follow the general setting"), !decided);
	QAction* always = choice(tr("Always"), decided && fetches);
	QAction* never = choice(tr("Never"), decided && !fetches);

	QAction* picked = menu.exec(m_tree->viewport()->mapToGlobal(at));
	if (!picked) return;

	/*
	 * Nothing written is what "follow the general setting" means: an absent value is how
	 * a hub says it has no opinion, so removing it is not the same as writing false.
	 */
	if (picked == general)
		m_config->remove(ConfigStore::Session, hubUrl, MEDIA_FETCH);
	else if (picked == always || picked == never)
		m_config->setBool(ConfigStore::Session, hubUrl, MEDIA_FETCH, picked == always);
	else
		return;

	m_config->save(ConfigStore::Session);
	loadHubMediaPolicy(hubUrl);
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
			tr("'%1' is not an address I can use.").arg(url.trimmed()));
	}
}

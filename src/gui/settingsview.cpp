/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "settingsview.h"

#include "reachabilitypanel.h"
#include "richtext.h"
#include "theme.h"
#include "widgets.h"

#include <core/configbridge.h>
#include <core/dropbridge.h>
#include <core/networkmode.h>
#include <core/shares.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

/* The groups and keys are those listed in quickdc/core/api/config.h. */
const QString USER = QStringLiteral("User");
const QString NETWORK = QStringLiteral("Network");
const QString DOWNLOADS = QStringLiteral("Downloads");
const QString CHAT_MEDIA = QStringLiteral("Chat media");

/**
 * The bulletin board settings, matching HubsView, which is what acts on them.
 *
 * The first two are the ones a hub may answer for itself, under the same names in its
 * own group of the session store; the page says as much, because a general setting
 * that a hub can override is only half an answer to what will happen.
 */
const QString BOARDS = QStringLiteral("Boards");
const QString BOARD_PREFETCH = QStringLiteral("Fetch a thread ahead");
const QString BOARD_SERVE = QStringLiteral("Offer what I have read");
const QString BOARD_KEEP = QStringLiteral("Keep posts for days");
const QString BOARD_CACHE = QStringLiteral("Cache size");

/**
 * The hub this program can run itself.
 *
 * The group and every key are the core's own - see readConfiguration() in the local
 * hub - so that this page and the hub are reading the same thing rather than two
 * spellings of it. The hub answers on the port the Connection tab sets, so there is
 * no port of its own here: a connection is sorted onto the hub by what the peer says
 * first rather than by which socket it arrived on.
 */
const QString LOCAL_HUB = QStringLiteral("Local hub");
const QString LOCAL_HUB_ENABLED = QStringLiteral("Enabled");
const QString LOCAL_HUB_NAME = QStringLiteral("Name");
const QString LOCAL_HUB_DESCRIPTION = QStringLiteral("Description");
const QString LOCAL_HUB_USERS = QStringLiteral("Max users");
const QString LOCAL_HUB_HISTORY = QStringLiteral("Max history");
const QString LOCAL_HUB_SENDQ = QStringLiteral("Max send queue bytes");
const QString LOCAL_HUB_PER_ADDRESS = QStringLiteral("Max connections per address");
const QString LOCAL_HUB_HANDSHAKES = QStringLiteral("Max connections in handshake");
const QString LOCAL_HUB_HANDSHAKE_TIMEOUT = QStringLiteral("Handshake timeout");
const QString LOCAL_HUB_IDLE_TIMEOUT = QStringLiteral("Idle timeout");
const QString LOCAL_HUB_LINGER = QStringLiteral("Linger timeout");
const QString LOCAL_HUB_CHECK_CLAIMS = QStringLiteral("Check reachability claims");

/**
 * The core's own defaults for those, so an unset field shows what is really in force.
 *
 * Off is the one that is not merely a default: running a hub means answering
 * strangers on this machine, which is a thing to be chosen rather than arrived at.
 */
constexpr bool LOCAL_HUB_ENABLED_DEFAULT = false;
constexpr bool LOCAL_HUB_CHECK_CLAIMS_DEFAULT = true;
constexpr int LOCAL_HUB_USERS_DEFAULT = 128;
constexpr int LOCAL_HUB_HISTORY_DEFAULT = 50;
constexpr int LOCAL_HUB_SENDQ_KIB_DEFAULT = 48;
constexpr int LOCAL_HUB_PER_ADDRESS_DEFAULT = 8;
constexpr int LOCAL_HUB_HANDSHAKES_DEFAULT = 32;
constexpr int LOCAL_HUB_HANDSHAKE_TIMEOUT_DEFAULT = 30;
constexpr int LOCAL_HUB_IDLE_TIMEOUT_DEFAULT = 900;
constexpr int LOCAL_HUB_LINGER_DEFAULT = 30;

const QString LOCAL_HUB_NAME_DEFAULT = QStringLiteral("A QuickDC hub");
const QString LOCAL_HUB_DESCRIPTION_DEFAULT = QStringLiteral("My QuickDC hub");

/**
 * The one group here that the core does not read.
 *
 * It stores a key outside the list above faithfully and then ignores it, which is
 * exactly what a setting belonging to the interface wants: one configuration file
 * rather than a second one for the sake of a single key.
 */
const QString INTERFACE = QStringLiteral("Interface");

const QString THEME_KEY = QStringLiteral("Theme");

/** The limit is kept in bytes and shown in mebibytes. */
constexpr int MIB = 1024 * 1024;

/** About six months, matching HubsView. Zero keeps them for ever. */
constexpr int MEDIA_KEEP_DAYS = 180;

/**
 * What the board settings say when nothing has been written, matching HubsView.
 *
 * Fetching a thread ahead is off, and that one is not a preference this page invented:
 * a post is fetched from whoever holds it, which at first is only the person who wrote
 * it, so anything fetched without being asked for tells an author who read their post
 * and when. The bounds are generous - an index entry is a few hundred bytes and a board
 * without its entries is not a board, so only the documents are bounded at all.
 */
constexpr bool BOARD_PREFETCH_DEFAULT = false;
constexpr bool BOARD_SERVE_DEFAULT = true;
constexpr int BOARD_KEEP_DAYS = 90;
constexpr int BOARD_CACHE_MIB = 256;

/** The core's own defaults, so an unset field shows what is really in force. */
constexpr int DEFAULT_PORT = 1411;
constexpr int DEFAULT_SLOTS = 3;
constexpr int DEFAULT_MAX_DOWNLOADS = 3;
constexpr int DEFAULT_CHUNK_KIB = 1024;

const QString DEFAULT_BIND = QStringLiteral("0.0.0.0");
const QString DEFAULT_DOWNLOAD_DIR = QStringLiteral("~/.quickdc/downloads");

/**
 * The key the core reads a DROP server's name from, and a name to show as an example.
 *
 * Shown as a placeholder and not as a default, because there is no default: asking a
 * server tells it this client's address and the port it listens on, so the field is
 * empty until somebody puts a name in it.
 */
const QString REACHABILITY_SERVER = QStringLiteral("Reachability server");
const QString REACHABILITY_SERVER_EXAMPLE = QStringLiteral("drop.quickdc.org");

/**
 * The key that says how much of getting connected the client works out itself.
 *
 * The one setting on the page that is not plumbing. Everything under it - the port,
 * the address to bind, whether to ask the gateway, whether to claim active mode
 * regardless - is derived from it, and in two of the three modes those are not shown
 * at all. See Qdc::networkModeKeys() for the derivation, which is the core's own so
 * that the page and the client cannot come to different conclusions.
 *
 * Absent from a file the core has not settled yet, which reads as advanced: it is
 * the mode that imposes nothing, answering with whatever values are already there.
 */
const QString NETWORK_MODE = QStringLiteral("Mode");

/**
 * The key that makes the core claim active mode before consulting anything.
 *
 * Off by default, because a claim nothing has established is one every peer on the hub
 * acts on: the core reads this ahead of the measurement, the connection that arrived and
 * the reports of peers that could not get through, so it is the only setting on the page
 * that can make this client wrong about itself on purpose.
 */
const QString FORCE_ACTIVE = QStringLiteral("Force active mode");
constexpr bool FORCE_ACTIVE_DEFAULT = false;

/**
 * The key that has the core ask the internet gateway to forward the peer port.
 *
 * Off by default, and the reason is not caution about the gateway: finding one means
 * sending a search to the whole local segment, which tells every host on it that this
 * machine is here and what it runs. That is a disclosure whoever owns the machine
 * should choose, and a client that cannot be connected to still works - it is only
 * passive.
 */
const QString USE_UPNP = QStringLiteral("UseUPnP");
constexpr bool USE_UPNP_DEFAULT = false;

/** Chunk size is stored in bytes and shown in KiB. */
constexpr int KIB = 1024;

}

Qdc::SettingsView::SettingsView(ConfigBridge* config, Shares* shares, DropBridge* drop,
                                QWidget* parent)
	: View(parent)
	, m_config(config)
	, m_shares(shares)
	, m_drop(drop)
	, m_tabs(new QTabWidget(this))
	, m_nickname(new QLineEdit(this))
	, m_description(new QLineEdit(this))
	, m_email(new QLineEdit(this))
	, m_clientId(new QLabel(this))
	, m_mode(new QComboBox(this))
	, m_port(new QSpinBox(this))
	, m_bind(new QLineEdit(this))
	, m_address(new QLineEdit(this))
	, m_dropServer(new QLineEdit(this))
	, m_reachability(new ReachabilityPanel(this))
	, m_slots(new QSpinBox(this))
	, m_maxDownloads(new QSpinBox(this))
	, m_chunkSize(new QSpinBox(this))
	, m_downloadDir(new QLineEdit(this))
	, m_theme(new QComboBox(this))
	, m_shareList(new QListWidget(this))
	, m_removeShare(new QPushButton(tr("Remove"), this))
	, m_renameShare(new QPushButton(tr("Rename…"), this))
	, m_save(new QPushButton(tr("Save"), this))
	, m_revert(new QPushButton(tr("Revert"), this))
	, m_status(new QLabel(this))
{
	m_tabs->setDocumentMode(true);
	m_tabs->addTab(makePage(buildIdentity()), tr("Identity"));
	m_tabs->addTab(makePage(buildConnection()), tr("Connection"));
	m_tabs->addTab(makePage(buildLocalHub()), tr("Local hub"));
	m_tabs->addTab(makePage(buildTransfers()), tr("Transfers"));
	m_tabs->addTab(makePage(buildSharing()), tr("Sharing"));
	m_tabs->addTab(makePage(buildChat()), tr("Chat"));
	m_tabs->addTab(makePage(buildBoards()), tr("Boards"));
	m_tabs->addTab(makePage(buildAppearance()), tr("Appearance"));

	m_status->setContentsMargins(4, 0, 4, 0);
	m_status->setObjectName(QStringLiteral("mutedText"));

	QHBoxLayout* buttons = new QHBoxLayout;
	buttons->setContentsMargins(16, 0, 16, 12);
	buttons->addWidget(m_status, 1);
	buttons->addWidget(m_revert);
	buttons->addWidget(m_save);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(m_tabs, 1);
	layout->addLayout(buttons);

	connect(m_save, &QPushButton::clicked, this, [this] { save(); });
	connect(m_revert, &QPushButton::clicked, this, &SettingsView::load);

	clearVerdict();

	if (m_drop)
	{
		connect(m_drop, &DropBridge::verdictChanged, this, &SettingsView::showVerdict);
		connect(m_drop, &DropBridge::checkFailed, this, &SettingsView::showCheckFailed);

		/*
		 * Said only for a run that actually began, and never on the way out of the
		 * button. A request the bridge decides not to make - one already in flight,
		 * or no server named - reports nothing back, so a page that announced the
		 * asking would be left saying it was checking for the life of the window.
		 * Every run that starts ends in a verdict or a failure, and both of those
		 * clear the line.
		 */
		connect(m_drop, &DropBridge::checkStarted, this, [this](bool) {
			m_reachability->setChecking(true);
		});

		showVerdict(m_drop->verdict(false));
		showVerdict(m_drop->verdict(true));

		connect(m_reachability, &ReachabilityPanel::checkRequested,
		        m_drop, &DropBridge::recheck);
	}
	else
	{
		m_reachability->checkButton()->setEnabled(false);
	}

	if (!m_config)
	{
		m_tabs->setEnabled(false);
		m_save->setEnabled(false);
		m_revert->setEnabled(false);
		m_status->setText(tr("The configuration is not available."));
		return;
	}

	connect(m_config, &ConfigBridge::saveFailed, this, [this](ConfigStore store) {
		if (store == ConfigStore::Application)
			m_status->setText(tr("The configuration could not be written."));
	});

	load();
}

QString Qdc::SettingsView::title() const
{
	return tr("Settings");
}

Qdc::Glyph Qdc::SettingsView::glyph() const
{
	return Glyph::Settings;
}

QLabel* Qdc::SettingsView::makeNote(const QString& text)
{
	return Qdc::makeNote(text, this);
}

QWidget* Qdc::SettingsView::makePage(QWidget* page)
{
	if (QLayout* form = page->layout())
		form->setContentsMargins(16, 16, 16, 16);

	QScrollArea* scroll = new QScrollArea(this);
	scroll->setWidget(page);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);

	return scroll;
}

QWidget* Qdc::SettingsView::buildIdentity()
{
	QWidget* page = new QWidget(this);

	m_nickname->setMaxLength(63);
	m_nickname->setPlaceholderText(tr("How other users see you"));

	m_description->setMaxLength(255);
	m_email->setMaxLength(127);

	m_clientId->setTextInteractionFlags(Qt::TextSelectableByMouse);

	QFont mono = m_clientId->font();
	mono.setFamily(QStringLiteral("monospace"));
	m_clientId->setFont(mono);

	QFormLayout* form = new QFormLayout(page);
	form->addRow(tr("Nickname"), m_nickname);
	form->addRow(tr("Description"), m_description);
	form->addRow(tr("E-mail"), m_email);
	form->addRow(tr("Client ID"), m_clientId);

	form->addRow(makeNote(tr(
		"The client ID is derived from a private key and identifies this "
		"installation.")));

	connect(m_nickname, &QLineEdit::textEdited, this, &SettingsView::markModified);
	connect(m_description, &QLineEdit::textEdited, this, &SettingsView::markModified);
	connect(m_email, &QLineEdit::textEdited, this, &SettingsView::markModified);

	return page;
}

QWidget* Qdc::SettingsView::buildConnection()
{
	QWidget* page = new QWidget(this);

	/*
	 * How much to work out without asking. Three answers rather than the two the
	 * protocol has, because "active" is a thing to be rather than a thing to set:
	 * the first two both intend it, and differ only in who chooses the plumbing.
	 */
	m_mode->addItem(tr("Automatic — set this up and check it for me"),
	                networkModeToken(NetworkMode::Automatic));
	m_mode->addItem(tr("Advanced — I will choose the port and forwarding"),
	                networkModeToken(NetworkMode::Advanced));
	m_mode->addItem(tr("Passive — I only connect out"),
	                networkModeToken(NetworkMode::Passive));

	m_port->setRange(1, 65535);
	m_bind->setPlaceholderText(DEFAULT_BIND);
	m_address->setPlaceholderText(tr("Detected automatically"));

	m_dropServer->setMaxLength(255);
	m_dropServer->setPlaceholderText(REACHABILITY_SERVER_EXAMPLE);

	m_useUpnp = new QCheckBox(
		tr("Ask the router to forward this port (UPnP-IGD)"), page);
	m_useUpnp->setToolTip(tr("Search the local network for an internet gateway and ask "
	                         "it to forward the port above to this machine"));
	connect(m_useUpnp, &QCheckBox::toggled, this, &SettingsView::markModified);

	m_forceActive = new QCheckBox(
		tr("Tell hubs a peer can connect here, whatever was measured"), page);
	m_forceActive->setToolTip(tr("Announce active mode on every hub without waiting for "
	                             "a measurement or a peer to establish it"));
	connect(m_forceActive, &QCheckBox::toggled, this, &SettingsView::markModified);

	QFormLayout* form = new QFormLayout(page);
	form->addRow(tr("Connection"), m_mode);
	form->addRow(tr("Port"), m_port);
	form->addRow(tr("Bind to"), m_bind);
	form->addRow(tr("External address"), m_address);

	form->addRow(QString(), m_useUpnp);

	form->addRow(makeNote(tr(
		"Forwarding is asked for by searching the local network for a gateway, which "
		"tells every host on it that this machine is here and what it runs. A router "
		"that grants a mapping does not know whether anything above it honours one, so "
		"the check below is what settles whether it helped - and where it did not, the "
		"mapping is given up again rather than left open.")));

	form->addRow(makeNote(tr(
		"The client tag hubs see is built from the version and whether this is "
		"active or passive, and cannot be changed.")));

	form->addRow(tr("Reachability server"), m_dropServer);

	form->addRow(makeNote(tr(
		"Asking a server whether this client can be reached tells that server the "
		"address it is connecting from and the port it listens on, and has it connect "
		"back to prove the answer. That is why the field above is empty until somebody "
		"puts a name in it: while it is empty, nobody is asked anything.")));

	form->addRow(m_reachability);

	form->addRow(makeNote(tr(
		"An answer describes the network as it was at the moment it was measured. Move "
		"to another one - a different WiFi, a cable, a VPN coming up - and the address "
		"and the verdict may both be wrong until it is checked again.")));

	form->addRow(QString(), m_forceActive);

	form->addRow(makeNote(tr(
		"Turning this on tells every hub that a peer can connect here, whatever the "
		"check above concluded and whatever peers have reported. The rows above and this "
		"setting can disagree on screen, and this setting is the one the hubs are told "
		"about. It is meant for a network where the check cannot work but you know that "
		"incoming connections do arrive: a check against one server is one server's "
		"opinion, and there is no second one configured to confirm it. When the claim is "
		"wrong, every peer that acts on it by dialling this client fails to reach it and "
		"the transfer fails at their end, so what you see is a download that never "
		"starts rather than a setting to turn off.")));

	connect(m_bind, &QLineEdit::textEdited, this, &SettingsView::markModified);
	connect(m_address, &QLineEdit::textEdited, this, &SettingsView::markModified);
	connect(m_dropServer, &QLineEdit::textEdited, this, &SettingsView::markModified);
	connect(m_mode, &QComboBox::currentIndexChanged, this, &SettingsView::markModified);
	connect(m_port, &QSpinBox::valueChanged, this, &SettingsView::markModified);

	return page;
}

QWidget* Qdc::SettingsView::buildLocalHub()
{
	QWidget* page = new QWidget(this);

	m_localHub = new QCheckBox(tr("Run a hub on this computer"), page);
	m_localHub->setToolTip(tr("Answer as a hub on the port this client already "
	                          "listens on, so that other people can connect here"));
	connect(m_localHub, &QCheckBox::toggled, this, &SettingsView::markModified);

	m_localHubFields = new QWidget(page);

	m_localHubName = new QLineEdit(m_localHubFields);
	m_localHubName->setMaxLength(63);
	m_localHubName->setPlaceholderText(LOCAL_HUB_NAME_DEFAULT);
	connect(m_localHubName, &QLineEdit::textEdited, this, &SettingsView::markModified);

	m_localHubDescription = new QLineEdit(m_localHubFields);
	m_localHubDescription->setMaxLength(255);
	m_localHubDescription->setPlaceholderText(LOCAL_HUB_DESCRIPTION_DEFAULT);
	connect(m_localHubDescription, &QLineEdit::textEdited,
	        this, &SettingsView::markModified);

	m_localHubUsers = new QSpinBox(m_localHubFields);
	m_localHubUsers->setRange(1, 65536);
	m_localHubUsers->setToolTip(tr("How many people may be on the hub at once"));
	connect(m_localHubUsers, &QSpinBox::valueChanged, this, &SettingsView::markModified);

	m_localHubHistory = new QSpinBox(m_localHubFields);
	m_localHubHistory->setRange(0, 65536);
	m_localHubHistory->setSuffix(tr(" lines"));
	m_localHubHistory->setSpecialValueText(tr("keep none"));
	m_localHubHistory->setToolTip(tr("What somebody arriving is shown of what was "
	                                 "said before they got here"));
	connect(m_localHubHistory, &QSpinBox::valueChanged,
	        this, &SettingsView::markModified);

	m_localHubPerAddress = new QSpinBox(m_localHubFields);
	m_localHubPerAddress->setRange(1, 65536);
	m_localHubPerAddress->setToolTip(tr("Connections allowed from one address at a time"));
	connect(m_localHubPerAddress, &QSpinBox::valueChanged,
	        this, &SettingsView::markModified);

	m_localHubHandshakes = new QSpinBox(m_localHubFields);
	m_localHubHandshakes->setRange(1, 65536);
	m_localHubHandshakes->setToolTip(tr("Connections allowed to sit part way through "
	                                    "logging in at once"));
	connect(m_localHubHandshakes, &QSpinBox::valueChanged,
	        this, &SettingsView::markModified);

	m_localHubHandshakeTimeout = new QSpinBox(m_localHubFields);
	m_localHubHandshakeTimeout->setRange(0, 86400);
	m_localHubHandshakeTimeout->setSuffix(tr(" seconds"));
	m_localHubHandshakeTimeout->setSpecialValueText(tr("no limit"));
	m_localHubHandshakeTimeout->setToolTip(tr("How long a connection may take over "
	                                          "logging in before it is dropped"));
	connect(m_localHubHandshakeTimeout, &QSpinBox::valueChanged,
	        this, &SettingsView::markModified);

	m_localHubIdleTimeout = new QSpinBox(m_localHubFields);
	m_localHubIdleTimeout->setRange(0, 86400);
	m_localHubIdleTimeout->setSuffix(tr(" seconds"));
	m_localHubIdleTimeout->setSpecialValueText(tr("no limit"));
	m_localHubIdleTimeout->setToolTip(tr("How long somebody logged in may be silent "
	                                     "for before they are dropped"));
	connect(m_localHubIdleTimeout, &QSpinBox::valueChanged,
	        this, &SettingsView::markModified);

	m_localHubLinger = new QSpinBox(m_localHubFields);
	m_localHubLinger->setRange(0, 86400);
	m_localHubLinger->setSuffix(tr(" seconds"));
	m_localHubLinger->setSpecialValueText(tr("no limit"));
	m_localHubLinger->setToolTip(tr("How long somebody being kicked is given to read "
	                                "the reason before the connection goes anyway"));
	connect(m_localHubLinger, &QSpinBox::valueChanged, this, &SettingsView::markModified);

	m_localHubSendQueue = new QSpinBox(m_localHubFields);

	m_localHubSendQueue->setRange(4, 16 * KIB);
	m_localHubSendQueue->setSingleStep(4);
	m_localHubSendQueue->setSuffix(tr(" KiB"));
	m_localHubSendQueue->setToolTip(tr("How much may be waiting to go to one person "
	                                   "before they are skipped for anything but the "
	                                   "most important messages"));
	connect(m_localHubSendQueue, &QSpinBox::valueChanged,
	        this, &SettingsView::markModified);

	m_localHubCheckClaims = new QCheckBox(
		tr("Check that a client can be connected to"), m_localHubFields);
	m_localHubCheckClaims->setToolTip(tr("Try the address somebody says they can be "
	                                     "reached on, so that they can be told when "
	                                     "they are wrong about it"));
	connect(m_localHubCheckClaims, &QCheckBox::toggled,
	        this, &SettingsView::markModified);

	QFormLayout* form = new QFormLayout(m_localHubFields);
	form->setContentsMargins(0, 0, 0, 0);
	form->addRow(tr("Hub name"), m_localHubName);
	form->addRow(tr("Description"), m_localHubDescription);
	form->addRow(tr("Most people at once"), m_localHubUsers);
	form->addRow(tr("Show new arrivals"), m_localHubHistory);
	form->addRow(tr("From one address"), m_localHubPerAddress);
	form->addRow(tr("Logging in at once"), m_localHubHandshakes);
	form->addRow(tr("Allowed to log in for"), m_localHubHandshakeTimeout);
	form->addRow(tr("Drop after silence of"), m_localHubIdleTimeout);
	form->addRow(tr("After a kick, wait"), m_localHubLinger);
	form->addRow(tr("Queue for one person"), m_localHubSendQueue);
	form->addRow(QString(), m_localHubCheckClaims);

	connect(m_localHub, &QCheckBox::toggled, m_localHubFields, &QWidget::setEnabled);
	m_localHubFields->setEnabled(m_localHub->isChecked());

	QVBoxLayout* column = new QVBoxLayout(page);
	column->addWidget(m_localHub);
	column->addWidget(m_localHubFields);

	column->addWidget(makeNote(tr(
		"A hub here answers on the same port this client already listens on, which is "
		"set on the Connection tab, so there is no separate address to give out. "
		"Anyone who can reach that port can join, and there is no password: the hub "
		"decides who may connect by address and by how many are already on it, and by "
		"nothing else.")));

	column->addWidget(makeNote(tr(
		"Saving this page applies it straight away, without restarting. Turning the "
		"hub off disconnects whoever is on it at the time.")));

	column->addWidget(makeNote(tr(
		"Bulletin boards on a hub run here are configured in the file rather than on "
		"this page, one section per board, and a hub with none configured offers none. "
		"The Boards tab is about the boards this client reads on other people's hubs.")));

	column->addStretch(1);

	return page;
}

QWidget* Qdc::SettingsView::buildTransfers()
{
	QWidget* page = new QWidget(this);

	m_slots->setRange(0, 999);
	m_slots->setToolTip(tr("How many users may download from you at once"));

	m_maxDownloads->setRange(1, 999);
	m_maxDownloads->setToolTip(tr("How many peers to fetch from at once"));

	m_chunkSize->setRange(64, 65536);
	m_chunkSize->setSingleStep(64);
	m_chunkSize->setSuffix(tr(" KiB"));
	m_chunkSize->setToolTip(tr("The least that is asked of one peer at a time; a "
	                           "peer that turns out to be fast is asked for more"));

	m_downloadDir->setPlaceholderText(DEFAULT_DOWNLOAD_DIR);

	QPushButton* browse = new QPushButton(tr("Browse…"), page);
	connect(browse, &QPushButton::clicked, this,
	        &SettingsView::chooseDownloadDirectory);

	QHBoxLayout* downloadRow = new QHBoxLayout;
	downloadRow->setContentsMargins(0, 0, 0, 0);
	downloadRow->addWidget(m_downloadDir, 1);
	downloadRow->addWidget(browse);

	QFormLayout* form = new QFormLayout(page);
	form->addRow(tr("Upload slots"), m_slots);
	form->addRow(tr("Concurrent downloads"), m_maxDownloads);
	form->addRow(tr("Smallest chunk"), m_chunkSize);
	form->addRow(tr("Save downloads to"), downloadRow);

	/*
	 * Said because the field is now a floor rather than the size, and somebody who
	 * set it expecting the latter would otherwise have no way to know.
	 */
	form->addRow(makeNote(tr(
		"A chunk is sized to the source it is asked of: enough for a second or two "
		"of transfer at whatever that peer has been managing, never less than the "
		"figure above. Every chunk costs a round trip, so asking a fast peer for a "
		"small one leaves the connection waiting between chunks instead of "
		"transferring.")));

	/*
	 * Said rather than shown as a disabled field. Nothing in the core throttles:
	 * the bandwidth manager only counts bytes, so a limit here would be stored and
	 * ignored.
	 */
	form->addRow(makeNote(tr(
		"There is no transfer rate limit yet. QuickDC measures its own throughput "
		"but has nothing that slows a transfer down, so a limit set here would "
		"have no effect and is not offered.")));

	/*
	 * Said here rather than in a tooltip, because somebody looking for the folder
	 * their unfinished downloads are in will not find one and is owed the reason.
	 */
	form->addRow(makeNote(tr(
		"An unfinished download is kept beside its final name with \".part\" "
		"added, so there is no separate folder for incomplete files.")));

	connect(m_slots, &QSpinBox::valueChanged, this, &SettingsView::markModified);
	connect(m_maxDownloads, &QSpinBox::valueChanged, this, &SettingsView::markModified);
	connect(m_chunkSize, &QSpinBox::valueChanged, this, &SettingsView::markModified);
	connect(m_downloadDir, &QLineEdit::textEdited, this, &SettingsView::markModified);

	return page;
}

QWidget* Qdc::SettingsView::buildSharing()
{
	QWidget* page = new QWidget(this);

	m_shareList->setSelectionMode(QAbstractItemView::SingleSelection);

	/*
	 * A floor only. The list has the tab to itself and is the thing being read on
	 * it, so it is given whatever height the window has to spare.
	 */
	m_shareList->setMinimumHeight(96);

	QPushButton* addShareButton = new QPushButton(tr("Add folder…"), page);
	connect(addShareButton, &QPushButton::clicked, this, &SettingsView::addShare);
	connect(m_removeShare, &QPushButton::clicked,
	        this, &SettingsView::removeSelectedShare);
	connect(m_renameShare, &QPushButton::clicked,
	        this, &SettingsView::renameSelectedShare);

	const auto followSelection = [this] {
		const bool chosen = m_shareList->currentRow() >= 0;
		m_removeShare->setEnabled(chosen);
		m_renameShare->setEnabled(chosen);
	};

	connect(m_shareList, &QListWidget::currentRowChanged, this, followSelection);
	followSelection();

	QVBoxLayout* shareButtons = new QVBoxLayout;
	shareButtons->setContentsMargins(0, 0, 0, 0);
	shareButtons->addWidget(addShareButton);
	shareButtons->addWidget(m_renameShare);
	shareButtons->addWidget(m_removeShare);
	shareButtons->addStretch(1);

	QHBoxLayout* shareRow = new QHBoxLayout;
	shareRow->setContentsMargins(0, 0, 0, 0);
	shareRow->addWidget(m_shareList, 1);
	shareRow->addLayout(shareButtons);

	/*
	 * A column rather than a form: the list is the page, so it is given a heading of
	 * its own and the height, instead of sitting in a field beside a label.
	 */
	QVBoxLayout* column = new QVBoxLayout(page);
	column->addWidget(new QLabel(tr("Shared folders"), page));
	column->addLayout(shareRow, 1);

	/*
	 * Said rather than left to be discovered: somebody who adds a folder and then
	 * looks for it on the hub will think the share is broken.
	 */
	column->addWidget(makeNote(tr(
		"Shared folders are read when QuickDC starts, so a change here takes "
		"effect the next time it is started.")));

	return page;
}

QWidget* Qdc::SettingsView::buildChat()
{
	QWidget* page = new QWidget(this);

	m_fetchMedia = new QCheckBox(tr("Fetch pictures sent in chat automatically"), page);
	connect(m_fetchMedia, &QCheckBox::toggled, this, &SettingsView::markModified);
	m_fetchMedia->setToolTip(tr("Ask whoever sent a picture for the file, as soon as "
	                            "the message arrives"));

	m_mediaLimit = new QSpinBox(page);
	m_mediaLimit->setRange(0, 1024);
	m_mediaLimit->setSuffix(tr(" MiB"));
	m_mediaLimit->setSpecialValueText(tr("no limit"));
	m_mediaLimit->setToolTip(tr("Larger pictures are left for you to fetch by hand"));
	connect(m_mediaLimit, &QSpinBox::valueChanged, this, &SettingsView::markModified);

	m_mediaKeep = new QSpinBox(page);
	m_mediaKeep->setRange(0, 3650);
	m_mediaKeep->setSuffix(tr(" days"));
	m_mediaKeep->setSpecialValueText(tr("keep for ever"));
	m_mediaKeep->setToolTip(tr("Pictures you paste in are kept this long, in case "
	                           "somebody follows a link to one later"));
	connect(m_mediaKeep, &QSpinBox::valueChanged, this, &SettingsView::markModified);

	QFormLayout* form = new QFormLayout(page);
	form->addRow(QString(), m_fetchMedia);
	form->addRow(tr("Fetch pictures up to"), m_mediaLimit);
	form->addRow(tr("Keep pictures you send for"), m_mediaKeep);

	/*
	 * Said here rather than in a tooltip, because both facts change what somebody would
	 * choose: fetching announces you to whoever sent the picture, and any hub can be
	 * decided about on its own.
	 */
	form->addRow(makeNote(tr(
		"Fetching a picture asks the person who sent it for the file, which tells "
		"them you are there. A single hub can be set to do this or not from its own "
		"page, whatever is chosen here.")));

	return page;
}

QWidget* Qdc::SettingsView::buildBoards()
{
	QWidget* page = new QWidget(this);

	m_prefetchPosts = new QCheckBox(
		tr("Fetch the rest of a thread when I open a post"), page);
	m_prefetchPosts->setToolTip(tr("Ask for the replies before they are read, so that "
	                               "they are there when you get to them"));
	connect(m_prefetchPosts, &QCheckBox::toggled, this, &SettingsView::markModified);

	m_servePosts = new QCheckBox(tr("Offer posts I have read to other people"), page);
	m_servePosts->setToolTip(tr("Answer for a post by its hash, so that somebody else "
	                            "can read it from here"));
	connect(m_servePosts, &QCheckBox::toggled, this, &SettingsView::markModified);

	m_postKeep = new QSpinBox(page);
	m_postKeep->setRange(0, 3650);
	m_postKeep->setSuffix(tr(" days"));
	m_postKeep->setSpecialValueText(tr("keep for ever"));
	m_postKeep->setToolTip(tr("Posts read longer ago than this are thrown away"));
	connect(m_postKeep, &QSpinBox::valueChanged, this, &SettingsView::markModified);

	m_postCache = new QSpinBox(page);

	/*
	 * Shown in mebibytes and stored in bytes, so the top of the range is what the store
	 * can hold rather than a figure about boards: a preference is a number, and two
	 * gibibytes of posts is more than anything this will be asked to keep.
	 */
	m_postCache->setRange(0, 2047);
	m_postCache->setSuffix(tr(" MiB"));
	m_postCache->setSpecialValueText(tr("no limit"));
	m_postCache->setToolTip(tr("The most this will hold of the posts themselves"));
	connect(m_postCache, &QSpinBox::valueChanged, this, &SettingsView::markModified);

	QFormLayout* form = new QFormLayout(page);
	form->addRow(QString(), m_prefetchPosts);
	form->addRow(QString(), m_servePosts);
	form->addRow(tr("Keep posts for"), m_postKeep);
	form->addRow(tr("Keep at most"), m_postCache);

	/*
	 * Said first and said plainly, because it is the thing about a board that somebody
	 * would not think of and cannot find out afterwards. A subscription is answered by
	 * the hub, but a post is a file fetched from whoever holds it - and until somebody
	 * else has read it, the only person who holds it is its author.
	 */
	form->addRow(makeNote(tr(
		"Reading a post fetches it from somebody, and to begin with the only person "
		"who has it is whoever wrote it. So opening a post may tell its author that "
		"you have read it, and when. Nothing is fetched until you open it, and "
		"fetching the rest of a thread is off until you turn it on.")));

	form->addRow(makeNote(tr(
		"Offering a post says the same thing to anybody who asks for it: that you "
		"have read it. It is also what keeps a board readable after the person who "
		"wrote it has gone, since a post ends up held by everyone who read it. A hub "
		"you have not bookmarked offers nothing until you say so on that hub.")));

	form->addRow(makeNote(tr(
		"Both of these can be answered by a single hub for itself, from its own row "
		"in the hub list, whatever is chosen here. The limits apply to the posts "
		"themselves and not to the list of them, which is small and is what makes a "
		"board a board.")));

	return page;
}

QWidget* Qdc::SettingsView::buildAppearance()
{
	QWidget* page = new QWidget(this);

	/*
	 * Filled in load() rather than here, so that Revert picks up a theme file that
	 * was written while the window was open.
	 */
	m_theme->setToolTip(tr("The colours the interface is drawn in"));

	connect(m_theme, &QComboBox::currentIndexChanged, this, &SettingsView::markModified);

	QFormLayout* form = new QFormLayout(page);
	form->addRow(tr("Theme"), m_theme);

	/*
	 * Where the files are is worth saying plainly: a theme is data rather than part
	 * of the program, and somebody who wants a scheme of their own has no other way
	 * to find that out.
	 */
	form->addRow(makeNote(tr(
		"Applied when this page is saved. A theme is a file of colours, and any "
		"dropped in %1 are offered here as well - each one names only what it "
		"changes about the theme it extends. A copy of widgets.qss in the same "
		"folder replaces the stylesheet the interface is built from, which is how "
		"borders, padding and corners are changed rather than only colours.")
			.arg(themeDirectory())));

	return page;
}

void Qdc::SettingsView::markModified()
{
	if (m_loading) return;

	m_modified = true;
	m_save->setEnabled(true);
	m_revert->setEnabled(true);
	m_status->setText(tr("Unsaved changes."));
}

bool Qdc::SettingsView::isModified() const
{
	return m_modified;
}

void Qdc::SettingsView::load()
{
	if (!m_config) return;

	/* So that populating the fields does not read as the user editing them. */
	m_loading = true;

	/*
	 * A group at a time rather than a key at a time. Every read is a blocking trip to
	 * the core thread, so a page of fourteen of them opened while the core was busy
	 * waited a whole core turn fourteen times over.
	 */
	const QVariantHash user = m_config->readGroup(ConfigStore::Application, USER, {
		{QStringLiteral("Nickname"), QString()},
		{QStringLiteral("Description"), QString()},
		{QStringLiteral("E-mail"), QString()},
		{QStringLiteral("Client ID"), QString()}
	});

	m_nickname->setText(user.value(QStringLiteral("Nickname")).toString());
	m_description->setText(user.value(QStringLiteral("Description")).toString());
	m_email->setText(user.value(QStringLiteral("E-mail")).toString());

	const QString cid = user.value(QStringLiteral("Client ID")).toString();
	m_clientId->setText(cid.isEmpty() ? tr("assigned on first connection") : cid);

	const QVariantHash network = m_config->readGroup(ConfigStore::Application, NETWORK, {
		{QStringLiteral("Active mode"), true},
		{QStringLiteral("Port"), DEFAULT_PORT},
		{QStringLiteral("Bind"), QString()},
		{QStringLiteral("Address"), QString()},
		{REACHABILITY_SERVER, QString()},
		{NETWORK_MODE, QString()},
		{FORCE_ACTIVE, FORCE_ACTIVE_DEFAULT},
		{USE_UPNP, USE_UPNP_DEFAULT},
		{QStringLiteral("Slots"), DEFAULT_SLOTS},
		{QStringLiteral("Max downloads"), DEFAULT_MAX_DOWNLOADS},
		{QStringLiteral("Chunk size"), DEFAULT_CHUNK_KIB * KIB}
	});

	/*
	 * The stored mode, or the reading that imposes nothing where the file has none.
	 * A store the core has already opened always has one; this answers for the case
	 * where it has not, which is a page built against a configuration of its own.
	 */
	const NetworkMode mode = networkModeFromToken(
		network.value(NETWORK_MODE).toString(),
		network.value(QStringLiteral("Active mode")).toBool()
			? NetworkMode::Advanced : NetworkMode::Passive);

	m_mode->setCurrentIndex(qMax(0, m_mode->findData(networkModeToken(mode))));
	m_port->setValue(network.value(QStringLiteral("Port")).toInt());
	m_bind->setText(network.value(QStringLiteral("Bind")).toString());
	m_address->setText(network.value(QStringLiteral("Address")).toString());

	m_dropServer->setText(network.value(REACHABILITY_SERVER).toString());

	m_forceActive->setChecked(network.value(FORCE_ACTIVE).toBool());

	m_useUpnp->setChecked(network.value(USE_UPNP).toBool());

	m_slots->setValue(network.value(QStringLiteral("Slots")).toInt());
	m_maxDownloads->setValue(network.value(QStringLiteral("Max downloads")).toInt());

	/* Stored in bytes, shown in KiB. */
	const int chunk = network.value(QStringLiteral("Chunk size")).toInt();
	m_chunkSize->setValue(qMax(m_chunkSize->minimum(), chunk / KIB));

	m_downloadDir->setText(m_config->getString(ConfigStore::Application, DOWNLOADS,
	                                           QStringLiteral("Directory")));

	const QVariantHash localHub = m_config->readGroup(ConfigStore::Application, LOCAL_HUB, {
		{LOCAL_HUB_ENABLED, LOCAL_HUB_ENABLED_DEFAULT},
		{LOCAL_HUB_NAME, LOCAL_HUB_NAME_DEFAULT},
		{LOCAL_HUB_DESCRIPTION, LOCAL_HUB_DESCRIPTION_DEFAULT},
		{LOCAL_HUB_USERS, LOCAL_HUB_USERS_DEFAULT},
		{LOCAL_HUB_HISTORY, LOCAL_HUB_HISTORY_DEFAULT},
		{LOCAL_HUB_SENDQ, LOCAL_HUB_SENDQ_KIB_DEFAULT * KIB},
		{LOCAL_HUB_PER_ADDRESS, LOCAL_HUB_PER_ADDRESS_DEFAULT},
		{LOCAL_HUB_HANDSHAKES, LOCAL_HUB_HANDSHAKES_DEFAULT},
		{LOCAL_HUB_HANDSHAKE_TIMEOUT, LOCAL_HUB_HANDSHAKE_TIMEOUT_DEFAULT},
		{LOCAL_HUB_IDLE_TIMEOUT, LOCAL_HUB_IDLE_TIMEOUT_DEFAULT},
		{LOCAL_HUB_LINGER, LOCAL_HUB_LINGER_DEFAULT},
		{LOCAL_HUB_CHECK_CLAIMS, LOCAL_HUB_CHECK_CLAIMS_DEFAULT}
	});

	m_localHub->setChecked(localHub.value(LOCAL_HUB_ENABLED).toBool());

	m_localHubFields->setEnabled(m_localHub->isChecked());

	m_localHubName->setText(localHub.value(LOCAL_HUB_NAME).toString());
	m_localHubDescription->setText(localHub.value(LOCAL_HUB_DESCRIPTION).toString());

	m_localHubUsers->setValue(localHub.value(LOCAL_HUB_USERS).toInt());
	m_localHubHistory->setValue(localHub.value(LOCAL_HUB_HISTORY).toInt());
	m_localHubPerAddress->setValue(localHub.value(LOCAL_HUB_PER_ADDRESS).toInt());
	m_localHubHandshakes->setValue(localHub.value(LOCAL_HUB_HANDSHAKES).toInt());
	m_localHubHandshakeTimeout->setValue(
		localHub.value(LOCAL_HUB_HANDSHAKE_TIMEOUT).toInt());
	m_localHubIdleTimeout->setValue(localHub.value(LOCAL_HUB_IDLE_TIMEOUT).toInt());
	m_localHubLinger->setValue(localHub.value(LOCAL_HUB_LINGER).toInt());
	m_localHubCheckClaims->setChecked(localHub.value(LOCAL_HUB_CHECK_CLAIMS).toBool());

	const int sendq = localHub.value(LOCAL_HUB_SENDQ).toInt();
	m_localHubSendQueue->setValue(qMax(m_localHubSendQueue->minimum(), sendq / KIB));

	const QVariantHash media = m_config->readGroup(ConfigStore::Application, CHAT_MEDIA, {
		{QStringLiteral("Fetch automatically"), false},
		{QStringLiteral("Maximum size"), 2 * MIB},
		{QStringLiteral("Keep pasted for days"), MEDIA_KEEP_DAYS}
	});

	m_fetchMedia->setChecked(media.value(QStringLiteral("Fetch automatically")).toBool());
	m_mediaLimit->setValue(
		qMax(0, media.value(QStringLiteral("Maximum size")).toInt() / MIB));
	m_mediaKeep->setValue(
		qMax(0, media.value(QStringLiteral("Keep pasted for days")).toInt()));

	const QVariantHash boards = m_config->readGroup(ConfigStore::Application, BOARDS, {
		{BOARD_PREFETCH, BOARD_PREFETCH_DEFAULT},
		{BOARD_SERVE, BOARD_SERVE_DEFAULT},
		{BOARD_KEEP, BOARD_KEEP_DAYS},
		{BOARD_CACHE, BOARD_CACHE_MIB * MIB}
	});

	m_prefetchPosts->setChecked(boards.value(BOARD_PREFETCH).toBool());
	m_servePosts->setChecked(boards.value(BOARD_SERVE).toBool());
	m_postKeep->setValue(qMax(0, boards.value(BOARD_KEEP).toInt()));
	m_postCache->setValue(qMax(0, boards.value(BOARD_CACHE).toInt() / MIB));

	/*
	 * Read afresh each time rather than once when the form was built, so that a theme
	 * file written while the window was open is on the list after a Revert.
	 */
	m_theme->clear();

	for (const Theme& scheme : availableThemes())
		m_theme->addItem(scheme.name, scheme.name);

	const QString chosen = m_config->getString(ConfigStore::Application, INTERFACE,
	                                           THEME_KEY, theme().name);

	const int found = m_theme->findData(chosen);
	m_theme->setCurrentIndex(qMax(0, found));

	refreshShareList();

	m_loading = false;
	m_modified = false;

	m_save->setEnabled(false);
	m_revert->setEnabled(false);
	m_status->clear();
}

bool Qdc::SettingsView::save()
{
	if (!m_config) return false;

	m_config->setString(ConfigStore::Application, USER,
	                    QStringLiteral("Nickname"), m_nickname->text().trimmed());
	m_config->setString(ConfigStore::Application, USER,
	                    QStringLiteral("Description"), m_description->text().trimmed());
	m_config->setString(ConfigStore::Application, USER,
	                    QStringLiteral("E-mail"), m_email->text().trimmed());

	/*
	 * The mode, and then everything it decides.
	 *
	 * Derived rather than read off the switches, because in two of the three modes
	 * those switches are not on screen: saving whatever they happen to hold would
	 * write a state nobody chose. Advanced is the mode that answers with them, so it
	 * is the only one where they are read - which is also what makes the answer to
	 * "what did I set the port to" survive a trip through automatic.
	 */
	const NetworkMode mode = networkModeFromToken(
		m_mode->currentData().toString(), NetworkMode::Advanced);

	const NetworkModeKeys keys = networkModeKeys(mode,
		m_useUpnp->isChecked(),
		m_forceActive->isChecked(),
		m_dropServer->text().trimmed());

	m_config->setString(ConfigStore::Application, NETWORK, NETWORK_MODE,
	                    networkModeToken(mode));

	m_config->setBool(ConfigStore::Application, NETWORK,
	                  QStringLiteral("Active mode"), keys.activeMode);
	m_config->setString(ConfigStore::Application, NETWORK,
	                    REACHABILITY_SERVER, keys.reachabilityServer);
	m_config->setBool(ConfigStore::Application, NETWORK,
	                  FORCE_ACTIVE, keys.forceActive);
	m_config->setBool(ConfigStore::Application, NETWORK, USE_UPNP, keys.useUpnp);

	/*
	 * The plumbing only where somebody is choosing it. The port is not merely bound
	 * from this key - it is also the number handed to every peer - so writing it from
	 * a field the user cannot see would have the client advertise whatever the form
	 * last loaded, and a peer dialling a port nothing listens on fails at their end
	 * rather than at ours.
	 */
	if (mode == NetworkMode::Advanced)
	{
		m_config->setNumber(ConfigStore::Application, NETWORK,
		                    QStringLiteral("Port"), m_port->value());
		m_config->setString(ConfigStore::Application, NETWORK,
		                    QStringLiteral("Bind"), m_bind->text().trimmed());
		m_config->setString(ConfigStore::Application, NETWORK,
		                    QStringLiteral("Address"), m_address->text().trimmed());
	}

	m_config->setNumber(ConfigStore::Application, NETWORK,
	                    QStringLiteral("Slots"), m_slots->value());
	m_config->setNumber(ConfigStore::Application, NETWORK,
	                    QStringLiteral("Max downloads"), m_maxDownloads->value());
	m_config->setNumber(ConfigStore::Application, NETWORK,
	                    QStringLiteral("Chunk size"), m_chunkSize->value() * KIB);

	m_config->setString(ConfigStore::Application, DOWNLOADS,
	                    QStringLiteral("Directory"), m_downloadDir->text().trimmed());

	m_config->setBool(ConfigStore::Application, LOCAL_HUB, LOCAL_HUB_ENABLED,
	                  m_localHub->isChecked());
	m_config->setString(ConfigStore::Application, LOCAL_HUB, LOCAL_HUB_NAME,
	                    m_localHubName->text().trimmed());
	m_config->setString(ConfigStore::Application, LOCAL_HUB, LOCAL_HUB_DESCRIPTION,
	                    m_localHubDescription->text().trimmed());
	m_config->setNumber(ConfigStore::Application, LOCAL_HUB, LOCAL_HUB_USERS,
	                    m_localHubUsers->value());
	m_config->setNumber(ConfigStore::Application, LOCAL_HUB, LOCAL_HUB_HISTORY,
	                    m_localHubHistory->value());
	m_config->setNumber(ConfigStore::Application, LOCAL_HUB, LOCAL_HUB_SENDQ,
	                    m_localHubSendQueue->value() * KIB);
	m_config->setNumber(ConfigStore::Application, LOCAL_HUB, LOCAL_HUB_PER_ADDRESS,
	                    m_localHubPerAddress->value());
	m_config->setNumber(ConfigStore::Application, LOCAL_HUB, LOCAL_HUB_HANDSHAKES,
	                    m_localHubHandshakes->value());
	m_config->setNumber(ConfigStore::Application, LOCAL_HUB,
	                    LOCAL_HUB_HANDSHAKE_TIMEOUT, m_localHubHandshakeTimeout->value());
	m_config->setNumber(ConfigStore::Application, LOCAL_HUB, LOCAL_HUB_IDLE_TIMEOUT,
	                    m_localHubIdleTimeout->value());
	m_config->setNumber(ConfigStore::Application, LOCAL_HUB, LOCAL_HUB_LINGER,
	                    m_localHubLinger->value());
	m_config->setBool(ConfigStore::Application, LOCAL_HUB, LOCAL_HUB_CHECK_CLAIMS,
	                  m_localHubCheckClaims->isChecked());

	m_config->setBool(ConfigStore::Application, CHAT_MEDIA,
	                  QStringLiteral("Fetch automatically"), m_fetchMedia->isChecked());
	m_config->setNumber(ConfigStore::Application, CHAT_MEDIA,
	                    QStringLiteral("Maximum size"), m_mediaLimit->value() * MIB);
	m_config->setNumber(ConfigStore::Application, CHAT_MEDIA,
	                    QStringLiteral("Keep pasted for days"), m_mediaKeep->value());

	m_config->setBool(ConfigStore::Application, BOARDS, BOARD_PREFETCH,
	                  m_prefetchPosts->isChecked());
	m_config->setBool(ConfigStore::Application, BOARDS, BOARD_SERVE,
	                  m_servePosts->isChecked());
	m_config->setNumber(ConfigStore::Application, BOARDS, BOARD_KEEP,
	                    m_postKeep->value());
	m_config->setNumber(ConfigStore::Application, BOARDS, BOARD_CACHE,
	                    m_postCache->value() * MIB);

	m_config->setString(ConfigStore::Application, INTERFACE, THEME_KEY,
	                    m_theme->currentData().toString());

	/*
	 * The file is written a moment later, on the core's own thread. Waiting to be
	 * told whether it worked is what made this page stall for as long as the core
	 * was busy; a write that fails reports itself through saveFailed() instead.
	 */
	m_config->save(ConfigStore::Application);

	/*
	 * Storing a nickname is not the same as using it: the hubs republish only when
	 * asked, and this is the ask.
	 */
	m_config->notifyChanged();

	/*
	 * The gateway mapping, brought into line with the switch that was just written.
	 * After the writes above rather than before them, because the core reads the
	 * setting rather than being handed it, and the jobs carrying both run in the
	 * order they were posted.
	 */
	if (m_drop) m_drop->applyForwarding();

	/*
	 * Here rather than as the combo changes: a theme applied while somebody is moving
	 * down a list restyles the whole window at every name they pass on the way to the
	 * one they wanted. Nothing to do when the scheme has not changed, which is almost
	 * every save.
	 */
	const QString scheme = m_theme->currentData().toString();
	if (scheme != theme().name) applyTheme(themeByName(scheme));

	m_modified = false;
	m_save->setEnabled(false);
	m_revert->setEnabled(false);
	m_status->setText(tr("Saved."));

	emit saved();
	return true;
}

void Qdc::SettingsView::clearVerdict()
{
	m_reachability->clear();
}

void Qdc::SettingsView::showVerdict(const ReachabilityVerdict& verdict)
{
	m_reachability->setVerdict(verdict);
}

void Qdc::SettingsView::showCheckFailed(const QString& reason)
{
	m_reachability->setFailure(reason);
}

void Qdc::SettingsView::refreshShareList()
{
	m_shareList->clear();
	if (!m_shares) return;

	for (const SharedFolder& folder : m_shares->folders())
	{
		QListWidgetItem* item = new QListWidgetItem(
			QStringLiteral("%1  —  %2").arg(folder.name, folder.path), m_shareList);

		/*
		 * Both fields are this user's own, so there is nobody else's text here to
		 * disarm - but a tooltip is drawn as markup whoever wrote it, and a folder named
		 * with an angle bracket in it should read as it was typed.
		 */
		item->setToolTip(RichText::literalHtml(tr("Shown to others as '%1'\n%2")
			.arg(folder.name, folder.path)));
	}
}

void Qdc::SettingsView::addShare()
{
	if (!m_shares) return;

	chooseDirectory(tr("Share a folder"), QDir::homePath(),
	                [this](const QString& chosen) {
		/*
		 * The shares list writes itself through immediately rather than waiting for
		 * Save, because it is its own store and a half-applied folder list is not a
		 * useful state to be in.
		 */
		if (!m_shares->add(chosen))
		{
			QMessageBox::information(this, tr("Share a folder"),
				tr("'%1' is already shared, or is inside a folder that is.")
					.arg(chosen));
			return;
		}

		refreshShareList();
	});
}

void Qdc::SettingsView::removeSelectedShare()
{
	if (!m_shares) return;

	const int row = m_shareList->currentRow();
	if (row < 0) return;

	if (m_shares->removeAt(row)) refreshShareList();
}

void Qdc::SettingsView::renameSelectedShare()
{
	if (!m_shares) return;

	const int row = m_shareList->currentRow();
	if (row < 0 || row >= m_shares->count()) return;

	const SharedFolder folder = m_shares->folders().at(row);

	bool accepted = false;
	const QString name = QInputDialog::getText(this, tr("Rename a shared folder"),
		tr("What other users see this folder as:"), QLineEdit::Normal,
		folder.name, &accepted);

	if (!accepted) return;

	if (m_shares->renameAt(row, name)) refreshShareList();
}

void Qdc::SettingsView::chooseDirectory(const QString& title, const QString& start,
                                        std::function<void(const QString&)> chosen)
{
	/*
	 * On the next turn of the event loop, and drawn by Qt rather than by the platform,
	 * for the same reasons the composer's file chooser is - see HubsView. Both
	 * failures look exactly like a button that does nothing: a chooser opened from
	 * inside the press that asked for it is dismissed again by that same press, and
	 * the platform's own panel, asked for modally, returns at once with nothing
	 * chosen.
	 */
	QTimer::singleShot(0, this, [this, title, start, chosen] {
		const QString directory = QFileDialog::getExistingDirectory(this, title, start,
			QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);

		if (directory.isEmpty()) return;

		chosen(directory);
	});
}

void Qdc::SettingsView::chooseDownloadDirectory()
{
	const QString current = m_downloadDir->text().trimmed();

	chooseDirectory(tr("Save downloads to"),
		current.isEmpty() ? QDir::homePath() : current, [this](const QString& chosen) {
			m_downloadDir->setText(chosen);
			markModified();
		});
}

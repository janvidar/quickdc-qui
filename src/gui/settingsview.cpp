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
#include <core/shares.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

/* Every key and default comes from Qdc::Settings, which takes the core's own schema. */
namespace S = Qdc::Settings;

constexpr int KIB = 1024;
constexpr int MIB = 1024 * 1024;

/* A page is a column this wide at most; a form spread over a wide window is hard to read. */
constexpr int PAGE_WIDTH = 640;
constexpr int TEXT_WIDTH = 340;
constexpr int NUMBER_WIDTH = 140;

QFormLayout* makeForm(QWidget* on)
{
	QFormLayout* form = new QFormLayout(on);
	form->setContentsMargins(0, 0, 0, 0);
	form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
	form->setLabelAlignment(Qt::AlignLeft);
	return form;
}

QSpinBox* makeNumber(QWidget* parent, int low, int high, const QString& suffix = QString())
{
	QSpinBox* spin = new QSpinBox(parent);
	spin->setRange(low, high);
	spin->setSuffix(suffix);
	spin->setMinimumWidth(NUMBER_WIDTH);
	spin->setKeyboardTracking(false);
	spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
	return spin;
}

QLineEdit* makeText(QWidget* parent, int maxLength = 255)
{
	QLineEdit* edit = new QLineEdit(parent);
	edit->setMaxLength(maxLength);
	edit->setMinimumWidth(TEXT_WIDTH);
	return edit;
}

}

Qdc::SettingsView::SettingsView(ConfigBridge* config, Shares* shares, DropBridge* drop,
                                QWidget* parent)
	: View(parent)
	, m_config(config)
	, m_shares(shares)
	, m_drop(drop)
	, m_pages(new QListWidget(this))
	, m_stack(new QStackedWidget(this))
	, m_status(new QLabel(this))
{
	m_pages->setObjectName(QStringLiteral("settingsPages"));
	m_pages->setFixedWidth(180);
	m_pages->setFrameShape(QFrame::NoFrame);
	connect(m_pages, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);

	addPage(tr("Identity"), buildIdentity());
	addPage(tr("Connection"), buildConnection());
	addPage(tr("Local hub"), buildLocalHub());
	addPage(tr("Transfers"), buildTransfers());
	addPage(tr("Sharing"), buildSharing());
	addPage(tr("Chat"), buildChat());
	addPage(tr("Boards"), buildBoards());
	addPage(tr("Appearance"), buildAppearance());
	m_pages->setCurrentRow(0);

	m_status->setObjectName(QStringLiteral("mutedText"));
	m_status->setContentsMargins(16, 0, 16, 12);
	m_status->hide();

	QVBoxLayout* right = new QVBoxLayout;
	right->setContentsMargins(0, 0, 0, 0);
	right->setSpacing(0);
	right->addWidget(m_stack, 1);
	right->addWidget(m_status);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(m_pages);
	layout->addLayout(right, 1);

	if (m_drop)
	{
		connect(m_drop, &DropBridge::verdictChanged, m_reachability, &ReachabilityPanel::setVerdict);
		connect(m_drop, &DropBridge::checkFailed, m_reachability, &ReachabilityPanel::setFailure);

		/* Only for a run that began: one the bridge declines reports nothing, and would leave this up. */
		connect(m_drop, &DropBridge::checkStarted, this, [this](bool) { m_reachability->setChecking(true); });

		m_reachability->setVerdict(m_drop->verdict(false));
		m_reachability->setVerdict(m_drop->verdict(true));

		connect(m_reachability, &ReachabilityPanel::checkRequested, m_drop, &DropBridge::recheck);
	}
	else
	{
		m_reachability->checkButton()->setEnabled(false);
	}

	if (!m_config)
	{
		m_stack->setEnabled(false);
		m_status->setText(tr("The configuration is not available."));
		m_status->show();
		return;
	}

	connect(m_config, &ConfigBridge::saveFailed, this, [this](ConfigStore store) {
		if (store != ConfigStore::Application) return;
		m_status->setText(tr("The configuration could not be written."));
		m_status->show();
	});

	/* A setting changed elsewhere - qdc, another window, the core itself - shows here too. */
	QTimer* reload = new QTimer(this);
	reload->setSingleShot(true);
	reload->setInterval(0);
	connect(reload, &QTimer::timeout, this, &SettingsView::load);
	connect(m_config, &ConfigBridge::changed, reload, [reload](ConfigStore store, const QString&, const QString&) {
		if (store == ConfigStore::Application) reload->start();
	});
	connect(m_config, &ConfigBridge::connectionModeChanged, reload, qOverload<>(&QTimer::start));
	connect(m_config, &ConfigBridge::peerPortChanged, this, &SettingsView::showPeerPort);

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

void Qdc::SettingsView::addPage(const QString& name, QWidget* content)
{
	content->setMinimumWidth(PAGE_WIDTH - 80);
	content->setMaximumWidth(PAGE_WIDTH);

	QWidget* holder = new QWidget(this);
	QLabel* heading = new QLabel(name, holder);
	heading->setObjectName(QStringLiteral("settingsHeading"));

	QVBoxLayout* column = new QVBoxLayout(holder);
	column->setContentsMargins(28, 20, 28, 20);
	column->addWidget(heading);
	column->addWidget(content, 0, Qt::AlignLeft | Qt::AlignTop);
	column->addStretch(1);

	QScrollArea* scroll = new QScrollArea(this);
	scroll->setWidget(holder);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);

	m_stack->addWidget(scroll);

	QListWidgetItem* item = new QListWidgetItem(name, m_pages);
	item->setSizeHint(QSize(0, 34));
}

QWidget* Qdc::SettingsView::advancedSection(QWidget* content)
{
	QWidget* section = new QWidget(content->parentWidget());

	QToolButton* toggle = new QToolButton(section);
	toggle->setText(tr("More settings"));
	toggle->setCheckable(true);
	toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	toggle->setArrowType(Qt::RightArrow);
	toggle->setAutoRaise(true);

	content->setParent(section);
	content->hide();

	connect(toggle, &QToolButton::toggled, section, [toggle, content](bool open) {
		toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
		content->setVisible(open);
	});

	QVBoxLayout* layout = new QVBoxLayout(section);
	layout->setContentsMargins(0, 8, 0, 0);
	layout->addWidget(toggle, 0, Qt::AlignLeft);
	layout->addWidget(content);
	return section;
}

QLabel* Qdc::SettingsView::makeHint(const QString& text, QWidget* parent)
{
	QLabel* hint = Qdc::makeNote(text, parent);
	hint->setWordWrap(true);
	return hint;
}

void Qdc::SettingsView::bind(QLineEdit* field, const Settings::Text& setting)
{
	m_loaders.push_back([this, field, setting] {
		if (!field->hasFocus()) field->setText(m_config->read(setting));
	});

	connect(field, &QLineEdit::editingFinished, this, [this, field, setting] {
		const QString value = field->text().trimmed();
		if (!m_loading && value != m_config->read(setting)) m_config->write(setting, value);
	});
}

void Qdc::SettingsView::bind(QSpinBox* field, const Settings::Number& setting, int scale)
{
	m_loaders.push_back([this, field, setting, scale] {
		if (!field->hasFocus()) field->setValue(qMax(field->minimum(), m_config->read(setting) / scale));
	});

	connect(field, &QSpinBox::valueChanged, this, [this, setting, scale](int value) {
		if (!m_loading && value * scale != m_config->read(setting)) m_config->write(setting, value * scale);
	});
}

void Qdc::SettingsView::bind(QCheckBox* field, const Settings::Flag& setting)
{
	m_loaders.push_back([this, field, setting] { field->setChecked(m_config->read(setting)); });

	connect(field, &QCheckBox::toggled, this, [this, setting](bool on) {
		if (!m_loading && on != m_config->read(setting)) m_config->write(setting, on);
	});
}

QWidget* Qdc::SettingsView::buildIdentity()
{
	QWidget* page = new QWidget(this);

	QLineEdit* nickname = makeText(page, 63);
	nickname->setObjectName(QStringLiteral("identityNickname"));
	QLineEdit* description = makeText(page);
	description->setObjectName(QStringLiteral("identityDescription"));
	QLineEdit* email = makeText(page, 127);
	email->setObjectName(QStringLiteral("identityEmail"));

	m_clientId = new QLabel(page);
	m_clientId->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_clientId->setToolTip(tr("Derived from this installation's private key"));
	QFont mono = m_clientId->font();
	mono.setFamily(QStringLiteral("monospace"));
	m_clientId->setFont(mono);

	QFormLayout* form = makeForm(page);
	form->addRow(tr("Nickname"), nickname);
	form->addRow(tr("Description"), description);
	form->addRow(tr("E-mail"), email);
	form->addRow(tr("Client ID"), m_clientId);

	bind(nickname, S::Nickname);
	bind(description, S::Description);
	bind(email, S::Email);
	m_loaders.push_back([this] {
		const QString cid = m_config->read(S::ClientId);
		m_clientId->setText(cid.isEmpty() ? tr("assigned on first connection") : cid);
	});

	return page;
}

QWidget* Qdc::SettingsView::buildConnection()
{
	QWidget* page = new QWidget(this);

	m_mode = new QComboBox(page);
	m_mode->setObjectName(QStringLiteral("connectionMode"));
	m_mode->addItem(tr("Automatic"), static_cast<int>(NetworkMode::Automatic));
	m_mode->addItem(tr("Advanced"), static_cast<int>(NetworkMode::Advanced));
	m_mode->addItem(tr("Passive"), static_cast<int>(NetworkMode::Passive));
	m_mode->setItemData(0, tr("Choose a port, forward it and check it for me"), Qt::ToolTipRole);
	m_mode->setItemData(1, tr("I choose the port and the forwarding"), Qt::ToolTipRole);
	m_mode->setItemData(2, tr("Only connect out"), Qt::ToolTipRole);

	m_modeHint = makeHint(QString(), page);

	m_automaticPort = new QLabel(page);
	m_automaticPort->setObjectName(QStringLiteral("connectionPortInUse"));

	m_port = makeNumber(page, 1, 65535);
	m_port->setObjectName(QStringLiteral("connectionPort"));
	m_portStatus = new QLabel(page);
	m_portStatus->setObjectName(QStringLiteral("connectionPortStatus"));

	QHBoxLayout* portRow = new QHBoxLayout;
	m_portRow = portRow;
	portRow->setContentsMargins(0, 0, 0, 0);
	portRow->addWidget(m_port);
	portRow->addWidget(m_portStatus);
	portRow->addStretch(1);

	m_useUpnp = new QCheckBox(tr("Ask the router to forward the port (UPnP)"), page);
	m_useUpnp->setObjectName(QStringLiteral("connectionUpnp"));
	m_useUpnp->setToolTip(tr("Announces this machine to the local network to find the router"));

	QWidget* more = new QWidget(page);
	QLineEdit* bindTo = makeText(more, 63);
	bindTo->setObjectName(QStringLiteral("connectionBind"));
	bindTo->setPlaceholderText(QString::fromUtf8(S::Bind.fallback));
	QLineEdit* address = makeText(more, 63);
	address->setObjectName(QStringLiteral("connectionAddress"));
	address->setPlaceholderText(tr("Detected automatically"));
	m_forceActive = new QCheckBox(tr("Always tell hubs I can be connected to"), more);
	m_forceActive->setObjectName(QStringLiteral("connectionForceActive"));
	m_forceActive->setToolTip(tr("Whatever the check says. If it is wrong, peers fail to reach you."));

	QFormLayout* moreForm = makeForm(more);
	moreForm->addRow(tr("Bind to"), bindTo);
	moreForm->addRow(tr("External address"), address);
	moreForm->addRow(QString(), m_forceActive);
	m_advancedMore = advancedSection(more);

	m_reachability = new ReachabilityPanel(page);

	m_connectionForm = makeForm(page);
	m_connectionForm->addRow(tr("Connection"), m_mode);
	m_connectionForm->addRow(m_modeHint);
	m_connectionForm->addRow(tr("Port"), m_automaticPort);
	m_connectionForm->addRow(tr("Port"), portRow);
	m_connectionForm->addRow(QString(), m_useUpnp);
	m_connectionForm->addRow(m_reachability);
	m_connectionForm->addRow(m_advancedMore);

	bind(m_port, S::Port);
	bind(bindTo, S::Bind);
	bind(address, S::Address);

	connect(m_mode, &QComboBox::activated, this, [this] {
		applyConnectionMode();
		submitConnectionMode();
	});
	connect(m_useUpnp, &QCheckBox::toggled, this, &SettingsView::submitConnectionMode);
	connect(m_forceActive, &QCheckBox::toggled, this, &SettingsView::submitConnectionMode);

	m_loaders.push_back([this] {
		const ConnectionMode connection = m_config->connectionMode();
		m_mode->setCurrentIndex(qMax(0, m_mode->findData(static_cast<int>(connection.mode))));
		m_useUpnp->setChecked(connection.useUpnp);
		m_forceActive->setChecked(connection.forceActive);
	});

	return page;
}

void Qdc::SettingsView::submitConnectionMode()
{
	if (m_loading || !m_config) return;

	/* Always sent: what the core last reported may not include a choice still on its way. */
	const NetworkMode mode = static_cast<NetworkMode>(m_mode->currentData().toInt());
	m_config->setConnectionMode(mode, m_useUpnp->isChecked(), m_forceActive->isChecked());
}

void Qdc::SettingsView::applyConnectionMode()
{
	const NetworkMode mode = static_cast<NetworkMode>(m_mode->currentData().toInt());
	const QString server = m_config ? m_config->connectionMode().reachabilityServer : QString();

	const bool advanced = mode == NetworkMode::Advanced;
	m_connectionForm->setRowVisible(m_automaticPort, mode == NetworkMode::Automatic);
	m_connectionForm->setRowVisible(m_portRow, advanced);
	m_connectionForm->setRowVisible(m_useUpnp, advanced);
	m_connectionForm->setRowVisible(m_advancedMore, advanced);
	m_reachability->setShowPorts(mode != NetworkMode::Passive);

	if (mode == NetworkMode::Automatic)
		m_modeHint->setText(server.isEmpty() ? tr("Picks a port and asks the router to forward it.")
		                                     : tr("Picks a port, asks the router to forward it, and checks it with %1.").arg(server));
	else if (mode == NetworkMode::Passive)
		m_modeHint->setText(tr("Peers cannot connect to you, so transfers with other passive users do not work."));
	m_modeHint->setVisible(mode != NetworkMode::Advanced);
}

void Qdc::SettingsView::showPeerPort()
{
	if (!m_config) return;

	const PeerPort port = m_config->peerPort();

	if (!port.bound)
	{
		m_portStatus->setText(tr("Not listening"));
		m_automaticPort->setText(tr("Not listening"));
	}
	else if (port.configured && port.bound != port.configured)
	{
		m_portStatus->setText(tr("%1 is in use, listening on %2").arg(port.configured).arg(port.bound));
		m_automaticPort->setText(tr("%1 (%2 is in use)").arg(port.bound).arg(port.configured));
	}
	else
	{
		m_portStatus->setText(tr("Listening"));
		m_automaticPort->setText(QString::number(port.bound));
	}

	if (m_localHubHint)
		m_localHubHint->setText(port.bound
			? tr("Anyone who can reach port %1 can join; there is no password.").arg(port.bound)
			: tr("Anyone who can reach the client's port can join; there is no password."));
}

QWidget* Qdc::SettingsView::buildLocalHub()
{
	QWidget* page = new QWidget(this);

	m_localHub = new QCheckBox(tr("Run a hub on this computer"), page);
	m_localHub->setObjectName(QStringLiteral("localHubEnabled"));

	m_localHubFields = new QWidget(page);

	QLineEdit* name = makeText(m_localHubFields, 63);
	name->setPlaceholderText(QString::fromUtf8(S::HubName.fallback));
	QLineEdit* description = makeText(m_localHubFields);
	description->setPlaceholderText(QString::fromUtf8(S::HubDescription.fallback));

	QWidget* limits = new QWidget(m_localHubFields);
	QSpinBox* users = makeNumber(limits, 1, 65536);
	users->setObjectName(QStringLiteral("localHubUsers"));
	QSpinBox* history = makeNumber(limits, 0, 65536, tr(" lines"));
	history->setSpecialValueText(tr("none"));
	QSpinBox* perAddress = makeNumber(limits, 1, 65536);
	QSpinBox* handshakes = makeNumber(limits, 1, 65536);
	QSpinBox* handshakeTimeout = makeNumber(limits, 0, 86400, tr(" s"));
	handshakeTimeout->setSpecialValueText(tr("no limit"));
	QSpinBox* idleTimeout = makeNumber(limits, 0, 86400, tr(" s"));
	idleTimeout->setSpecialValueText(tr("no limit"));
	QSpinBox* linger = makeNumber(limits, 0, 86400, tr(" s"));
	linger->setSpecialValueText(tr("no limit"));
	QSpinBox* sendQueue = makeNumber(limits, 4, 16 * KIB, tr(" KiB"));
	sendQueue->setSingleStep(4);
	sendQueue->setObjectName(QStringLiteral("localHubSendQueue"));
	sendQueue->setToolTip(tr("Beyond this, only the most important messages go to that person"));
	QCheckBox* checkClaims = new QCheckBox(tr("Check that clients can be connected to"), limits);

	QFormLayout* limitForm = makeForm(limits);
	limitForm->addRow(tr("Users at most"), users);
	limitForm->addRow(tr("Chat shown on arrival"), history);
	limitForm->addRow(tr("Connections per address"), perAddress);
	limitForm->addRow(tr("Logins at once"), handshakes);
	limitForm->addRow(tr("Login timeout"), handshakeTimeout);
	limitForm->addRow(tr("Idle timeout"), idleTimeout);
	limitForm->addRow(tr("Kick grace"), linger);
	limitForm->addRow(tr("Send queue per user"), sendQueue);
	limitForm->addRow(QString(), checkClaims);

	m_localHubHint = makeHint(QString(), m_localHubFields);

	QFormLayout* form = makeForm(m_localHubFields);
	form->addRow(tr("Name"), name);
	form->addRow(tr("Description"), description);
	form->addRow(m_localHubHint);
	form->addRow(advancedSection(limits));

	connect(m_localHub, &QCheckBox::toggled, m_localHubFields, &QWidget::setEnabled);

	QVBoxLayout* column = new QVBoxLayout(page);
	column->setContentsMargins(0, 0, 0, 0);
	column->addWidget(m_localHub);
	column->addWidget(m_localHubFields);

	bind(m_localHub, S::HubEnabled);
	bind(name, S::HubName);
	bind(description, S::HubDescription);
	bind(users, S::HubMaxUsers);
	bind(history, S::HubMaxHistory);
	bind(perAddress, S::HubMaxPerAddress);
	bind(handshakes, S::HubMaxHandshakes);
	bind(handshakeTimeout, S::HubHandshakeTimeout);
	bind(idleTimeout, S::HubIdleTimeout);
	bind(linger, S::HubLingerTimeout);
	bind(sendQueue, S::HubMaxSendQueue, KIB);
	bind(checkClaims, S::HubCheckClaims);

	return page;
}

QWidget* Qdc::SettingsView::buildTransfers()
{
	QWidget* page = new QWidget(this);

	QSpinBox* uploadSlots = makeNumber(page, 0, 999);
	uploadSlots->setObjectName(QStringLiteral("transfersSlots"));
	uploadSlots->setToolTip(tr("How many people may download from you at once"));
	QSpinBox* downloads = makeNumber(page, 1, 999);
	downloads->setObjectName(QStringLiteral("transfersDownloads"));
	downloads->setToolTip(tr("How many peers to download from at once"));

	m_downloadDir = makeText(page, 4096);
	m_downloadDir->setObjectName(QStringLiteral("transfersDirectory"));
	m_downloadDir->setPlaceholderText(QString::fromUtf8(S::DownloadDirectory.fallback));
	QPushButton* browse = new QPushButton(tr("Browse…"), page);
	connect(browse, &QPushButton::clicked, this, &SettingsView::chooseDownloadDirectory);

	QHBoxLayout* directoryRow = new QHBoxLayout;
	directoryRow->setContentsMargins(0, 0, 0, 0);
	directoryRow->addWidget(m_downloadDir);
	directoryRow->addWidget(browse);

	QWidget* more = new QWidget(page);
	QSpinBox* chunk = makeNumber(more, 64, 65536, tr(" KiB"));
	chunk->setObjectName(QStringLiteral("transfersChunk"));
	chunk->setSingleStep(64);
	chunk->setToolTip(tr("The least asked of a peer at a time; fast peers are asked for more"));
	makeForm(more)->addRow(tr("Smallest chunk"), chunk);

	QFormLayout* form = makeForm(page);
	form->addRow(tr("Upload slots"), uploadSlots);
	form->addRow(tr("Downloads at once"), downloads);
	form->addRow(tr("Save downloads to"), directoryRow);
	form->addRow(advancedSection(more));

	bind(uploadSlots, S::Slots);
	bind(downloads, S::MaxDownloads);
	bind(chunk, S::ChunkSize, KIB);
	bind(m_downloadDir, S::DownloadDirectory);

	return page;
}

QWidget* Qdc::SettingsView::buildSharing()
{
	QWidget* page = new QWidget(this);
	page->setMinimumWidth(PAGE_WIDTH);

	m_shareList = new QListWidget(page);
	m_shareList->setObjectName(QStringLiteral("sharingFolders"));
	m_shareList->setSelectionMode(QAbstractItemView::SingleSelection);
	m_shareList->setMinimumHeight(240);

	QPushButton* add = new QPushButton(tr("Add folder…"), page);
	m_renameShare = new QPushButton(tr("Rename…"), page);
	m_removeShare = new QPushButton(tr("Remove"), page);
	connect(add, &QPushButton::clicked, this, &SettingsView::addShare);
	connect(m_renameShare, &QPushButton::clicked, this, &SettingsView::renameSelectedShare);
	connect(m_removeShare, &QPushButton::clicked, this, &SettingsView::removeSelectedShare);

	const auto followSelection = [this] {
		const bool chosen = m_shareList->currentRow() >= 0;
		m_removeShare->setEnabled(chosen);
		m_renameShare->setEnabled(chosen);
	};
	connect(m_shareList, &QListWidget::currentRowChanged, this, followSelection);
	followSelection();

	QVBoxLayout* buttons = new QVBoxLayout;
	buttons->setContentsMargins(0, 0, 0, 0);
	buttons->addWidget(add);
	buttons->addWidget(m_renameShare);
	buttons->addWidget(m_removeShare);
	buttons->addStretch(1);

	QHBoxLayout* row = new QHBoxLayout(page);
	row->setContentsMargins(0, 0, 0, 0);
	row->addWidget(m_shareList, 1);
	row->addLayout(buttons);

	if (m_shares)
	{
		connect(m_shares, &Shares::changed, this, &SettingsView::refreshShareList);
		connect(m_shares, &Shares::refused, this, [this](const QString& path, ShareRefusal why) {
			if (why == ShareRefusal::Other) return;

			QMessageBox::information(this, tr("Share a folder"), why == ShareRefusal::NotAFolder
				? tr("'%1' is not a folder that can be shared.").arg(path)
				: tr("'%1' is already shared, or is inside a folder that is.").arg(path));
		});
	}

	m_loaders.push_back([this] { refreshShareList(); });
	return page;
}

QWidget* Qdc::SettingsView::buildChat()
{
	QWidget* page = new QWidget(this);

	QCheckBox* fetch = new QCheckBox(tr("Fetch pictures sent in chat"), page);
	fetch->setObjectName(QStringLiteral("chatFetch"));
	QSpinBox* limit = makeNumber(page, 0, 1024, tr(" MiB"));
	limit->setSpecialValueText(tr("no limit"));
	limit->setToolTip(tr("Larger pictures are left for you to fetch by hand"));
	QSpinBox* keep = makeNumber(page, 0, 3650, tr(" days"));
	keep->setSpecialValueText(tr("for ever"));

	QFormLayout* form = makeForm(page);
	form->addRow(QString(), fetch);
	form->addRow(tr("Fetch pictures up to"), limit);
	form->addRow(tr("Keep pictures I send"), keep);
	form->addRow(makeHint(tr("Fetching a picture tells its sender you are online. Each hub can override this."), page));

	bind(fetch, S::ChatMediaFetch);
	bind(limit, S::ChatMediaLimit, MIB);
	bind(keep, S::ChatMediaKeepDays);

	return page;
}

QWidget* Qdc::SettingsView::buildBoards()
{
	QWidget* page = new QWidget(this);

	QCheckBox* prefetch = new QCheckBox(tr("Fetch the rest of a thread when I open a post"), page);
	QCheckBox* serve = new QCheckBox(tr("Offer posts I have read to others"), page);
	QSpinBox* keep = makeNumber(page, 0, 3650, tr(" days"));
	keep->setSpecialValueText(tr("for ever"));
	QSpinBox* cache = makeNumber(page, 0, 2047, tr(" MiB"));
	cache->setObjectName(QStringLiteral("boardsCache"));
	cache->setSpecialValueText(tr("no limit"));

	QFormLayout* form = makeForm(page);
	form->addRow(QString(), prefetch);
	form->addRow(QString(), serve);
	form->addRow(tr("Keep posts for"), keep);
	form->addRow(tr("Keep at most"), cache);
	form->addRow(makeHint(tr("Opening or offering a post can tell others you have read it. Each hub can override this."), page));

	bind(prefetch, S::BoardPrefetch);
	bind(serve, S::BoardServe);
	bind(keep, S::BoardKeepDays);
	bind(cache, S::BoardCache, MIB);

	return page;
}

QWidget* Qdc::SettingsView::buildAppearance()
{
	QWidget* page = new QWidget(this);

	m_theme = new QComboBox(page);
	m_theme->setObjectName(QStringLiteral("appearanceTheme"));
	m_theme->setMinimumWidth(TEXT_WIDTH / 2);
	m_theme->setToolTip(tr("Themes dropped in %1 are listed too").arg(themeDirectory()));

	makeForm(page)->addRow(tr("Theme"), m_theme);

	/* On a choice made, not on every name passed on the way to it. */
	connect(m_theme, &QComboBox::activated, this, [this] {
		const QString scheme = m_theme->currentData().toString();
		m_config->write(S::ThemeName, scheme);
		if (scheme != theme().name) applyTheme(themeByName(scheme));
	});

	m_loaders.push_back([this] {
		m_theme->clear();
		for (const Theme& scheme : availableThemes())
			m_theme->addItem(scheme.name, scheme.name);

		const QString stored = m_config->read(S::ThemeName);
		m_theme->setCurrentIndex(qMax(0, m_theme->findData(stored.isEmpty() ? theme().name : stored)));
	});

	return page;
}

void Qdc::SettingsView::load()
{
	if (!m_config) return;

	m_loading = true;
	for (const auto& loader : m_loaders) loader();
	m_loading = false;

	applyConnectionMode();
	showPeerPort();
	m_localHubFields->setEnabled(m_localHub->isChecked());
}

void Qdc::SettingsView::refreshShareList()
{
	m_shareList->clear();
	if (!m_shares) return;

	for (const SharedFolder& folder : m_shares->folders())
	{
		QListWidgetItem* item = new QListWidgetItem(
			QStringLiteral("%1  —  %2").arg(folder.name, folder.path), m_shareList);

		/* A tooltip is drawn as markup, and a folder name with an angle bracket should read as typed. */
		item->setToolTip(RichText::literalHtml(tr("Shown to others as '%1'\n%2")
			.arg(folder.name, folder.path)));
	}
}

void Qdc::SettingsView::addShare()
{
	if (!m_shares) return;

	chooseDirectory(tr("Share a folder"), QDir::homePath(),
	                [this](const QString& chosen) { m_shares->add(chosen); });
}

void Qdc::SettingsView::removeSelectedShare()
{
	if (!m_shares) return;

	const int row = m_shareList->currentRow();
	if (row >= 0) m_shares->removeAt(row);
}

void Qdc::SettingsView::renameSelectedShare()
{
	if (!m_shares) return;

	const int row = m_shareList->currentRow();
	if (row < 0 || row >= m_shares->count()) return;

	const SharedFolder folder = m_shares->folders().at(row);

	bool accepted = false;
	const QString name = QInputDialog::getText(this, tr("Rename a shared folder"),
		tr("What other users see this folder as:"), QLineEdit::Normal, folder.name, &accepted);

	if (accepted) m_shares->renameAt(row, name);
}

void Qdc::SettingsView::chooseDirectory(const QString& title, const QString& start,
                                        std::function<void(const QString&)> chosen)
{
	/*
	 * On the next turn and drawn by Qt, as the composer's chooser is (see HubsView): opened
	 * inside the press it is dismissed by that press, and the platform's modal panel
	 * returns at once with nothing chosen.
	 */
	QTimer::singleShot(0, this, [this, title, start, chosen] {
		const QString directory = QFileDialog::getExistingDirectory(this, title, start,
			QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);

		if (!directory.isEmpty()) chosen(directory);
	});
}

void Qdc::SettingsView::chooseDownloadDirectory()
{
	const QString current = m_downloadDir->text().trimmed();

	chooseDirectory(tr("Save downloads to"), current.isEmpty() ? QDir::homePath() : current,
		[this](const QString& chosen) {
			m_downloadDir->setText(chosen);
			m_config->write(S::DownloadDirectory, chosen);
		});
}

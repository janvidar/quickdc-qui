/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "settingsview.h"

#include "theme.h"

#include <core/configbridge.h>
#include <core/shares.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace {

/* The groups and keys are those listed in quickdc/core/api/config.h. */
const QString USER = QStringLiteral("User");
const QString NETWORK = QStringLiteral("Network");
const QString DOWNLOADS = QStringLiteral("Downloads");
const QString CHAT_MEDIA = QStringLiteral("Chat media");

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

/** The core's own defaults, so an unset field shows what is really in force. */
constexpr int DEFAULT_PORT = 1411;
constexpr int DEFAULT_SLOTS = 3;
constexpr int DEFAULT_MAX_DOWNLOADS = 3;
constexpr int DEFAULT_CHUNK_KIB = 1024;

const QString DEFAULT_BIND = QStringLiteral("0.0.0.0");
const QString DEFAULT_DOWNLOAD_DIR = QStringLiteral("~/.quickdc/downloads");

/** Chunk size is stored in bytes and shown in KiB. */
constexpr int KIB = 1024;

}

Qdc::SettingsView::SettingsView(ConfigBridge* config, Shares* shares, QWidget* parent)
	: View(parent)
	, m_config(config)
	, m_shares(shares)
	, m_nickname(new QLineEdit(this))
	, m_description(new QLineEdit(this))
	, m_email(new QLineEdit(this))
	, m_clientId(new QLabel(this))
	, m_mode(new QComboBox(this))
	, m_port(new QSpinBox(this))
	, m_bind(new QLineEdit(this))
	, m_address(new QLineEdit(this))
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
	QWidget* page = new QWidget(this);

	QVBoxLayout* stacked = new QVBoxLayout(page);
	stacked->setContentsMargins(16, 16, 16, 16);
	stacked->setSpacing(14);
	stacked->addWidget(buildIdentity());
	stacked->addWidget(buildLimits());
	stacked->addWidget(buildDirectories());
	stacked->addWidget(buildAppearance());
	stacked->addStretch(1);

	QScrollArea* scroll = new QScrollArea(this);
	scroll->setWidget(page);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);

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
	layout->addWidget(scroll, 1);
	layout->addLayout(buttons);

	connect(m_save, &QPushButton::clicked, this, [this] { save(); });
	connect(m_revert, &QPushButton::clicked, this, &SettingsView::load);

	if (!m_config)
	{
		page->setEnabled(false);
		m_save->setEnabled(false);
		m_revert->setEnabled(false);
		m_status->setText(tr("The configuration is not available."));
		return;
	}

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
	QLabel* note = new QLabel(text, this);
	note->setWordWrap(true);

	note->setObjectName(QStringLiteral("mutedText"));

	QFont smaller = note->font();
	smaller.setPointSize(qMax(1, smaller.pointSize() - 1));
	note->setFont(smaller);

	return note;
}

QWidget* Qdc::SettingsView::buildIdentity()
{
	QGroupBox* group = new QGroupBox(tr("Identity"), this);

	m_nickname->setMaxLength(63);
	m_nickname->setPlaceholderText(tr("How other users see you"));

	m_description->setMaxLength(255);
	m_email->setMaxLength(127);

	m_clientId->setTextInteractionFlags(Qt::TextSelectableByMouse);

	QFont mono = m_clientId->font();
	mono.setFamily(QStringLiteral("monospace"));
	m_clientId->setFont(mono);

	/*
	 * Active or passive. The core reads this as a boolean; the words are the ones
	 * the protocol uses, because "Active mode: off" says less than "Passive".
	 */
	m_mode->addItem(tr("Active — others connect to me"), true);
	m_mode->addItem(tr("Passive — I connect out only"), false);

	m_port->setRange(1, 65535);
	m_bind->setPlaceholderText(DEFAULT_BIND);
	m_address->setPlaceholderText(tr("Detected automatically"));

	QFormLayout* form = new QFormLayout(group);
	form->addRow(tr("Nickname"), m_nickname);
	form->addRow(tr("Description"), m_description);
	form->addRow(tr("E-mail"), m_email);
	form->addRow(tr("Client ID"), m_clientId);
	form->addRow(tr("Connection"), m_mode);
	form->addRow(tr("Port"), m_port);
	form->addRow(tr("Bind to"), m_bind);
	form->addRow(tr("External address"), m_address);

	form->addRow(makeNote(tr(
		"The client tag hubs see is built from the version and this connection "
		"setting and cannot be changed. The client ID is derived from a private "
		"key and identifies this installation.")));

	connect(m_nickname, &QLineEdit::textEdited, this, &SettingsView::markModified);
	connect(m_description, &QLineEdit::textEdited, this, &SettingsView::markModified);
	connect(m_email, &QLineEdit::textEdited, this, &SettingsView::markModified);
	connect(m_bind, &QLineEdit::textEdited, this, &SettingsView::markModified);
	connect(m_address, &QLineEdit::textEdited, this, &SettingsView::markModified);
	connect(m_mode, &QComboBox::currentIndexChanged, this, &SettingsView::markModified);
	connect(m_port, &QSpinBox::valueChanged, this, &SettingsView::markModified);

	return group;
}

QWidget* Qdc::SettingsView::buildLimits()
{
	QGroupBox* group = new QGroupBox(tr("Limits"), this);

	m_slots->setRange(0, 999);
	m_slots->setToolTip(tr("How many users may download from you at once"));

	m_maxDownloads->setRange(1, 999);
	m_maxDownloads->setToolTip(tr("How many peers to fetch from at once"));

	m_chunkSize->setRange(64, 65536);
	m_chunkSize->setSingleStep(64);
	m_chunkSize->setSuffix(tr(" KiB"));
	m_chunkSize->setToolTip(tr("The least that is asked of one peer at a time; a "
	                           "peer that turns out to be fast is asked for more"));

	QFormLayout* form = new QFormLayout(group);
	form->addRow(tr("Upload slots"), m_slots);
	form->addRow(tr("Concurrent downloads"), m_maxDownloads);
	form->addRow(tr("Smallest chunk"), m_chunkSize);

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

	connect(m_slots, &QSpinBox::valueChanged, this, &SettingsView::markModified);
	connect(m_maxDownloads, &QSpinBox::valueChanged, this, &SettingsView::markModified);
	connect(m_chunkSize, &QSpinBox::valueChanged, this, &SettingsView::markModified);

	return group;
}

QWidget* Qdc::SettingsView::buildDirectories()
{
	QGroupBox* group = new QGroupBox(tr("Folders"), this);

	m_downloadDir->setPlaceholderText(DEFAULT_DOWNLOAD_DIR);

	QPushButton* browse = new QPushButton(tr("Browse…"), this);
	connect(browse, &QPushButton::clicked, this,
	        &SettingsView::chooseDownloadDirectory);

	QHBoxLayout* downloadRow = new QHBoxLayout;
	downloadRow->setContentsMargins(0, 0, 0, 0);
	downloadRow->addWidget(m_downloadDir, 1);
	downloadRow->addWidget(browse);

	m_shareList->setSelectionMode(QAbstractItemView::SingleSelection);

	/*
	 * Bounded at both ends. Without a maximum the form layout gives the list every
	 * spare pixel and pushes the notes under it off the bottom of the page.
	 */
	m_shareList->setMinimumHeight(96);
	m_shareList->setMaximumHeight(150);

	QPushButton* addShareButton = new QPushButton(tr("Add folder…"), this);
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

	m_fetchMedia = new QCheckBox(tr("Fetch pictures sent in chat automatically"), group);
	connect(m_fetchMedia, &QCheckBox::toggled, this, &SettingsView::markModified);
	m_fetchMedia->setToolTip(tr("Ask whoever sent a picture for the file, as soon as "
	                            "the message arrives"));

	m_mediaLimit = new QSpinBox(group);
	m_mediaLimit->setRange(0, 1024);
	m_mediaLimit->setSuffix(tr(" MiB"));
	m_mediaLimit->setSpecialValueText(tr("no limit"));
	m_mediaLimit->setToolTip(tr("Larger pictures are left for you to fetch by hand"));
	connect(m_mediaLimit, &QSpinBox::valueChanged, this, &SettingsView::markModified);

	m_mediaKeep = new QSpinBox(group);
	m_mediaKeep->setRange(0, 3650);
	m_mediaKeep->setSuffix(tr(" days"));
	m_mediaKeep->setSpecialValueText(tr("keep for ever"));
	m_mediaKeep->setToolTip(tr("Pictures you paste in are kept this long, in case "
	                           "somebody follows a link to one later"));
	connect(m_mediaKeep, &QSpinBox::valueChanged, this, &SettingsView::markModified);

	QFormLayout* form = new QFormLayout(group);
	form->addRow(tr("Save downloads to"), downloadRow);
	form->addRow(tr("Shared folders"), shareRow);
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

	/*
	 * Both notes are about the core rather than about this form, which is why they
	 * are here and not in a tooltip: a user who does not know either of these will
	 * think something is broken.
	 */
	form->addRow(makeNote(tr(
		"Shared folders are read when QuickDC starts, so a change here takes "
		"effect the next time it is started.\n\n"
		"An unfinished download is kept beside its final name with \".part\" "
		"added, so there is no separate folder for incomplete files.")));

	connect(m_downloadDir, &QLineEdit::textEdited, this, &SettingsView::markModified);

	return group;
}

QWidget* Qdc::SettingsView::buildAppearance()
{
	QGroupBox* group = new QGroupBox(tr("Appearance"), this);

	/*
	 * Filled in load() rather than here, so that Revert picks up a theme file that
	 * was written while the window was open.
	 */
	m_theme->setToolTip(tr("The colours the interface is drawn in"));

	connect(m_theme, &QComboBox::currentIndexChanged, this, &SettingsView::markModified);

	QFormLayout* form = new QFormLayout(group);
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

	return group;
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
		{QStringLiteral("Slots"), DEFAULT_SLOTS},
		{QStringLiteral("Max downloads"), DEFAULT_MAX_DOWNLOADS},
		{QStringLiteral("Chunk size"), DEFAULT_CHUNK_KIB * KIB}
	});

	m_mode->setCurrentIndex(network.value(QStringLiteral("Active mode")).toBool() ? 0 : 1);
	m_port->setValue(network.value(QStringLiteral("Port")).toInt());
	m_bind->setText(network.value(QStringLiteral("Bind")).toString());
	m_address->setText(network.value(QStringLiteral("Address")).toString());
	m_slots->setValue(network.value(QStringLiteral("Slots")).toInt());
	m_maxDownloads->setValue(network.value(QStringLiteral("Max downloads")).toInt());

	/* Stored in bytes, shown in KiB. */
	const int chunk = network.value(QStringLiteral("Chunk size")).toInt();
	m_chunkSize->setValue(qMax(m_chunkSize->minimum(), chunk / KIB));

	m_downloadDir->setText(m_config->getString(ConfigStore::Application, DOWNLOADS,
	                                           QStringLiteral("Directory")));

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

	m_config->setBool(ConfigStore::Application, NETWORK,
	                  QStringLiteral("Active mode"),
	                  m_mode->currentData().toBool());
	m_config->setNumber(ConfigStore::Application, NETWORK,
	                    QStringLiteral("Port"), m_port->value());
	m_config->setString(ConfigStore::Application, NETWORK,
	                    QStringLiteral("Bind"), m_bind->text().trimmed());
	m_config->setString(ConfigStore::Application, NETWORK,
	                    QStringLiteral("Address"), m_address->text().trimmed());

	m_config->setNumber(ConfigStore::Application, NETWORK,
	                    QStringLiteral("Slots"), m_slots->value());
	m_config->setNumber(ConfigStore::Application, NETWORK,
	                    QStringLiteral("Max downloads"), m_maxDownloads->value());
	m_config->setNumber(ConfigStore::Application, NETWORK,
	                    QStringLiteral("Chunk size"), m_chunkSize->value() * KIB);

	m_config->setString(ConfigStore::Application, DOWNLOADS,
	                    QStringLiteral("Directory"), m_downloadDir->text().trimmed());

	m_config->setBool(ConfigStore::Application, CHAT_MEDIA,
	                  QStringLiteral("Fetch automatically"), m_fetchMedia->isChecked());
	m_config->setNumber(ConfigStore::Application, CHAT_MEDIA,
	                    QStringLiteral("Maximum size"), m_mediaLimit->value() * MIB);
	m_config->setNumber(ConfigStore::Application, CHAT_MEDIA,
	                    QStringLiteral("Keep pasted for days"), m_mediaKeep->value());

	m_config->setString(ConfigStore::Application, INTERFACE, THEME_KEY,
	                    m_theme->currentData().toString());

	const bool written = m_config->save(ConfigStore::Application);

	if (!written)
	{
		m_status->setText(tr("The configuration could not be written."));
		return false;
	}

	/*
	 * Storing a nickname is not the same as using it: the hubs republish only when
	 * asked, and this is the ask.
	 */
	m_config->notifyChanged();

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

void Qdc::SettingsView::refreshShareList()
{
	m_shareList->clear();
	if (!m_shares) return;

	for (const SharedFolder& folder : m_shares->folders())
	{
		QListWidgetItem* item = new QListWidgetItem(
			QStringLiteral("%1  —  %2").arg(folder.name, folder.path), m_shareList);

		item->setToolTip(tr("Shown to others as '%1'\n%2")
			.arg(folder.name, folder.path));
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

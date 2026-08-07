/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "hubpane.h"

#include "composer.h"
#include "connectionbar.h"
#include "filelistview.h"
#include "hubheader.h"
#include "usermodel.h"

#include <core/configbridge.h>
#include <core/hubbridge.h>

#include <QClipboard>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMetaEnum>
#include <QListView>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

/** How wide the user list starts, and the least it can be dragged to. */
constexpr int USER_LIST_WIDTH = 190;
constexpr int USER_LIST_MINIMUM = 120;

/*
 * The identity keys, spelled as the core spells them.
 *
 * Per hub they live in session.conf under a group named by the hub's address, and
 * globally in quickdc.conf under [User] - the same four names in both, which is what
 * makes one a fallback for the other.
 */
const QString USER_GROUP = QStringLiteral("User");
const QString NICKNAME_KEY = QStringLiteral("Nickname");
const QString DESCRIPTION_KEY = QStringLiteral("Description");
const QString EMAIL_KEY = QStringLiteral("E-mail");
const QString PASSWORD_KEY = QStringLiteral("Password");

/**
 * Why the composer is not sendable, or empty when it is.
 *
 * Both halves matter. Reaching the hub is not the same as being logged in to it -
 * a hub that accepts the connection and then says nothing leaves the socket
 * connected for as long as one cares to wait - and a message sent in between is
 * refused by the core, which is no way to find out.
 */
QString status_reason(Qdc::NetStatus net, Qdc::HubStatus hub)
{
	switch (net)
	{
		case Qdc::NetStatus::Connected:
		case Qdc::NetStatus::TlsConnected:
			break;

		case Qdc::NetStatus::Lookup:
		case Qdc::NetStatus::Connecting:
			return QObject::tr("Connecting…");

		default:
			return QObject::tr("Not connected");
	}

	switch (hub)
	{
		case Qdc::HubStatus::LoggedIn:
			return QString();

		case Qdc::HubStatus::Password:
			return QObject::tr("Waiting for the password…");

		case Qdc::HubStatus::None:
		case Qdc::HubStatus::Login:
			return QObject::tr("Signing in…");

		default:
			return QObject::tr("The hub would not let us in");
	}
}

}

Qdc::HubPane::HubPane(HubBridge* hub, ConfigBridge* config, QWidget* parent)
	: ChatPane(hub, parent)
	, m_config(config)
	, m_users(new UserModel(this))
	, m_userList(new QListView(this))
	, m_info(new QLabel(this))
{
	header()->setTitle(hub ? hub->url() : tr("No hub"));
	header()->setUserListVisible(true);
	header()->setFavouriteVisible(true);

	m_userList->setObjectName(QStringLiteral("userList"));
	m_userList->setModel(m_users);
	m_userList->setUniformItemSizes(true);
	m_userList->setFrameShape(QFrame::NoFrame);
	m_userList->setSelectionMode(QAbstractItemView::SingleSelection);
	m_userList->setAccessibleName(tr("Users"));
	m_userList->setMinimumWidth(USER_LIST_MINIMUM);
	m_userList->setContextMenuPolicy(Qt::CustomContextMenu);

	connect(m_userList, &QListView::customContextMenuRequested,
	        this, &HubPane::showUserMenu);

	if (m_config)
	{
		connect(m_config, &ConfigBridge::saveFailed, this, [this](ConfigStore store) {
			if (store == ConfigStore::Session)
				m_sessionStatus->setText(tr("The session file could not be written."));
		});
	}

	/*
	 * The user list is the context sidebar from the layout. It belongs to this pane
	 * rather than to the window, because it is the only view that has one and its
	 * contents are hub specific.
	 */
	addSidebar(m_userList, USER_LIST_WIDTH);

	m_info->setWordWrap(true);
	m_info->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);

	/* Inset by the page below rather than by itself, so the form lines up with it. */
	m_info->setMargin(0);

	/*
	 * What the hub is, and then who we are on it. One page rather than two tabs:
	 * both are about this hub and neither is long enough to be looked for.
	 */
	QWidget* info = new QWidget(this);

	QVBoxLayout* stacked = new QVBoxLayout(info);
	stacked->setContentsMargins(12, 12, 12, 12);
	stacked->setSpacing(12);
	stacked->addWidget(m_info);
	stacked->addWidget(buildSessionForm());
	stacked->addStretch(1);

	QScrollArea* infoPage = new QScrollArea(this);
	infoPage->setWidget(info);
	infoPage->setWidgetResizable(true);
	infoPage->setFrameShape(QFrame::NoFrame);

	tabs()->addTab(infoPage, tr("Info"));

	connect(header(), &HubHeader::userListToggled, m_userList, &QWidget::setVisible);
	header()->setUserListShown(true);

	connect(header(), &HubHeader::favouriteToggled, this, [this](bool favourite) {
		emit favouriteToggled(this->hub() ? this->hub()->url() : QString(),
		                      favourite);
	});

	connect(m_users, &UserModel::countChanged, this, [this](int count) {
		header()->setUserCount(count);

		/* The mention list follows whoever is actually here. */
		composer()->setNicknames(m_users->nicknames());
		refreshInfo();
	});

	connect(m_userList, &QListView::activated, this, [this](const QModelIndex& index) {
		const UserInfo user = m_users->at(index.row());
		if (!user.id.isEmpty()) emit directMessageRequested(user);
	});

	if (!hub)
	{
		composer()->setSendable(false, tr("There is no hub."));
		refreshInfo();
		return;
	}

	/*
	 * The hub-wide state is subscribed to here rather than routed in from above,
	 * because this pane is the only thing that wants any of it.
	 */
	connect(hub, &HubBridge::userJoined, m_users, &UserModel::noteNickname);
	connect(hub, &HubBridge::userUpdated, m_users, &UserModel::upsert);
	connect(hub, &HubBridge::usersCleared, m_users, &UserModel::clear);

	connect(hub, &HubBridge::userLeft, this,
	        [this](const UserInfo& user, const QString&, bool) {
		m_users->remove(user.id);
	});

	connect(hub, &HubBridge::hubNameChanged, this, [this](const QString& name) {
		header()->setSubtitle(name);
		refreshInfo();
	});

	connect(hub, &HubBridge::secureChanged, this, [this](bool secure) {
		header()->setSecure(secure);
		refreshInfo();
	});

	connect(hub, &HubBridge::netStatusChanged, this, &HubPane::applyNetStatus);

	connect(hub, &HubBridge::reconnectScheduled,
	        connectionBar(), &ConnectionBar::setReconnectIn);

	/*
	 * The band offers to stop waiting; this is what it means. connectToHub() is
	 * the same call that opened the hub, and the core drops the attempt it had
	 * pending when a connection is asked for.
	 */
	connect(connectionBar(), &ConnectionBar::reconnectRequested, this, [hub] {
		hub->connectToHub();
	});

	connect(hub, &HubBridge::hubStatusChanged, this, [this](HubStatus status) {
		/* Logging in is half of whether a message can be sent; see status_reason. */
		m_hubStatus = status;
		applySendable();
		applyBand();
		refreshInfo();
	});

	header()->setSecure(hub->isSecure());
	m_hubStatus = hub->hubStatus();
	applyNetStatus(hub->netStatus());
	refreshInfo();
	loadSession();
}

QString Qdc::HubPane::paneTitle() const
{
	const QString name = hub() ? hub()->hubName() : QString();
	if (!name.isEmpty()) return name;

	return hub() ? hub()->url() : tr("No hub");
}

Qdc::UserModel* Qdc::HubPane::users() const
{
	return m_users;
}

void Qdc::HubPane::setFavourite(bool favourite)
{
	header()->setFavourite(favourite);
}

void Qdc::HubPane::sendMessage(const QString& message, bool richText)
{
	if (hub()) hub()->sendChat(message, richText);
}

void Qdc::HubPane::applyNetStatus(NetStatus status)
{
	m_netStatus = status;
	header()->setNetStatus(status);
	applySendable();
	applyBand();
	refreshInfo();
}

void Qdc::HubPane::applyBand()
{
	connectionBar()->setStatus(m_netStatus, m_hubStatus);
}

void Qdc::HubPane::applySendable()
{
	if (!hub()) return;

	const QString reason = status_reason(m_netStatus, m_hubStatus);
	composer()->setSendable(reason.isEmpty(), reason);
}

QWidget* Qdc::HubPane::buildSessionForm()
{
	QGroupBox* group = new QGroupBox(tr("Who you are on this hub"), this);

	/*
	 * Named, so that the field for a key can be found without relying on the order
	 * they were built in - which is how a test reaches them, the members being this
	 * pane's own business.
	 */
	m_nickname = new QLineEdit(group);
	m_nickname->setObjectName(QStringLiteral("sessionNickname"));
	m_nickname->setMaxLength(63);

	m_description = new QLineEdit(group);
	m_description->setObjectName(QStringLiteral("sessionDescription"));
	m_description->setMaxLength(255);

	m_email = new QLineEdit(group);
	m_email->setObjectName(QStringLiteral("sessionEmail"));
	m_email->setMaxLength(127);

	m_password = new QLineEdit(group);
	m_password->setObjectName(QStringLiteral("sessionPassword"));
	m_password->setEchoMode(QLineEdit::Password);
	m_password->setMaxLength(127);

	/* The object name is the stylesheet's, so a test finds this by its text. */
	m_saveSession = new QPushButton(tr("Save"), group);
	m_saveSession->setObjectName(QStringLiteral("primaryButton"));
	m_saveSession->setEnabled(false);

	m_sessionStatus = new QLabel(group);
	m_sessionStatus->setObjectName(QStringLiteral("mutedText"));
	m_sessionStatus->setWordWrap(true);

	QFormLayout* form = new QFormLayout(group);
	form->addRow(tr("Nickname"), m_nickname);
	form->addRow(tr("Description"), m_description);
	form->addRow(tr("E-mail"), m_email);
	form->addRow(tr("Password"), m_password);

	/*
	 * Said rather than left to be worked out, because an empty field here means
	 * something: the hub gets whatever Settings says, and clearing one is how a hub
	 * is handed back to the general identity rather than given an empty name.
	 */
	QLabel* note = new QLabel(tr(
		"Left empty, a field falls back to the identity in Settings — which is what "
		"the greyed text in each one shows. A password is kept in the clear in "
		"session.conf, as the rest of this is."), group);

	note->setWordWrap(true);
	note->setObjectName(QStringLiteral("mutedText"));

	QFont smaller = note->font();
	smaller.setPointSize(qMax(1, smaller.pointSize() - 1));
	note->setFont(smaller);

	form->addRow(note);

	QHBoxLayout* buttons = new QHBoxLayout;
	buttons->setContentsMargins(0, 0, 0, 0);
	buttons->addWidget(m_sessionStatus, 1);
	buttons->addWidget(m_saveSession);

	form->addRow(buttons);

	for (QLineEdit* field : {m_nickname, m_description, m_email, m_password})
		connect(field, &QLineEdit::textEdited, this, &HubPane::markSessionEdited);

	connect(m_saveSession, &QPushButton::clicked, this, &HubPane::saveSession);

	if (!m_config || !hub())
	{
		group->setEnabled(false);
		m_sessionStatus->setText(tr("The configuration is not available."));
	}

	return group;
}

void Qdc::HubPane::loadSession()
{
	if (!m_config || !hub() || !m_nickname) return;

	m_loadingSession = true;

	/* Canonical, so a keyprint in the address does not split the settings in two. */
	const QString group = HubBridge::settingsUrl(hub()->url());

	/*
	 * A group at a time would be better, as the settings page does it, but these are
	 * two groups rather than one - the hub's own and the global one it falls back to -
	 * and the placeholders need both.
	 */
	const QVariantHash mine = m_config->readGroup(ConfigStore::Session, group, {
		{NICKNAME_KEY, QString()},
		{DESCRIPTION_KEY, QString()},
		{EMAIL_KEY, QString()},
		{PASSWORD_KEY, QString()}
	});

	const QVariantHash general = m_config->readGroup(ConfigStore::Application,
	                                                 USER_GROUP, {
		{NICKNAME_KEY, QString()},
		{DESCRIPTION_KEY, QString()},
		{EMAIL_KEY, QString()}
	});

	m_nickname->setText(mine.value(NICKNAME_KEY).toString());
	m_description->setText(mine.value(DESCRIPTION_KEY).toString());
	m_email->setText(mine.value(EMAIL_KEY).toString());
	m_password->setText(mine.value(PASSWORD_KEY).toString());

	m_nickname->setPlaceholderText(general.value(NICKNAME_KEY).toString());
	m_description->setPlaceholderText(general.value(DESCRIPTION_KEY).toString());
	m_email->setPlaceholderText(general.value(EMAIL_KEY).toString());
	m_password->setPlaceholderText(tr("none"));

	m_loadingSession = false;

	m_saveSession->setEnabled(false);
	m_sessionStatus->clear();
}

void Qdc::HubPane::markSessionEdited()
{
	if (m_loadingSession || !m_saveSession) return;

	m_saveSession->setEnabled(true);
	m_sessionStatus->setText(tr("Unsaved changes."));
}

void Qdc::HubPane::saveSession()
{
	if (!m_config || !hub()) return;

	/* Canonical, so a keyprint in the address does not split the settings in two. */
	const QString group = HubBridge::settingsUrl(hub()->url());

	const struct { const QString& key; QLineEdit* field; } fields[] = {
		{ NICKNAME_KEY, m_nickname },
		{ DESCRIPTION_KEY, m_description },
		{ EMAIL_KEY, m_email },
		{ PASSWORD_KEY, m_password }
	};

	for (const auto& entry : fields)
	{
		const QString value = entry.field->text().trimmed();

		/*
		 * Removed rather than stored empty, and the difference is the whole point of
		 * the note under the form: the core falls back to the global identity only
		 * for a key that is absent, and reads a key that is present and empty as an
		 * empty name.
		 */
		if (value.isEmpty())
			m_config->remove(ConfigStore::Session, group, entry.key);
		else
			m_config->setString(ConfigStore::Session, group, entry.key, value);
	}

	/*
	 * Said before the file has been written, because the write happens a moment
	 * later on the core's own thread and waiting for its answer is the freeze this
	 * form used to be. A write that fails says so through saveFailed(), which is
	 * connected where the form is built.
	 */
	m_config->save(ConfigStore::Session);

	m_saveSession->setEnabled(false);
	m_sessionStatus->setText(tr("Saved."));

	/*
	 * A session reads its identity when it is built and never again, so this changes
	 * nothing about the connection that is up. Offered rather than done, because
	 * reconnecting drops out of the hub and anybody in the middle of a conversation
	 * should be the one to decide that.
	 */
	const bool live = hub()->netStatus() == NetStatus::Connected
		|| hub()->netStatus() == NetStatus::TlsConnected;

	if (!live) return;

	const auto answer = QMessageBox::question(this, tr("Reconnect"),
		tr("Saved. This hub already knows you by the old identity, and will keep "
		   "using it until the next connection.\n\nReconnect now?"),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

	if (answer != QMessageBox::Yes) return;

	hub()->disconnectFromHub();
	hub()->connectToHub();
}

void Qdc::HubPane::showUserMenu(const QPoint& where)
{
	const QModelIndex index = m_userList->indexAt(where);
	if (!index.isValid()) return;

	const UserInfo user = m_users->at(index.row());
	if (user.nick.isEmpty() && user.id.isEmpty()) return;

	QMenu menu(this);

	/*
	 * The nickname across the top, disabled. Which user a menu is about is otherwise
	 * only implied by where it opened, and a menu that opened over the wrong row is
	 * exactly the mistake worth making impossible to miss.
	 */
	QAction* who = menu.addAction(user.nick.isEmpty() ? tr("This user") : user.nick);
	who->setEnabled(false);
	menu.addSeparator();

	QAction* talk = menu.addAction(tr("Send a private message"));
	QAction* files = menu.addAction(tr("Browse files"));
	menu.addSeparator();
	QAction* copy = menu.addAction(tr("Copy nickname"));

	/* A private message is routed by the session id, and a browse by the source id. */
	talk->setEnabled(!user.id.isEmpty());
	files->setEnabled(!user.id.isEmpty() && !user.sourceId.isEmpty());

	if (user.sourceId.isEmpty())
		files->setToolTip(tr("The hub has not said enough about them to ask yet."));

	copy->setEnabled(!user.nick.isEmpty());

	QAction* picked = menu.exec(m_userList->viewport()->mapToGlobal(where));

	if (picked == talk)
	{
		emit directMessageRequested(user);
	}
	else if (picked == files)
	{
		emit filesRequested(user);
	}
	else if (picked == copy)
	{
		if (QClipboard* board = QGuiApplication::clipboard())
			board->setText(user.nick);
	}
}

void Qdc::HubPane::refreshInfo()
{
	if (!hub())
	{
		m_info->setText(tr("There is no hub."));
		return;
	}

	const QMetaEnum net = QMetaEnum::fromType<NetStatus>();
	const QMetaEnum state = QMetaEnum::fromType<HubStatus>();

	QStringList rows;

	rows << tr("<b>Address</b><br>%1").arg(hub()->url().toHtmlEscaped());

	if (!hub()->hubName().isEmpty())
		rows << tr("<b>Name</b><br>%1").arg(hub()->hubName().toHtmlEscaped());

	rows << tr("<b>Users</b><br>%1").arg(m_users->count());

	rows << tr("<b>Encryption</b><br>%1").arg(hub()->isSecure()
		? tr("TLS") : tr("None"));

	/*
	 * The enumerator names, by way of Q_ENUM_NS - which is the reason the bridge
	 * mirrors the core's enums rather than passing the raw values through.
	 */
	rows << tr("<b>Network</b><br>%1")
		.arg(QString::fromUtf8(net.valueToKey(static_cast<int>(hub()->netStatus()))));

	rows << tr("<b>Session</b><br>%1")
		.arg(QString::fromUtf8(state.valueToKey(static_cast<int>(hub()->hubStatus()))));

	quint64 shared = 0;
	for (int n = 0; n < m_users->count(); ++n) shared += m_users->at(n).sharedBytes;

	if (shared > 0)
	{
		rows << tr("<b>Shared here</b><br>%1")
			.arg(QLocale().formattedDataSize(static_cast<qint64>(shared)));
	}

	m_info->setText(rows.join(QStringLiteral("<br><br>")));
}

Qdc::DmPane::DmPane(HubBridge* hub, const UserInfo& user,
                    FilelistBridge* filelists, QWidget* parent)
	: ChatPane(hub, parent)
	, m_user(user)
	, m_files(new FilelistView(filelists, hub ? hub->url() : QString(),
	                           user.sourceId, this))
{
	/* No star and no user list: a conversation with one person has neither. */
	header()->setFavouriteVisible(false);
	header()->setUserListVisible(false);

	tabs()->addTab(m_files, tr("Files"));

	connect(m_files, &FilelistView::fileActivated,
	        this, &DmPane::downloadRequested);

	setUser(user);

	if (!hub)
	{
		composer()->setSendable(false, tr("There is no hub."));
		return;
	}

	connect(hub, &HubBridge::netStatusChanged, this, [this](NetStatus status) {
		m_netStatus = status;
		applySendable();
		applyBand();
	});

	connect(hub, &HubBridge::hubStatusChanged, this, [this](HubStatus status) {
		m_hubStatus = status;
		applySendable();
		applyBand();
	});

	/*
	 * The same band as the hub's own pane gets: a private message goes through the
	 * hub, so a conversation with one person is as unusable as the hub is.
	 */
	connect(hub, &HubBridge::reconnectScheduled,
	        connectionBar(), &ConnectionBar::setReconnectIn);

	/*
	 * The band offers to stop waiting; this is what it means. connectToHub() is
	 * the same call that opened the hub, and the core drops the attempt it had
	 * pending when a connection is asked for.
	 */
	connect(connectionBar(), &ConnectionBar::reconnectRequested, this, [hub] {
		hub->connectToHub();
	});

	/*
	 * Who we are talking to, followed for as long as this is open. A message can only
	 * be sent to somebody the hub will relay it to, so a conversation with somebody
	 * who has gone is closed for writing until they come back.
	 */
	connect(hub, &HubBridge::userLeft, this,
	        [this](const UserInfo& user, const QString&, bool) {
		if (isWith(user)) setPresent(false, m_user.nick);
	});

	connect(hub, &HubBridge::userUpdated, this, [this](const UserInfo& user) {
		if (!isWith(user)) return;

		const QString was = m_user.id;
		const QString wasCalled = m_user.nick;

		setUser(user);

		/*
		 * A new session id for the same client ID is them having come back, and the
		 * conversation is filed by that id - so whoever holds it has to be told.
		 */
		if (!was.isEmpty() && !user.id.isEmpty() && user.id != was)
			emit contextChanged(was, user.id);

		setPresent(true, wasCalled);
	});

	/*
	 * The session ended, so nothing is known about anybody any more. Said as an
	 * absence rather than left alone: the hub coming back re-announces everyone, and
	 * until it has there is no one to write to.
	 */
	connect(hub, &HubBridge::usersCleared, this, [this] {
		setPresent(false, m_user.nick);
	});

	m_netStatus = hub->netStatus();
	m_hubStatus = hub->hubStatus();
	applySendable();
	applyBand();

	/* Mentioning the other party is the only completion that makes sense here. */
	composer()->setNicknames({m_user.nick});
}

void Qdc::DmPane::applyBand()
{
	connectionBar()->setAbsent(m_present ? QString() : m_user.nick);
	connectionBar()->setStatus(m_netStatus, m_hubStatus);
}

void Qdc::DmPane::applySendable()
{
	QString reason = status_reason(m_netStatus, m_hubStatus);

	/*
	 * The hub being usable is not enough here: a private message is relayed to one
	 * person, and a hub cannot relay it to somebody who is not on it. Said after the
	 * connection reasons, which are the more fundamental ones - there is no point
	 * saying who has left while the hub itself is unreachable.
	 */
	if (reason.isEmpty() && !m_present)
	{
		reason = m_user.nick.isEmpty()
			? tr("They are no longer on this hub")
			: tr("%1 is no longer on this hub").arg(m_user.nick);
	}

	composer()->setSendable(reason.isEmpty(), reason);
}

bool Qdc::DmPane::isWith(const UserInfo& user) const
{
	if (!m_user.sourceId.isEmpty() && !user.sourceId.isEmpty())
		return user.sourceId == m_user.sourceId;

	return !m_user.id.isEmpty() && user.id == m_user.id;
}

void Qdc::DmPane::setPresent(bool present, const QString& nickname)
{
	const bool changed = m_present != present;
	m_present = present;

	applySendable();
	applyBand();

	if (!changed) return;

	/*
	 * Written into the conversation as well as shown across the top, because the band
	 * says what is true now and this is a record of when it changed - which is what
	 * somebody scrolling back through the morning is reading.
	 */
	const QString who = nickname.isEmpty() ? tr("They") : nickname;

	if (!present)
	{
		postNotice(tr("%1 has left the hub.").arg(who));
		return;
	}

	postNotice(m_user.nick == nickname
		? tr("%1 is back.").arg(who)
		: tr("%1 is back, now called %2.").arg(who, m_user.nick));
}

QString Qdc::DmPane::paneTitle() const
{
	return m_user.nick.isEmpty() ? m_user.id : m_user.nick;
}

QString Qdc::DmPane::context() const
{
	return m_user.id;
}

QString Qdc::DmPane::peerId() const
{
	return m_user.sourceId.isEmpty() ? m_user.id : m_user.sourceId;
}

void Qdc::DmPane::restoreOffline()
{
	/*
	 * The field rather than setPresent(), which writes the change into the
	 * conversation - and there is no change here, only a conversation being put
	 * back the way it was left.
	 */
	m_present = false;

	applySendable();
	applyBand();
}

void Qdc::DmPane::showFiles()
{
	/* By widget rather than by index: the base class owns the tabs before this one. */
	const int index = tabs()->indexOf(m_files);
	if (index >= 0) tabs()->setCurrentIndex(index);
}

void Qdc::DmPane::setUser(const UserInfo& user)
{
	/*
	 * A record about somebody else is ignored rather than applied. Which records are
	 * about this conversation is isWith()'s judgement, and it is deliberately not
	 * "the same session id": somebody who leaves and comes back is the same person
	 * under a new session, and often under a new nickname, which is precisely the
	 * case this conversation has to follow rather than lose.
	 */
	if (!m_user.id.isEmpty() && !user.id.isEmpty() && !isWith(user)) return;

	m_user = user;

	header()->setTitle(paneTitle());
	header()->setSubtitle(user.description);

	m_files->setSource(hub() ? hub()->url() : QString(), user.sourceId);
	composer()->setNicknames({m_user.nick});
}

void Qdc::DmPane::sendMessage(const QString& message, bool richText)
{
	if (hub()) hub()->sendPrivateChat(m_user.id, message, richText);
}

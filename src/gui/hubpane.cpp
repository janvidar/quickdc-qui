/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "hubpane.h"

#include "composer.h"
#include "filelistview.h"
#include "hubheader.h"
#include "usermodel.h"

#include <core/hubbridge.h>

#include <QLabel>
#include <QMetaEnum>
#include <QListView>
#include <QLocale>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

/** How wide the user list starts, and the least it can be dragged to. */
constexpr int USER_LIST_WIDTH = 190;
constexpr int USER_LIST_MINIMUM = 120;

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

Qdc::HubPane::HubPane(HubBridge* hub, QWidget* parent)
	: ChatPane(hub, parent)
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

	/*
	 * The user list is the context sidebar from the layout. It belongs to this pane
	 * rather than to the window, because it is the only view that has one and its
	 * contents are hub specific.
	 */
	addSidebar(m_userList, USER_LIST_WIDTH);

	m_info->setWordWrap(true);
	m_info->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_info->setMargin(12);

	QScrollArea* infoPage = new QScrollArea(this);
	infoPage->setWidget(m_info);
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

	connect(hub, &HubBridge::hubStatusChanged, this, [this](HubStatus status) {
		/* Logging in is half of whether a message can be sent; see status_reason. */
		m_hubStatus = status;
		applySendable();
		refreshInfo();
	});

	header()->setSecure(hub->isSecure());
	m_hubStatus = hub->hubStatus();
	applyNetStatus(hub->netStatus());
	refreshInfo();
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

void Qdc::HubPane::sendMessage(const QString& message)
{
	if (hub()) hub()->sendChat(message);
}

void Qdc::HubPane::applyNetStatus(NetStatus status)
{
	m_netStatus = status;
	header()->setNetStatus(status);
	applySendable();
	refreshInfo();
}

void Qdc::HubPane::applySendable()
{
	if (!hub()) return;

	const QString reason = status_reason(m_netStatus, m_hubStatus);
	composer()->setSendable(reason.isEmpty(), reason);
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
	});

	connect(hub, &HubBridge::hubStatusChanged, this, [this](HubStatus status) {
		m_hubStatus = status;
		applySendable();
	});

	m_netStatus = hub->netStatus();
	m_hubStatus = hub->hubStatus();
	applySendable();

	/* Mentioning the other party is the only completion that makes sense here. */
	composer()->setNicknames({m_user.nick});
}

void Qdc::DmPane::applySendable()
{
	const QString reason = status_reason(m_netStatus, m_hubStatus);
	composer()->setSendable(reason.isEmpty(), reason);
}

QString Qdc::DmPane::paneTitle() const
{
	return m_user.nick.isEmpty() ? m_user.id : m_user.nick;
}

QString Qdc::DmPane::context() const
{
	return m_user.id;
}

void Qdc::DmPane::setUser(const UserInfo& user)
{
	/*
	 * The id is what the conversation is keyed by, so a record that disagrees is
	 * about somebody else and is ignored rather than applied.
	 */
	if (!m_user.id.isEmpty() && !user.id.isEmpty() && user.id != m_user.id) return;

	m_user = user;

	header()->setTitle(paneTitle());
	header()->setSubtitle(user.description);

	m_files->setSource(hub() ? hub()->url() : QString(), user.sourceId);
	composer()->setNicknames({m_user.nick});
}

void Qdc::DmPane::sendMessage(const QString& message)
{
	if (hub()) hub()->sendPrivateChat(m_user.id, message);
}

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "hubbridge.h"

#include "corechannel.h"

#include <api/huburl.h>
#include "corethread.h"


#include <QMetaObject>
#include <QUrl>

namespace {

/** How many replies travel together, and how long the first of them waits. */
constexpr int HIT_BATCH = 64;
constexpr int HIT_BATCH_MS = 50;


/**
 * Whether a network status means the session is over.
 *
 * Asked so that what a hub agreed to does not outlive the connection it was agreed on:
 * the next one negotiates again, and until it has, nothing is known.
 */
bool session_ended(Qdc::NetStatus status)
{
	switch (status)
	{
		case Qdc::NetStatus::Disconnected:
		case Qdc::NetStatus::TlsDisconnected:
		case Qdc::NetStatus::ErrorTimeout:
		case Qdc::NetStatus::ErrorConnectionRefused:
		case Qdc::NetStatus::ErrorHostNotFound:
		case Qdc::NetStatus::ErrorHostUnreachable:
		case Qdc::NetStatus::ErrorIO:
		case Qdc::NetStatus::TlsError:
			return true;

		default:
			return false;
	}
}

/**
 * How long a hub has to log us in before we say something.
 *
 * Generous: a login is a handful of round trips and is normally done inside a
 * second, but a hub behind a slow link and a TLS handshake is still a hub. This is
 * long enough that no working hub trips it and short enough to be an answer rather
 * than a wait.
 */
constexpr int LOGIN_TIMEOUT_MS = 20000;

QString from(const std::string& text)
{
	return QString::fromStdString(text);
}

Qdc::NetStatus convert(QuickDC::Api::NetStatus status)
{
	switch (status)
	{
		case QuickDC::Api::NetStatus::Invalid:                return Qdc::NetStatus::Invalid;
		case QuickDC::Api::NetStatus::Lookup:                 return Qdc::NetStatus::Lookup;
		case QuickDC::Api::NetStatus::Connecting:             return Qdc::NetStatus::Connecting;
		case QuickDC::Api::NetStatus::Connected:              return Qdc::NetStatus::Connected;
		case QuickDC::Api::NetStatus::Disconnected:           return Qdc::NetStatus::Disconnected;
		case QuickDC::Api::NetStatus::TlsConnected:           return Qdc::NetStatus::TlsConnected;
		case QuickDC::Api::NetStatus::TlsDisconnected:        return Qdc::NetStatus::TlsDisconnected;
		case QuickDC::Api::NetStatus::ErrorTimeout:           return Qdc::NetStatus::ErrorTimeout;
		case QuickDC::Api::NetStatus::ErrorConnectionRefused: return Qdc::NetStatus::ErrorConnectionRefused;
		case QuickDC::Api::NetStatus::ErrorHostNotFound:      return Qdc::NetStatus::ErrorHostNotFound;
		case QuickDC::Api::NetStatus::ErrorHostUnreachable:   return Qdc::NetStatus::ErrorHostUnreachable;
		case QuickDC::Api::NetStatus::ErrorIO:                return Qdc::NetStatus::ErrorIO;
		case QuickDC::Api::NetStatus::TlsError:               return Qdc::NetStatus::TlsError;
	}
	return Qdc::NetStatus::Invalid;
}

Qdc::HubStatus convert(QuickDC::Api::HubStatus status)
{
	switch (status)
	{
		case QuickDC::Api::HubStatus::None:                 return Qdc::HubStatus::None;
		case QuickDC::Api::HubStatus::Login:                return Qdc::HubStatus::Login;
		case QuickDC::Api::HubStatus::Password:             return Qdc::HubStatus::Password;
		case QuickDC::Api::HubStatus::LoggedIn:             return Qdc::HubStatus::LoggedIn;
		case QuickDC::Api::HubStatus::ErrorLoginRefused:    return Qdc::HubStatus::ErrorLoginRefused;
		case QuickDC::Api::HubStatus::ErrorFull:            return Qdc::HubStatus::ErrorFull;
		case QuickDC::Api::HubStatus::ErrorDisabled:        return Qdc::HubStatus::ErrorDisabled;
		case QuickDC::Api::HubStatus::ErrorNickNotAccepted: return Qdc::HubStatus::ErrorNickNotAccepted;
		case QuickDC::Api::HubStatus::ErrorNickTaken:       return Qdc::HubStatus::ErrorNickTaken;
		case QuickDC::Api::HubStatus::ErrorCidTaken:        return Qdc::HubStatus::ErrorCidTaken;
		case QuickDC::Api::HubStatus::ErrorWrongPassword:   return Qdc::HubStatus::ErrorWrongPassword;
		case QuickDC::Api::HubStatus::Kicked:               return Qdc::HubStatus::Kicked;
		case QuickDC::Api::HubStatus::BannedTemporarily:    return Qdc::HubStatus::BannedTemporarily;
		case QuickDC::Api::HubStatus::BannedPermanently:    return Qdc::HubStatus::BannedPermanently;
		case QuickDC::Api::HubStatus::Redirected:           return Qdc::HubStatus::Redirected;
	}
	return Qdc::HubStatus::None;
}

QDateTime stamped(uint64_t timestamp)
{
	return timestamp ? QDateTime::fromSecsSinceEpoch(static_cast<qint64>(timestamp))
	                 : QDateTime::currentDateTime();
}

Qdc::UserInfo convert(const QuickDC::Api::User& user)
{
	Qdc::UserInfo info;
	info.id           = from(user.id);
	info.nick         = from(user.nick);
	info.description  = from(user.description);
	info.email        = from(user.email);
	info.speed        = from(user.speed);
	info.userAgent    = from(user.userAgent);
	info.addressIPv4  = from(user.addressIPv4);
	info.addressIPv6  = from(user.addressIPv6);
	info.keyprint     = from(user.keyprint);
	info.sourceId     = from(user.sourceId);
	info.sharedBytes  = user.sharedBytes;
	info.sharedFiles  = user.sharedFiles;
	info.slotCount    = user.slotCount;
	info.op           = user.op;
	info.away         = user.away;
	return info;
}

Qdc::ChatMessage convert(const QuickDC::Api::ChatLine& line)
{
	Qdc::ChatMessage chat;
	chat.hubUrl         = from(line.hubUrl);
	chat.from           = convert(line.from);
	chat.fromHub        = from(line.fromHub);
	chat.text           = from(line.text);
	chat.context        = from(line.context);
	chat.received       = stamped(line.timestamp);
	chat.action         = line.action;
	chat.richText       = line.richText;
	chat.privateMessage = line.privateMessage;
	return chat;
}

Qdc::SearchHit convert(const QuickDC::Api::SearchHit& found)
{
	Qdc::SearchHit hit;
	hit.hubUrl    = from(found.hubUrl);
	hit.name      = from(found.name);
	hit.tth       = from(found.tth);
	hit.size      = found.size;
	hit.freeSlots = found.freeSlots;
	hit.user      = convert(found.user);
	hit.directory = hit.name.endsWith(QLatin1Char('/'));
	return hit;
}

QString normalise_url(const QString& url)
{
	return QString::fromStdString(QuickDC::canonicalHubUrl(url.toStdString()));
}

}

Qdc::HubBridge::HubBridge(CoreThread* thread, const QString& url, QObject* parent, bool remember)
	: QObject(parent)
	, m_thread(thread)
	, m_url(normalise_url(url))
{
	static QuickDC::Api::HubHandle handles = 0;
	m_handle = ++handles;

	startLoginWatch();

	if (!m_thread || !m_thread->isCoreRunning())
	{
		QMetaObject::invokeMethod(this, [this] {
			emit systemError(tr("The QuickDC core is not running."));
		}, Qt::QueuedConnection);
		return;
	}

	if (m_url.isEmpty())
	{
		QMetaObject::invokeMethod(this, [this] {
			emit systemError(tr("No hub address was given."));
		}, Qt::QueuedConnection);
		return;
	}

	connect(m_thread->channel()->route(m_handle), &HubRoute::received, this, &HubBridge::onCoreEvent);

	m_opened = m_thread->submit(QuickDC::Api::Commands::OpenHub{m_handle, m_url.toStdString(), remember});
	if (m_opened) sendPolicy();
}

/* Closing disconnects the session; nothing of it is left to report here. */
Qdc::HubBridge::~HubBridge()
{
	if (m_opened && m_thread)
		m_thread->submit(QuickDC::Api::Commands::CloseHub{m_handle});

	if (m_thread) m_thread->channel()->releaseRoute(m_handle);
}

void Qdc::HubBridge::sendPolicy()
{
	if (!m_opened || !m_thread) return;

	m_thread->submit(QuickDC::Api::Commands::SetHubPolicy{
		m_handle, m_followRedirects, m_allowPeerConnections});
}


template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::HubOpenFailed&)
{
	m_opened = false;
	emit systemError(tr("'%1' is not an address I can use.").arg(m_url));
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::HubNetStatus& e)
{
	const NetStatus status = convert(e.status);
	m_netStatus = status;

	if (e.secure != m_secure)
	{
		m_secure = e.secure;
		emit secureChanged(m_secure);
	}

	/* What a hub offered lasts only as long as the session it offered it in. */
	if (session_ended(status) && m_richText)
	{
		m_richText = false;
		emit richTextSupportChanged(false);
	}

	emit netStatusChanged(status);
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::HubReconnectScheduled& e)
{
	emit reconnectScheduled(e.seconds);
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::HubStatusChanged& e)
{
	m_hubStatus = convert(e.status);
	emit hubStatusChanged(m_hubStatus);
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::HubExtensions& e)
{
	if (e.richText == m_richText) return;
	m_richText = e.richText;
	emit richTextSupportChanged(m_richText);
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::HubName& e)
{
	const QString name = from(e.name);
	if (name == m_hubName) return;
	m_hubName = name;
	emit hubNameChanged(name);
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::HubDescription& e)
{
	const QString text = from(e.description);
	if (text == m_hubDescription) return;
	m_hubDescription = text;
	emit hubDescriptionChanged(text);
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::HubSystemError& e)
{
	emit systemError(from(e.message));
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::KickRefused& e)
{
	using Why = QuickDC::Api::Events::KickRefused::Why;
	switch (e.why)
	{
		case Why::NoSession:   emit systemError(tr("Not logged in to this hub.")); return;
		case Why::NoSuchUser:  emit systemError(tr("That user is not on this hub.")); return;
		case Why::NotOperator: emit systemError(tr("Only an operator of this hub can do that.")); return;
		case Why::Unsupported: emit systemError(tr("This hub cannot be asked to ban.")); return;
	}
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::HubAuthenticate&)
{
	emit authenticationRequired();
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::HubRedirect& e)
{
	/* The core reconnects this same handle there, and what it says next names that hub. */
	if (e.followed && !e.url.empty()) m_url = from(e.url);
	emit redirectRequested(from(e.address), e.port, e.followed);
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::ChatReceived& e)
{
	emit chatReceived(convert(e.line));
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::HistoryRestored& e)
{
	QVector<ChatMessage> lines;
	lines.reserve(static_cast<qsizetype>(e.lines.size()));
	for (const QuickDC::Api::ChatLine& line : e.lines) lines.append(convert(line));

	emit historyRestored(from(e.conversation), lines);
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::UserJoined& e)
{
	emit userJoined(from(e.nick));
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::UserLeft& e)
{
	emit userLeft(convert(e.user), from(e.message), e.disconnected);
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::UserUpdated& e)
{
	emit userUpdated(convert(e.user));
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::UsersCleared&)
{
	emit usersCleared();
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::SearchResult& e)
{
	SearchHit hit = convert(e.hit);
	hit.search = e.search;
	collect(hit);
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::ChatMedia& e)
{
	if (e.state == QuickDC::Api::Events::ChatMedia::State::Ready)
		emit chatMediaReady(from(e.tth), from(e.path));
}

/* The core follows a hub's boards itself; what is drawn of them comes through BoardBridge. */
template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::BoardsAvailable&)
{
}

template<>
void Qdc::HubBridge::handle(const QuickDC::Api::Events::BoardRefused& e)
{
	emit boardRefused(e.code, from(e.message), from(e.board), from(e.tth));
}

void Qdc::HubBridge::onCoreEvent(const QuickDC::Api::Event& event)
{
	using namespace QuickDC::Api::Events;

	std::visit([this](const auto& e) {
		if constexpr (requires { e.hub; })
		{
			if (e.hub == m_handle) handle(e);
		}
	}, event.value);
}

QStringList Qdc::HubBridge::sessionHubs(CoreThread* thread)
{
	QStringList out;
	if (!thread) return out;
	if (const auto* kept = thread->channel()->latest<QuickDC::Api::Events::SessionHubs>())
		for (const std::string& url : kept->urls) out << QString::fromStdString(url);
	return out;
}

void Qdc::HubBridge::clearSessionHubs(CoreThread* thread)
{
	if (thread) thread->submit(QuickDC::Api::Commands::ClearSessionHubs{});
}

void Qdc::HubBridge::endSession(CoreThread* thread)
{
	if (thread) thread->submit(QuickDC::Api::Commands::EndSession{});
}

QString Qdc::HubBridge::normalisedUrl(const QString& url)
{
	return normalise_url(url);
}

QString Qdc::HubBridge::settingsUrl(const QString& url)
{
	return QString::fromStdString(QuickDC::hubSettingsGroup(url.toStdString()));
}

QString Qdc::HubBridge::url() const
{
	return m_url;
}

void Qdc::HubBridge::requestHistory(const QString& conversation, int limit)
{
	if (limit <= 0 || !m_thread) return;

	m_thread->submit(QuickDC::Api::Commands::RequestHistory{
		m_handle, m_url.toStdString(), conversation.toStdString(), limit});
}

Qdc::NetStatus Qdc::HubBridge::netStatus() const
{
	return m_netStatus;
}

Qdc::HubStatus Qdc::HubBridge::hubStatus() const
{
	return m_hubStatus;
}

QString Qdc::HubBridge::hubName() const
{
	return m_hubName;
}

QString Qdc::HubBridge::hubDescription() const
{
	return m_hubDescription;
}

bool Qdc::HubBridge::isSecure() const
{
	return m_secure;
}

bool Qdc::HubBridge::supportsRichText() const
{
	return m_richText;
}

void Qdc::HubBridge::setFollowRedirects(bool follow)
{
	m_followRedirects = follow;
	sendPolicy();
}

bool Qdc::HubBridge::followRedirects() const
{
	return m_followRedirects;
}

void Qdc::HubBridge::setAllowPeerConnections(bool allow)
{
	m_allowPeerConnections = allow;
	sendPolicy();
}

bool Qdc::HubBridge::allowPeerConnections() const
{
	return m_allowPeerConnections;
}

void Qdc::HubBridge::startLoginWatch()
{
	m_loginTimer = new QTimer(this);
	m_loginTimer->setSingleShot(true);
	m_loginTimer->setInterval(LOGIN_TIMEOUT_MS);

	connect(m_loginTimer, &QTimer::timeout, this, [this] {
		emit systemError(tr("%1 took the connection and then said nothing. "
		                    "The hub is most likely not running, however open "
		                    "the port is.").arg(m_url));
	});

	/*
	 * Both of these arrive on this thread - the session marshals them - so the
	 * timer is only ever touched from the one place.
	 */
	connect(this, &HubBridge::hubStatusChanged, this, [this](HubStatus status) {
		if (status == HubStatus::LoggedIn) m_loginTimer->stop();
	});

	connect(this, &HubBridge::netStatusChanged, this, [this](NetStatus status) {
		switch (status)
		{
			case NetStatus::Lookup:
			case NetStatus::Connecting:
			case NetStatus::Connected:
			case NetStatus::TlsConnected:
				break;

			/* It failed and said so itself; no need to say it twice. */
			default:
				m_loginTimer->stop();
		}
	});
}

void Qdc::HubBridge::connectToHub()
{
	if (!m_opened) return;

	m_loginTimer->start();
	m_thread->submit(QuickDC::Api::Commands::ConnectHub{m_handle});
}

void Qdc::HubBridge::disconnectFromHub()
{
	if (!m_opened) return;

	m_loginTimer->stop();
	m_thread->submit(QuickDC::Api::Commands::DisconnectHub{m_handle});
}

QString Qdc::HubBridge::textForHub(const QString& message, bool richText)
{
	if (!richText || message.isEmpty()) return message;

	/* The prefixes hubs actually use. A message beginning with one is a command to
	   them, whatever it says afterwards. */
	static const QString prefixes = QStringLiteral("+!");

	if (!prefixes.contains(message.at(0))) return message;

	return QLatin1Char(' ') + message;
}

void Qdc::HubBridge::sendChat(const QString& message, bool richText)
{
	if (!m_opened) return;

	m_thread->submit(QuickDC::Api::Commands::SendChat{
		m_handle, textForHub(message, richText).toStdString(), richText});
}

void Qdc::HubBridge::sendPrivateChat(const QString& userId, const QString& message,
                                     bool richText)
{
	if (!m_opened) return;

	m_thread->submit(QuickDC::Api::Commands::SendPrivateChat{
		m_handle, userId.toStdString(), textForHub(message, richText).toStdString(), richText});
}

void Qdc::HubBridge::sendPassword(const QString& password)
{
	if (!m_opened) return;

	m_thread->submit(QuickDC::Api::Commands::SendPassword{m_handle, password.toStdString()});
}

void Qdc::HubBridge::collect(const SearchHit& hit)
{
	m_pendingHits.append(hit);

	/*
	 * Enough of them to be worth handing over now. The rest of a burst follows in
	 * the next batch; what matters is that the view is not asked to lay itself out
	 * once per reply, not that every batch is exactly full.
	 */
	if (m_pendingHits.size() >= HIT_BATCH)
	{
		deliverSearchResults();
		return;
	}

	if (!m_hitTimer)
	{
		m_hitTimer = new QTimer(this);
		m_hitTimer->setSingleShot(true);
		m_hitTimer->setInterval(HIT_BATCH_MS);

		connect(m_hitTimer, &QTimer::timeout, this, &HubBridge::deliverSearchResults);
	}

	/*
	 * Started rather than restarted, so that a steady stream of replies is still
	 * handed over regularly instead of being held back for as long as it keeps
	 * coming.
	 */
	if (!m_hitTimer->isActive()) m_hitTimer->start();
}

void Qdc::HubBridge::deliverSearchResults()
{
	if (m_hitTimer) m_hitTimer->stop();
	if (m_pendingHits.isEmpty()) return;

	QVector<SearchHit> going;
	going.swap(m_pendingHits);

	emit searchResultsReceived(going);
}


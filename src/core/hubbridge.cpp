/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "hubbridge.h"

#include "corethread.h"

#include <api/core.h>
#include <api/hub.h>
#include <chat/archive.h>
#include <samurai/io/net/url.h>
#include <share/searchresult.h>

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

QString from_utf8(const char* text)
{
	return text ? QString::fromUtf8(text) : QString();
}

Qdc::NetStatus convert(QuickDC::HubListener::StatusNetwork status)
{
	switch (status)
	{
		case QuickDC::HubListener::NetInvalid:                 return Qdc::NetStatus::Invalid;
		case QuickDC::HubListener::NetLookup:                  return Qdc::NetStatus::Lookup;
		case QuickDC::HubListener::NetConnecting:              return Qdc::NetStatus::Connecting;
		case QuickDC::HubListener::NetConnected:               return Qdc::NetStatus::Connected;
		case QuickDC::HubListener::NetDisconnected:            return Qdc::NetStatus::Disconnected;
		case QuickDC::HubListener::NetTlsConnected:            return Qdc::NetStatus::TlsConnected;
		case QuickDC::HubListener::NetTlsDisconnected:         return Qdc::NetStatus::TlsDisconnected;
		case QuickDC::HubListener::NetErrorTimeout:            return Qdc::NetStatus::ErrorTimeout;
		case QuickDC::HubListener::NetErrorConnectionRefused:  return Qdc::NetStatus::ErrorConnectionRefused;
		case QuickDC::HubListener::NetErrorHostNotFound:       return Qdc::NetStatus::ErrorHostNotFound;
		case QuickDC::HubListener::NetErrorHostUnreachable:    return Qdc::NetStatus::ErrorHostUnreachable;
		case QuickDC::HubListener::NetErrorIO:                 return Qdc::NetStatus::ErrorIO;
		case QuickDC::HubListener::NetTlsError:                return Qdc::NetStatus::TlsError;
	}

	return Qdc::NetStatus::Invalid;
}

Qdc::HubStatus convert(QuickDC::HubListener::StatusHub status)
{
	switch (status)
	{
		case QuickDC::HubListener::HubNone:                  return Qdc::HubStatus::None;
		case QuickDC::HubListener::HubLogin:                 return Qdc::HubStatus::Login;
		case QuickDC::HubListener::HubPassword:              return Qdc::HubStatus::Password;
		case QuickDC::HubListener::HubLoggedIn:              return Qdc::HubStatus::LoggedIn;
		case QuickDC::HubListener::HubErrorLoginRefused:     return Qdc::HubStatus::ErrorLoginRefused;
		case QuickDC::HubListener::HubErrorFull:             return Qdc::HubStatus::ErrorFull;
		case QuickDC::HubListener::HubErrorDisabled:         return Qdc::HubStatus::ErrorDisabled;
		case QuickDC::HubListener::HubErrorNickNotAccepted:  return Qdc::HubStatus::ErrorNickNotAccepted;
		case QuickDC::HubListener::HubErrorNickTaken:        return Qdc::HubStatus::ErrorNickTaken;
		case QuickDC::HubListener::HubErrorCIDTaken:         return Qdc::HubStatus::ErrorCidTaken;
		case QuickDC::HubListener::HubErrorWrongPassword:    return Qdc::HubStatus::ErrorWrongPassword;
		case QuickDC::HubListener::HubKicked:                return Qdc::HubStatus::Kicked;
		case QuickDC::HubListener::HubBannedTemporarily:     return Qdc::HubStatus::BannedTemporarily;
		case QuickDC::HubListener::HubBannedPermanently:     return Qdc::HubStatus::BannedPermanently;
		case QuickDC::HubListener::HubRedirected:            return Qdc::HubStatus::Redirected;
	}

	return Qdc::HubStatus::None;
}

/**
 * When a message was said, as a QDateTime.
 *
 * The core hands down seconds since the epoch and always sets it: the hub's own
 * TS where it sent one, and the moment of arrival otherwise. A zero is treated
 * as nothing having been said about it rather than as 1970, which is what a
 * front end would otherwise draw against a replayed line.
 */
QDateTime stamped(uint64_t timestamp)
{
	return timestamp ? QDateTime::fromSecsSinceEpoch(static_cast<qint64>(timestamp))
	                 : QDateTime::currentDateTime();
}

Qdc::UserInfo convert(const QuickDC::User* user)
{
	Qdc::UserInfo info;
	if (!user) return info;

	info.id           = from_utf8(user->getID());
	info.nick         = from_utf8(user->getNick());
	info.description  = from_utf8(user->getDescription());
	info.email        = from_utf8(user->getEmail());
	info.speed        = from_utf8(user->getSpeed());
	info.userAgent    = from_utf8(user->getUserAgent());
	info.addressIPv4  = from_utf8(user->getAddressIPv4());
	info.addressIPv6  = from_utf8(user->getAddressIPv6());
	info.keyprint     = from_utf8(user->getKeyprint());

	/*
	 * The client ID is held as bytes but is a base32 string, and is empty on
	 * NMDC, where the nickname does the same job. Same choice the reference
	 * client makes in cmd_browse().
	 */
	const char* clientId = reinterpret_cast<const char*>(user->getClientID());
	info.sourceId     = (clientId && *clientId) ? QString::fromUtf8(clientId)
	                                            : info.nick;

	info.sharedBytes  = user->getSharedBytes();
	info.sharedFiles  = user->getSharedFiles();
	info.slotCount    = user->getNumSlots();
	info.op           = user->isOp();
	info.away         = user->isAway();

	return info;
}

/**
 * Fill in what the core needs but a user is unlikely to type: a scheme, because
 * the core dispatches on it and has no default, and a port, because a URL
 * without one never resolves. dchub and 411 are what the reference client
 * assumes for the shorthand.
 *
 * Edited in place rather than parsed and rebuilt, so that everything not being
 * changed survives untouched. An ADCS keyprint lives in the query, and anything
 * that reassembled the address from its parsed parts would drop it.
 */
/**
 * Where the authority ends: at the path, the query or the fragment, or at the end of
 * the string when there is none of them.
 *
 * @param from the first character after the "://".
 */
qsizetype authority_end(const QString& url, qsizetype from)
{
	for (qsizetype n = from; n < url.size(); ++n)
	{
		const QChar at = url.at(n);

		if (at == QLatin1Char('/') || at == QLatin1Char('?')
		    || at == QLatin1Char('#'))
		{
			return n;
		}
	}

	return url.size();
}

QString normalise_url(const QString& url)
{
	QString normalised = url.trimmed();
	if (normalised.isEmpty()) return normalised;

	qsizetype authority = normalised.indexOf(QStringLiteral("://"));
	if (authority < 0)
	{
		normalised.prepend(QStringLiteral("dchub://"));
		authority = normalised.indexOf(QStringLiteral("://"));
	}
	authority += 3;

	const qsizetype end = authority_end(normalised, authority);

	const QString host = normalised.mid(authority, end - authority);

	/*
	 * A colon inside brackets belongs to an IPv6 literal rather than a port, so
	 * only one after the closing bracket counts. Both are -1 when there is
	 * neither, which reads as "no port" and is right.
	 */
	if (host.lastIndexOf(QLatin1Char(':')) > host.lastIndexOf(QLatin1Char(']')))
		return normalised;

	normalised.insert(end, QStringLiteral(":411"));
	return normalised;
}

}

namespace Qdc {

/**
 * The core side half of a HubBridge.
 *
 * Built and destroyed inside jobs on the core thread, and only ever called from
 * there: QuickDC::Hub registers itself with the hub manager and creates protocol
 * sessions, none of which is safe anywhere else.
 *
 * Both halves of the core's hub interface are implemented here - Hub for the
 * commands, HubListener for the events - exactly as the reference client does it
 * in core/test/cli/hubconnection.h.
 *
 * Every event does the same two things: copy whatever the core lent us, then
 * hand the copy to the bridge's thread. The copy is not optional. EventUserLeave
 * is handed a User the core deletes on the next line, and every const char* here
 * points into a buffer that is reused as soon as the next command is parsed.
 */
class HubSession final : public QuickDC::Hub, public QuickDC::HubListener {

public:
	HubSession(HubBridge* bridge, std::shared_ptr<Relay> relay,
	           std::shared_ptr<HubBridge::SessionBox> holder,
	           Samurai::IO::Net::URL* url)
		: QuickDC::Hub(this, url)
		, m_bridge(bridge)
		, m_relay(std::move(relay))
		, m_holder(std::move(holder))
	{
	}

	~HubSession() override = default;

	void EventNetStatus(enum StatusNetwork netstate) override
	{
		const NetStatus status = convert(netstate);

		/*
		 * Asked of the core here, on the core thread, and carried over as a plain
		 * bool: this is the only place it can be asked safely, and the answer is
		 * the socket's own rather than a guess made from the status.
		 *
		 * Guessing does not work. The obvious rule - secure once the handshake is
		 * reported, not secure otherwise - reads correctly for a moment and is
		 * then wrong for the rest of the connection, because the ADC session
		 * reports being connected again after its handshake, so the last thing
		 * said about a TLS connection is "connected".
		 */
		const bool secure = isSecure();

		report([status, secure](HubBridge* bridge) {
			bridge->m_netStatus = status;

			if (secure != bridge->m_secure)
			{
				bridge->m_secure = secure;
				emit bridge->secureChanged(secure);
			}

			if (session_ended(status) && bridge->m_richText)
			{
				bridge->m_richText = false;
				emit bridge->richTextSupportChanged(false);
			}

			emit bridge->netStatusChanged(status);
		});
	}

	void EventReconnectScheduled(int seconds) override
	{
		report([seconds](HubBridge* bridge) {
			emit bridge->reconnectScheduled(seconds);
		});
	}

	void EventHubStatus(enum StatusHub hubstate) override
	{
		const HubStatus status = convert(hubstate);
		report([status](HubBridge* bridge) {
			bridge->m_hubStatus = status;
			emit bridge->hubStatusChanged(status);
		});
	}

	void EventHubExtension(const char* /* fourcc */) override
	{
		/*
		 * Asked of the core rather than matched against a name here: which extension
		 * carries markdown is the protocol's business, and this is the thread where the
		 * core can be asked at all.
		 */
		const bool rich = supportsRichText();

		report([rich](HubBridge* bridge) {
			if (rich == bridge->m_richText) return;

			bridge->m_richText = rich;
			emit bridge->richTextSupportChanged(rich);
		});
	}

	void EventSystemError(const char* msg) override
	{
		const QString message = from_utf8(msg);
		report([message](HubBridge* bridge) {
			emit bridge->systemError(message);
		});
	}

	void EventChat(const QuickDC::User* user, const char* message, bool action,
	               bool richText, uint64_t timestamp) override
	{
		ChatMessage chat;
		chat.hubUrl   = from_utf8(getUrl());
		chat.from     = convert(user);
		chat.text     = from_utf8(message);
		chat.received = stamped(timestamp);
		chat.action   = action;
		chat.richText = richText;

		report([chat](HubBridge* bridge) {
			emit bridge->chatReceived(chat);
		});
	}

	/**
	 * 'to' is unused: it is us on an incoming message and the other party on an
	 * echo of one of ours, so it is the context the core supplies - not the
	 * sender - that says which conversation a line belongs to.
	 */
	void EventPrivateChat(const QuickDC::User* from, const QuickDC::User*,
	                      const char* message, const char* userContextID,
	                      bool action, bool richText, uint64_t timestamp) override
	{
		ChatMessage chat;
		chat.hubUrl         = from_utf8(getUrl());
		chat.from           = convert(from);
		chat.text           = from_utf8(message);
		chat.context        = from_utf8(userContextID);
		chat.received       = stamped(timestamp);
		chat.action         = action;
		chat.richText       = richText;
		chat.privateMessage = true;

		if (chat.context.isEmpty())
			chat.context = chat.from.id;

		report([chat](HubBridge* bridge) {
			emit bridge->chatReceived(chat);
		});
	}

	void EventHubMessage(const char* fakeuser, const char* message, bool action,
	                     bool richText, uint64_t timestamp) override
	{
		ChatMessage chat;
		chat.hubUrl   = from_utf8(getUrl());
		chat.fromHub  = from_utf8(fakeuser);
		chat.text     = from_utf8(message);
		chat.received = stamped(timestamp);
		chat.action   = action;
		chat.richText = richText;

		report([chat](HubBridge* bridge) {
			emit bridge->chatReceived(chat);
		});
	}

	void EventUserJoin(const char* user) override
	{
		const QString nick = from_utf8(user);
		report([nick](HubBridge* bridge) {
			emit bridge->userJoined(nick);
		});
	}

	void EventUserLeave(const QuickDC::User* user, const char* message,
	                    bool disconnect) override
	{
		const UserInfo info = convert(user);
		const QString reason = from_utf8(message);

		report([info, reason, disconnect](HubBridge* bridge) {
			emit bridge->userLeft(info, reason, disconnect);
		});
	}

	void EventUsersCleanup() override
	{
		report([](HubBridge* bridge) {
			emit bridge->usersCleared();
		});
	}

	void EventUserUpdate(const QuickDC::User* u) override
	{
		const UserInfo info = convert(u);
		report([info](HubBridge* bridge) {
			emit bridge->userUpdated(info);
		});
	}

	/**
	 * A search reply, as an untyped pointer, which is all the listener interface
	 * offers - see the note in TODO.md about the signature.
	 *
	 * Copied and not deleted. The session deletes the result on the line after it
	 * calls this, exactly as it does with the User* handed to the other events, so
	 * taking ownership here would be a second delete of the same object.
	 */
	void EventSearchResult(void* pointer) override
	{
		const QuickDC::Share::SearchResult* result =
			static_cast<const QuickDC::Share::SearchResult*>(pointer);

		if (!result) return;

		SearchHit hit;
		hit.hubUrl    = from_utf8(getUrl());
		hit.token     = from_utf8(result->getToken());
		hit.name      = from_utf8(result->getFile());
		hit.tth       = from_utf8(result->getTTH());
		hit.size      = result->getSize();
		hit.freeSlots = static_cast<quint32>(result->getFreeSlots());
		hit.user      = convert(result->getUser());

		/* The separator on the end is the whole of what says this is a folder. */
		hit.directory = hit.name.endsWith(QLatin1Char('/'));

		report([hit](HubBridge* bridge) {
			bridge->collect(hit);
		});
	}

	/*
	 * Both of these report only a change. A hub re-sends its own INF for reasons of
	 * its own, and every repeat would otherwise redraw the sidebar row and write the
	 * same name into the list of hubs visited.
	 */
	void EventHubName(const char* hubname) override
	{
		const QString name = from_utf8(hubname);
		report([name](HubBridge* bridge) {
			if (name == bridge->m_hubName) return;

			bridge->m_hubName = name;
			emit bridge->hubNameChanged(name);
		});
	}

	void EventHubDescription(const char* description) override
	{
		const QString text = from_utf8(description);
		report([text](HubBridge* bridge) {
			if (text == bridge->m_hubDescription) return;

			bridge->m_hubDescription = text;
			emit bridge->hubDescriptionChanged(text);
		});
	}

	bool EventHubRedirect(const char* address, uint16_t port) override
	{
		const bool follow = m_holder->followRedirects.load(std::memory_order_relaxed);
		const QString target = from_utf8(address);

		report([target, port, follow](HubBridge* bridge) {
			emit bridge->redirectRequested(target, port, follow);
		});

		return follow;
	}

	void EventHubAutenticate() override
	{
		report([](HubBridge* bridge) {
			emit bridge->authenticationRequired();
		});
	}

	bool EventClientConnect(const char*, uint16_t, const QuickDC::User*) override
	{
		return m_holder->allowPeerConnections.load(std::memory_order_relaxed);
	}

	bool EventClientConnect(const QuickDC::User*) override
	{
		return m_holder->allowPeerConnections.load(std::memory_order_relaxed);
	}

	/**
	 * Send a private message, addressed by the id the core knows a user by.
	 *
	 * Resolved here rather than by the caller, because Hub::sendPrivateChatMessage
	 * wants the User object and there is no way to hold on to one - the hub may
	 * have dropped the user since, and the core would have freed it. The lookup
	 * goes through the protected session, the only route to it that api/hub.h
	 * offers.
	 */
	void sendPrivateChat(const char* userId, const char* message, bool richText)
	{
		const QuickDC::User* user = session ? session->getUser(userId) : nullptr;

		if (!user)
		{
			EventSystemError("sendPrivateChat(): No such user");
			return;
		}

		sendPrivateChatMessage(user, message, richText);
	}

private:
	/**
	 * Run 'work' on the bridge's thread.
	 *
	 * Addressed to the relay rather than to the bridge, and the relay is held by
	 * the closure: an event may be in flight when the bridge is destroyed, and
	 * there is no way to test a bridge pointer for that from this thread. The
	 * relay is detached instead, which is what turns 'work' into nothing.
	 *
	 * Queued explicitly: a same-thread invokeMethod would run 'work' inline, and
	 * this is only ever called from the core thread, where inline would mean
	 * emitting a signal straight into a view.
	 */
	template<typename Work>
	void report(Work work)
	{
		HubBridge* bridge = m_bridge;
		std::shared_ptr<Relay> relay = m_relay;
		Relay* target = relay.get();

		QMetaObject::invokeMethod(target, [relay = std::move(relay), bridge, work] {
			if (!relay->detached()) work(bridge);
		}, Qt::QueuedConnection);
	}

	HubBridge* m_bridge;
	std::shared_ptr<Relay> m_relay;

	/** Where the policy flags live, and what keeps them alive; see SessionBox. */
	std::shared_ptr<HubBridge::SessionBox> m_holder;
};

}

Qdc::HubBridge::HubBridge(CoreThread* thread, const QString& url, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
	, m_url(normalise_url(url))
	, m_holder(std::make_shared<SessionBox>())
	, m_relay(Relay::create())
{
	startLoginWatch();

	if (!m_thread || !m_thread->isCoreRunning())
	{
		/*
		 * Queued rather than emitted here, because nothing can be connected to
		 * a signal of an object that is still being constructed.
		 */
		QMetaObject::invokeMethod(this, [this] {
			emit systemError(tr("The QuickDC core is not running."));
		}, Qt::QueuedConnection);
		return;
	}

	const QString address = m_url;
	if (address.isEmpty())
	{
		QMetaObject::invokeMethod(this, [this] {
			emit systemError(tr("No hub address was given."));
		}, Qt::QueuedConnection);
		return;
	}

	/*
	 * Posted rather than waited on. Building the session registers it with the hub
	 * manager, which happens between turns of the reactor, and a window that waited
	 * for that stopped drawing for as long as the core's current turn took - opening
	 * a hub while a transfer was moving used to hang the whole interface.
	 *
	 * Everything the bridge can be asked to do afterwards is posted too, and each
	 * job finds the session through the same box, so an order given before it exists
	 * simply queues up behind it.
	 */
	/*
	 * Every one of these is a copy, including the bridge pointer and the relay.
	 * Nothing here may reach through 'this' to a member: the job runs on the core
	 * thread at a time of the reactor's choosing, and by then this object may be
	 * gone. The pointer itself is only ever handed on to the session, which uses it
	 * through the relay and so not at all once the relay is detached.
	 */
	m_thread->call(m_relay, [holder = m_holder, relay = m_relay, address,
	                         bridge = this] {
		/*
		 * Hub copies the URL it is given, so this one is ours to destroy - and
		 * it has to be, since Hub keeps no reference to it.
		 */
		Samurai::IO::Net::URL parsed(address.toStdString());
		if (!parsed.isValid()) return false;

		holder->session = new HubSession(bridge, relay, holder, &parsed);
		return true;
	}, [this, address](bool built) {
		if (!built)
			emit systemError(tr("'%1' is not an address I can use.").arg(address));
	});
}

Qdc::HubBridge::~HubBridge()
{
	/* Nothing in flight has anywhere to report to from here on. */
	m_relay->detach();

	if (!m_thread) return;

	/*
	 * Posted, and it no longer has to block. The session is still registered with
	 * the hub manager and may have an event in flight, but the event is addressed
	 * to the relay rather than to this object, and the relay is detached above - so
	 * letting this go while a job can still run costs nothing.
	 *
	 * disconnect() before delete because ~Hub does not do it - it unregisters
	 * from the hub manager and frees the URL, and leaves an open session alone.
	 *
	 * The box rather than a member, because by the time this job runs there is no
	 * bridge left to read one from. A core that has already stopped drops the job
	 * and leaks the session, which remains the lesser evil: ~Hub asks
	 * Core::getInstance() to unregister it, and getInstance() builds a new Core
	 * when there is none - so deleting it from this thread would resurrect the
	 * whole reactor in order to tear one hub out of it.
	 */
	m_thread->post([holder = m_holder] {
		if (!holder->session) return;

		holder->session->disconnect();
		delete holder->session;
		holder->session = nullptr;
	});
}

QString Qdc::HubBridge::normalisedUrl(const QString& url)
{
	return normalise_url(url);
}

QString Qdc::HubBridge::settingsUrl(const QString& url)
{
	const QString normalised = normalise_url(url);

	const qsizetype scheme = normalised.indexOf(QStringLiteral("://"));
	if (scheme < 0) return normalised;

	return normalised.left(authority_end(normalised, scheme + 3));
}

QString Qdc::HubBridge::url() const
{
	return m_url;
}

void Qdc::HubBridge::requestHistory(const QString& conversation, int limit)
{
	if (limit <= 0) return;

	/*
	 * Converted to bytes before the closure takes them: a QString shares its
	 * buffer with its copies, and this one is read on the core's thread.
	 */
	const QByteArray hub = m_url.toUtf8();
	const QByteArray with = conversation.toUtf8();

	m_thread->call(m_relay,
		[thread = m_thread, hub, with, limit] {
			QVector<ChatMessage> lines;

			QuickDC::Core* core = thread->core();
			if (!core || !core->chat) return lines;

			const std::vector<QuickDC::ChatArchive::Line> found =
				core->chat->tail(hub.constData(), with.constData(),
				                 static_cast<size_t>(limit));

			lines.reserve(static_cast<qsizetype>(found.size()));

			for (const QuickDC::ChatArchive::Line& line : found)
			{
				ChatMessage message;

				message.hubUrl = QString::fromUtf8(hub);
				message.text = QString::fromUtf8(line.text.c_str());
				message.fromHub = QString::fromUtf8(line.hub.c_str());
				message.from.nick = QString::fromUtf8(line.nick.c_str());
				message.from.sourceId = QString::fromUtf8(line.source.c_str());
				message.from.op = line.op;
				message.action = line.action;
				message.richText = line.richText;
				message.privateMessage = line.privateMessage;
				message.received =
					QDateTime::fromSecsSinceEpoch(static_cast<qint64>(line.time));

				lines.append(message);
			}

			return lines;
		},
		[this, conversation](QVector<ChatMessage> lines) {
			emit historyRestored(conversation, lines);
		});
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
	m_holder->followRedirects.store(follow, std::memory_order_relaxed);
}

bool Qdc::HubBridge::followRedirects() const
{
	return m_holder->followRedirects.load(std::memory_order_relaxed);
}

void Qdc::HubBridge::setAllowPeerConnections(bool allow)
{
	m_holder->allowPeerConnections.store(allow, std::memory_order_relaxed);
}

bool Qdc::HubBridge::allowPeerConnections() const
{
	return m_holder->allowPeerConnections.load(std::memory_order_relaxed);
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
	if (!m_thread) return;

	m_loginTimer->start();
	m_thread->post([holder = m_holder] {
		if (holder->session) holder->session->connect();
	});
}

void Qdc::HubBridge::disconnectFromHub()
{
	if (!m_thread) return;

	m_loginTimer->stop();
	m_thread->post([holder = m_holder] {
		if (holder->session) holder->session->disconnect();
	});
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
	if (!m_thread) return;

	const QByteArray utf8 = textForHub(message, richText).toUtf8();

	m_thread->post([holder = m_holder, utf8, richText] {
		if (holder->session) holder->session->sendChatMessage(utf8.constData(), richText);
	});
}

void Qdc::HubBridge::sendPrivateChat(const QString& userId, const QString& message,
                                    bool richText)
{
	if (!m_thread) return;

	const QByteArray id = userId.toUtf8();
	const QByteArray utf8 = textForHub(message, richText).toUtf8();

	m_thread->post([holder = m_holder, id, utf8, richText] {
		if (holder->session)
			holder->session->sendPrivateChat(id.constData(), utf8.constData(), richText);
	});
}

void Qdc::HubBridge::sendPassword(const QString& password)
{
	if (!m_thread) return;

	const QByteArray utf8 = password.toUtf8();
	m_thread->post([holder = m_holder, utf8] {
		if (holder->session) holder->session->sendPassword(utf8.constData());
	});
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

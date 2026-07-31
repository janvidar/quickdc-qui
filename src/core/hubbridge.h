/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_HUBBRIDGE_H
#define HAVE_QUICKDC_QUI_HUBBRIDGE_H

#include "coretypes.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>

namespace Qdc {

class CoreThread;
class HubSession;

/**
 * One hub connection, as a view sees it.
 *
 * Lives on the thread that constructed it, normally the user interface thread.
 * The core side half is a HubSession, built and torn down inside jobs on the
 * core thread, and every command below is a job too - so a caller never touches
 * a core object, and the session never touches a widget.
 *
 * Events arrive as signals on this object's thread, carrying value types. That
 * is not politeness: the core frees the User* it hands a listener as soon as the
 * call returns, so the session copies eagerly and the copy is what is emitted.
 *
 * Constructing one connects nothing. Call connectToHub().
 */
class HubBridge final : public QObject {
	Q_OBJECT

public:
	/**
	 * @param url a hub address - "dchub://host:port", "adc://..." or
	 *        "adcs://...". A bare "host:port" is taken as dchub, matching the
	 *        core's own shorthand, and a missing port as 411. The scheme is what
	 *        picks the protocol, and an ADCS keyprint in the query is preserved.
	 *
	 * Fails silently if the core is not running or the address cannot be parsed;
	 * systemError() says which, and netStatus() stays Invalid.
	 */
	HubBridge(CoreThread* thread, const QString& url, QObject* parent = nullptr);
	~HubBridge() override;

	/**
	 * The address as this class would use it, with the scheme and port filled in.
	 *
	 * Public so a caller can tell whether two addresses name the same hub without
	 * opening a connection to find out - "example.org" and
	 * "dchub://example.org:411" are the same hub, and only this knows that.
	 */
	static QString normalisedUrl(const QString& url);

	/**
	 * The address as given. Note that this is not the canonical form the core
	 * uses internally, which is only known once the session exists.
	 */
	QString url() const;

	/** The last state reported, so a view built later can catch up. */
	NetStatus netStatus() const;
	HubStatus hubStatus() const;

	/** The hub's name or topic; empty until the hub sends one. */
	QString hubName() const;

	/**
	 * Whether the connection is carrying TLS.
	 *
	 * Derived from the network status transitions rather than asked, because the
	 * core offers nothing to ask: Hub keeps its session protected and has no
	 * isSecure(), so NetTlsConnected arriving and any subsequent disconnection or
	 * error is all there is to go on. That makes this a record of what was last
	 * reported, not a live check of the socket.
	 */
	bool isSecure() const;

	/**
	 * Whether a message on this hub can say that it is markdown.
	 *
	 * ADC has room for the flag; NMDC has none, so formatting there would be sent as
	 * markers nobody asked for and read back literally. The composer greys its
	 * controls out rather than losing them silently.
	 */
	bool supportsRichText() const;

	/**
	 * Whether to follow a redirect the hub asks for.
	 *
	 * The core wants an answer inside the callback, on the core thread, and
	 * there is no way to ask a user without blocking the reactor for as long as
	 * they take to decide - so this is a policy set in advance rather than a
	 * prompt. redirectRequested() is emitted either way, and reports which way
	 * it went.
	 *
	 * Off by default: a hub that sends an unattended client somewhere else
	 * should not be obeyed without someone having said so.
	 */
	void setFollowRedirects(bool follow);
	bool followRedirects() const;

	/**
	 * Whether to accept the peer connections the hub brokers. Same reasoning as
	 * above; on by default, because refusing them means no transfers at all.
	 */
	void setAllowPeerConnections(bool allow);
	bool allowPeerConnections() const;

public slots:
	void connectToHub();
	void disconnectFromHub();

	/** Public chat. Dropped with a systemError() if there is no session yet. */
	/**
	 * @param richText the text is markdown. Only set it when the message really is
	 *        formatted, and never on a hub that cannot carry the flag.
	 */
	void sendChat(const QString& message, bool richText = false);

	/**
	 * @param userId the id the core knows the user by - a CID on ADC, a nickname
	 *        on NMDC. This is UserInfo::id, not UserInfo::nick.
	 */
	void sendPrivateChat(const QString& userId, const QString& message,
	                     bool richText = false);

	/** Answer a hub that asked for one, after authenticationRequired(). */
	void sendPassword(const QString& password);

signals:
	void netStatusChanged(Qdc::NetStatus status);
	void hubStatusChanged(Qdc::HubStatus status);
	void hubNameChanged(const QString& name);
	void secureChanged(bool secure);

	/** Something the core wants to complain about, already formatted. */
	void systemError(const QString& message);

	void chatReceived(const Qdc::ChatMessage& message);

	/**
	 * Only a nickname is known at this point. The rest follows in userUpdated()
	 * once the hub sends it, which for most hubs is immediately.
	 */
	void userJoined(const QString& nick);

	void userLeft(const Qdc::UserInfo& user, const QString& message, bool disconnected);

	/** A user's details arrived or changed. Also the first full sight of them. */
	void userUpdated(const Qdc::UserInfo& user);

	/**
	 * Every user the core told us about is gone; empty the list. Emitted when a
	 * session ends, before any reconnect.
	 */
	void usersCleared();

	/** The hub wants a password; answer with sendPassword(). */
	void authenticationRequired();

	/** @param followed what followRedirects() decided, at the time it was asked. */
	void redirectRequested(const QString& address, quint16 port, bool followed);

private:
	friend class HubSession;

	/** Build the login watchdog and the signals that call it off. */
	void startLoginWatch();

	CoreThread* m_thread;
	QString m_url;

	/** Owned by the core thread; only ever read or written from a job there. */
	HubSession* m_session = nullptr;

	NetStatus m_netStatus = NetStatus::Invalid;
	HubStatus m_hubStatus = HubStatus::None;
	QString m_hubName;
	bool m_secure = false;

	/**
	 * Says so when a hub takes the connection and then never logs us in.
	 *
	 * Nothing else ever will. A hub that accepts the socket and says nothing leaves
	 * the connection open and healthy for as long as anyone cares to wait, so
	 * without this the view sits there connected to a hub that is not there.
	 */
	QTimer* m_loginTimer = nullptr;

	/** Read by the session on the core thread, written by whoever calls the setter. */
	std::atomic<bool> m_followRedirects{false};
	std::atomic<bool> m_allowPeerConnections{true};
};

}

#endif // HAVE_QUICKDC_QUI_HUBBRIDGE_H

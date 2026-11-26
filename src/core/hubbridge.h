/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_HUBBRIDGE_H
#define HAVE_QUICKDC_QUI_HUBBRIDGE_H

#include "coretypes.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <cstdint>

namespace QuickDC { namespace Api { struct Event; } }

namespace Qdc {

class CoreThread;

/**
 * One hub connection, as a view sees it.
 *
 * Lives on the thread that constructed it, normally the user interface thread.
 * The session itself is the core's: this opens one with an OpenHub command under
 * a handle of its own, drives it with commands, and hears it through the core's
 * events for that handle - so a caller never touches a core object, and the
 * session never touches a widget. Destroying this closes the session.
 *
 * Events arrive as signals on this object's thread, carrying value types copied
 * by the core while what they describe still existed.
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
	/** The hubs open when the last session ended, as the core kept them. */
	static QStringList sessionHubs(CoreThread* thread);

	/** Forget every kept hub that is not open now. */
	static void clearSessionHubs(CoreThread* thread);

	/** The front end is going away: the hubs it closes now are kept for next time. */
	static void endSession(CoreThread* thread);

	/** 'remember' keeps the hub among those the core opens again next start. */
	HubBridge(CoreThread* thread, const QString& url, QObject* parent = nullptr, bool remember = true);
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
	 * The address with everything after the authority taken off:
	 * "{scheme}://{host}:{port}" and nothing else.
	 *
	 * This is what names a hub's settings, and it has to be this rather than the
	 * address as given, because the same hub is reachable by more than one address.
	 * "adcs://host:1511/?kp=..." and "adcs://host:1511" are one hub with one
	 * nickname and one password; keyed on what was typed they would have a set of
	 * settings each, and pasting a keyprint into the address once would be enough to
	 * lose the identity already set up for that hub.
	 *
	 * The same form the core files a hub under - Hub::getUrl(), which is built the
	 * same way. Computed here rather than asked of the core, because the settings for
	 * a hub are wanted before there is a session to ask.
	 */
	static QString settingsUrl(const QString& url);

	/**
	 * A message as it has to go on the wire so that no hub reads it as a command.
	 *
	 * Hubs take a leading character as a command prefix, answer the sender, and relay
	 * nothing: '+' is QuickDC's own and '!' is uhub's, among others. Markdown writes a
	 * picture as "![name](...)", so an image at the start of a message was swallowed by
	 * the hub with "Command not found" coming back to the sender and nothing at all
	 * reaching anybody else.
	 *
	 * A leading space is the whole fix. It costs nothing to look at - a picture is drawn
	 * on its own line anyway - and the alternative is a message that never arrives.
	 *
	 * Only for markdown. Plain text is left exactly as typed, because somebody who
	 * begins a plain message with '+' or '!' is addressing the hub on purpose.
	 */
	static QString textForHub(const QString& message, bool richText);

	/**
	 * The address as given. Note that this is not the canonical form the core
	 * uses internally, which is only known once the session exists.
	 */
	QString url() const;

	/**
	 * Ask for what was said on this hub before now.
	 *
	 * The core keeps the transcripts and writes them as the chat arrives; this
	 * only reads the recent end of one back. The answer arrives as
	 * historyRestored(), because reading a file is the core's work and nothing
	 * drawn on screen may wait for the reactor to get to it.
	 *
	 * @param conversation empty for this hub's public chat, and otherwise the
	 *        other party's lasting id - the client id on ADC, the nickname on
	 *        NMDC.
	 */
	void requestHistory(const QString& conversation, int limit);

	/** The last state reported, so a view built later can catch up. */
	NetStatus netStatus() const;
	HubStatus hubStatus() const;

	/** The hub's name; empty until the hub sends one. */
	QString hubName() const;

	/**
	 * What the hub says about itself, under its name.
	 *
	 * Empty until the hub sends one, and empty for most of an NMDC session: NMDC
	 * has one field for the two and this arrives only from the $HubTopic that some
	 * hub software sends as well.
	 */
	QString hubDescription() const;

	/**
	 * Whether the connection is carrying TLS.
	 *
	 * Reported by the core with every change of network status, and remembered
	 * here. A record of what was last reported rather than a live check
	 * of the socket - but not a guess: the status alone cannot answer, because an
	 * ADC session reports being connected again after its TLS handshake.
	 */
	bool isSecure() const;

	/**
	 * Whether a message on this hub can say that it is markdown.
	 *
	 * Negotiated, not assumed: the client offers the RTF0 extension in its SUP and only
	 * a hub that answers with the same has agreed to carry the flag - which is how a hub
	 * operator decides whether markdown, and the media a message may point at, are
	 * allowed here at all. False until that answer has been read, false for the rest of
	 * a session on a hub that stayed quiet, and false on NMDC, which has nowhere to put
	 * a flag in the first place.
	 *
	 * Follow richTextSupportChanged() rather than asking once: nothing is known about a
	 * hub until it has been spoken to.
	 */
	bool supportsRichText() const;

	/**
	 * Whether to follow a redirect the hub asks for.
	 *
	 * The core answers inside its own callback, and there is no way to ask a
	 * user without blocking the reactor for as long as they take to decide - so
	 * this is a policy handed to the core in advance rather than a prompt. redirectRequested() is emitted either way, and reports which way
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

	/**
	 * Public chat. Dropped with a systemError() if there is no session yet.
	 *
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

	/**
	 * The core will try this hub again in @a seconds, or not at all if it is -1.
	 *
	 * Emitted after the status that ended the session, so a view following both has
	 * the reason before it is told what happens next. Nothing is emitted when the
	 * disconnection was asked for; there is nothing to wait for then.
	 */
	void reconnectScheduled(int seconds);

	/**
	 * The hub agreed to carry markdown, or the session that had agreed has ended.
	 *
	 * Emitted only on a change, and false again on disconnection: the next connection
	 * negotiates from nothing, and what one hub allowed says nothing about the next.
	 */
	void richTextSupportChanged(bool supported);

	void hubStatusChanged(Qdc::HubStatus status);
	void hubNameChanged(const QString& name);
	void hubDescriptionChanged(const QString& description);
	void secureChanged(bool secure);

	/** Something the core wants to complain about, already formatted. */
	void systemError(const QString& message);

	void chatReceived(const Qdc::ChatMessage& message);

	/**
	 * What requestHistory() went to fetch, oldest first.
	 *
	 * The conversation is echoed back because several may be asked for at once
	 * and the answers arrive in whatever order the core gets to them. Empty when
	 * there is no transcript, which is the ordinary case for a hub being opened
	 * for the first time.
	 */
	void historyRestored(const QString& conversation,
	                     const QVector<Qdc::ChatMessage>& lines);

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

	/**
	 * Somebody on this hub answered a search.
	 *
	 * Batched, and deliberately: replies arrive one at a time from the core and a
	 * wide search across several busy hubs delivers thousands of them in a burst.
	 * Handed on one by one, each cost the results view a model transaction and the
	 * window a relayout, which is what a search froze the interface doing. They are
	 * collected for a moment instead and handed over together.
	 *
	 * They arrive for as long as peers keep answering - there is no event that says
	 * a search is over, because on either protocol there is no such message. A
	 * search is finished when the replies stop.
	 *
	 * Which search each answers is SearchHit::search, the core's attribution; a
	 * reply answering no search of ours is not told at all.
	 */
	void searchResultsReceived(const QVector<Qdc::SearchHit>& hits);

	/** A picture a message here showed is on this disk at @a path; the core fetched it or had it. */
	void chatMediaReady(const QString& tth, const QString& path);

	/**
	 * The hub refused something said about a board.
	 *
	 * Named by the board or by the hash rather than by a token, because the
	 * extension has none: a submission is answered either by its entry or by a
	 * refusal quoting the same hash. Either may be empty where the refusal named
	 * neither.
	 *
	 * @param code the status code with the severity taken off. 71 is a board that is
	 *        not there and can be dropped from the interface, 72 a post larger than
	 *        the board allows, 75 a rate limit, and 76 a post the hub holds no entry
	 *        for and which can stop being asked for. Everything else is to report
	 *        rather than to act on.
	 * @param message the hub's own words: remote text bound for an interface, and
	 *        neither trusted nor this side's to shorten.
	 */
	void boardRefused(int code, const QString& message, const QString& board,
	                  const QString& tth);

	/** @param followed what followRedirects() decided, at the time it was asked. */
	void redirectRequested(const QString& address, quint16 port, bool followed);

private:
	void onCoreEvent(const QuickDC::Api::Event& event);

	/** One of the core's events about this session; specialised per event. */
	template<typename Event>
	void handle(const Event& event);

	/** Tell the core what this session agrees to when a hub asks. */
	void sendPolicy();

	/** Build the login watchdog and the signals that call it off. */
	void startLoginWatch();

	/** Hold a reply back for a moment, so that the ones behind it travel with it. */
	void collect(const Qdc::SearchHit& hit);

	void deliverSearchResults();



	CoreThread* m_thread;
	QString m_url;

	/** What the core knows this session by; see QuickDC::Api::HubHandle. */
	std::uint64_t m_handle = 0;

	/** Asked for, and not refused; nothing is sent for a session there is not. */
	bool m_opened = false;

	bool m_followRedirects = false;
	bool m_allowPeerConnections = true;

	/** What the hub answered about RTF0; see supportsRichText(). */
	bool m_richText = false;

	NetStatus m_netStatus = NetStatus::Invalid;
	HubStatus m_hubStatus = HubStatus::None;
	QString m_hubName;

	/**
	 * Kept across a disconnection, unlike the negotiated capabilities above.
	 *
	 * A name and a description describe the hub rather than say what may be sent
	 * to it, so a stale one costs nothing while clearing one costs twice: the
	 * sidebar row would fall back to a raw address on every reconnect, and the
	 * empty string would be written over the recorded name of a hub that had
	 * merely dropped. A followed redirect keeps the old name until the new hub
	 * introduces itself, which is the price.
	 */
	QString m_hubDescription;
	bool m_secure = false;

	/**
	 * Says so when a hub takes the connection and then never logs us in.
	 *
	 * Nothing else ever will. A hub that accepts the socket and says nothing leaves
	 * the connection open and healthy for as long as anyone cares to wait, so
	 * without this the view sits there connected to a hub that is not there.
	 */
	QTimer* m_loginTimer = nullptr;

	/** Replies waiting to go out together; see searchResultsReceived(). */
	QVector<Qdc::SearchHit> m_pendingHits;
	QTimer* m_hitTimer = nullptr;

};

}

#endif // HAVE_QUICKDC_QUI_HUBBRIDGE_H

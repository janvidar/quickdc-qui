/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_DROPBRIDGE_H
#define HAVE_QUICKDC_QUI_DROPBRIDGE_H

#include <QDateTime>
#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

namespace QuickDC { namespace Api { struct Event; } }

namespace Qdc {

class CoreThread;

/**
 * What a measurement says this client should do; QuickDC::DropMode.
 *
 * Mirrored rather than reused so that nothing above this layer includes a core
 * header. No Q_ENUM_NS on it: the namespace's Q_NAMESPACE belongs to
 * coretypes.h and may only be declared once, and a view that draws the outcome
 * has words of its own for it rather than an enumerator's name.
 */
enum class ReachabilityMode {
	/** Nothing was established. Change no setting on the strength of this. */
	Unknown,

	/** Something outside reached the advertised port. */
	Active,

	/** Nothing did. */
	Passive
};

/**
 * Whether the measurement got through to one port; QuickDC::DropReach.
 *
 * Mirrored beside the words rather than derived from them, for the reason the
 * core's own header gives: the strings are what a person is shown, and this is
 * what a drawing decision is made on. Reducing one to the other would mean
 * matching on display text, which changes when the wording does - and the words
 * are a third party's, so they can change without this client being rebuilt.
 *
 * 'Unknown' is not a soft 'Blocked'. A probe the server could not perform, or one
 * it does not offer, says nothing whatever about this client.
 */
enum class ReachabilityReach { Unknown, Reached, Blocked };

/**
 * What a reachability measurement concluded, in types a view can draw.
 *
 * A copy of QuickDC::DropVerdict with every string already converted, made on
 * the core thread and carried across whole. The core's own struct is not passed
 * up: it is full of std::string, which would be handed between threads still
 * sharing whatever buffer the copy on the other side has - and a view that
 * included the header it lives in would be reaching into the core.
 *
 * The wording in reason, remedies and note was written for a user by whoever
 * measured, and is meant to be shown as it is. Somebody who moves between two
 * clients and is given two different explanations of the same network concludes
 * that one of them is broken.
 */
struct ReachabilityVerdict {
	/** False until a check has completed, and then nothing below means anything. */
	bool ran = false;

	/** Which family was measured. One run answers for one of them. */
	bool ipv6 = false;

	/** The address the server saw, which need not be one of this host's. */
	QString publicAddress;

	/** How the server classified that address, in its own words. */
	QString classification;

	ReachabilityMode mode = ReachabilityMode::Unknown;

	/** The per-port outcomes, named. Empty where the test was not run. */
	QString tcp;
	QString udp;

	/* And the same two as something to draw from; see ReachabilityReach. */
	ReachabilityReach tcpReached = ReachabilityReach::Unknown;
	ReachabilityReach udpReached = ReachabilityReach::Unknown;

	/** The port that was tested, which is the one this client advertises. */
	int port = 0;

	QString reason;
	QStringList remedies;
	QString note;

	/**
	 * When it was established, for deciding whether to ask again.
	 *
	 * Invalid where the core recorded no time, which is every verdict that did
	 * not run. A view that drew the epoch instead would be dating a measurement
	 * that never happened.
	 */
	QDateTime observedAt;

	/**
	 * True where nothing connected back, so this carries an address and no more.
	 *
	 * What a lookup leaves. The core asked a DROP server what address it sees this
	 * client at and stopped there, which is what a client that has declared itself
	 * passive can still be shown without arranging for a stranger to dial its peer
	 * port. Nothing about reachability was established in such a verdict, so a view
	 * must draw no port outcome from one: mode is Unknown and both reach values are
	 * Unknown because nobody tried, and that is not the same as having tried and
	 * been blocked.
	 */
	bool addressOnly = false;
};

/**
 * Whether anything outside can reach this client, as a settings page sees it.
 *
 * Reachability cannot be worked out from here. What arrives unsolicited from
 * outside is the only evidence there is, so the core asks a DROP server what
 * address it sees us at and to connect back to the port we advertise, and
 * reports what came of it. This carries that answer to the thread that draws.
 *
 * Lives on the thread that constructed it. It sends the core commands and hears
 * its reachability events, so a view never touches QuickDC::DropCheck and the
 * check never touches a widget.
 *
 * Constructing one measures nothing. A check tells the server this client's
 * address and the port it listens on and has it connect back, which is a
 * disclosure a user chooses: the core runs one at start-up only when
 * [Network] Reachability server is set, and otherwise nothing happens until
 * somebody asks for it here.
 *
 * There is one measurement, not one per window, because the question is about
 * this client's own port rather than about any hub. Build one of these and share
 * it.
 */
class DropBridge final : public QObject {
	Q_OBJECT

public:
	/**
	 * Listens to the core's reachability events, and measures nothing.
	 *
	 * Asks for the verdicts already held to be said again, since a run started
	 * at launch has usually finished before a settings page exists to draw it,
	 * and asking a third party again for an answer already in hand would be a
	 * disclosure for nothing. What the core does with a verdict happens whether
	 * or not a bridge is listening.
	 */
	explicit DropBridge(CoreThread* thread, QObject* parent = nullptr);

	/** Stops listening; a run in flight goes on in the core and is still recorded there. */
	~DropBridge() override;

	/**
	 * The last verdict seen, or one with ran false if there is none yet.
	 *
	 * Here because a check completes when it completes: a page opened after the
	 * start-up one finished would otherwise have nothing to show and no way to
	 * get it but to measure again, which is a request to a third party for an
	 * answer that is already in hand.
	 */
	ReachabilityVerdict verdict(bool ipv6) const;

	/**
	 * Measure again, from the settings as they stand now.
	 *
	 * The port and the server are read by the core rather than taken from here,
	 * so that what is tested is what the core is actually listening on rather
	 * than what a form last wrote.
	 *
	 * Returns at once. The outcome arrives as verdictChanged() or checkFailed(),
	 * and a check involves a stranger's server connecting back, so that is
	 * seconds away rather than milliseconds. Nothing here may block: a settings
	 * page draws once without an answer and again when one arrives.
	 *
	 * A core that is not running is reported through checkFailed() at once rather
	 * than by saying nothing: somebody is waiting in front of a settings page, and
	 * silence reads as a check that is still running.
	 */
	void recheck();

	/** Pass on what the platform says about the network; the core decides what to check. */
	void noteNetworkChange(bool online);

	/**
	 * Ask a DROP server what address it sees this client at, and nothing further.
	 *
	 * For passive mode, where the address the world sees is worth showing but
	 * nothing may dial the peer port: a client that has announced it cannot be
	 * connected to must not have a stranger connect to it in order to fill in a
	 * field. No session is opened and no port is probed.
	 *
	 * The answer arrives as verdictChanged() like any other, carrying
	 * ReachabilityVerdict::addressOnly - so what a view may draw from it is the
	 * address, and nothing about whether anything can get in.
	 *
	 * Takes the family where recheck() does not, because a check starts a sequence
	 * the core carries on with the other family by itself and a lookup promises no
	 * such thing: a page that wants both addresses asks for both.
	 *
	 * Returns at once, and refuses through checkFailed() where there is no core to
	 * ask or no server configured to ask - the sentences a check is refused with,
	 * for the same reason it is refused out loud: somebody is waiting in front of a
	 * settings page, and silence there reads as a request still running.
	 */
	void lookupPublicAddress(bool ipv6 = false);

signals:
	/**
	 * A run has begun; one of the two below will follow it.
	 *
	 * Emitted for every run, not only for one this bridge asked for. The core
	 * measures on its own - at start-up, when a measurement has aged out, and again
	 * whenever a gateway grants a mapping - and a page that only knew about the
	 * runs it started itself would sit still through all of those and then change
	 * its mind with no warning.
	 */
	void checkStarted(bool ipv6);

	/** A run completed, whatever it concluded. */
	void verdictChanged(const Qdc::ReachabilityVerdict& verdict);

	/**
	 * A run could not be completed, and nothing was established.
	 *
	 * 'reason' is a sentence fit to show whoever is looking at the connection
	 * settings. It says that nobody measured, not that this client is
	 * unreachable, and the two must not be drawn as the same thing.
	 */
	void checkFailed(const QString& reason);

private:
	void onCoreEvent(const QuickDC::Api::Event& event);

	/**
	 * Guarded, because the core thread is not this object's to outlive: both are
	 * often children of the same parent, and Qt destroys them in the order they
	 * were made, which puts the thread first.
	 */
	QPointer<CoreThread> m_thread;

	/**
	 * One per family, because one run answers for one of them.
	 *
	 * Held apart rather than as the last verdict seen: the two arrive one after
	 * the other, so a single slot would leave a page showing whichever finished
	 * last and claiming it described the client.
	 */
	ReachabilityVerdict m_verdictV4;
	ReachabilityVerdict m_verdictV6;
};

}

/*
 * Registered so that a queued connection can carry one. Everything here emits
 * on this object's own thread, but a receiver living on a third one - a worker
 * asked to write the verdict somewhere - connects across threads, and Qt cannot
 * queue a type it has never been told about.
 */
Q_DECLARE_METATYPE(Qdc::ReachabilityVerdict)

#endif // HAVE_QUICKDC_QUI_DROPBRIDGE_H

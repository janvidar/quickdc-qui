/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_NETWORKWATCH_H
#define HAVE_QUICKDC_QUI_NETWORKWATCH_H

#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

namespace Qdc {

/**
 * Says when the network underneath us has changed, so that whatever was worked
 * out about it can be worked out again.
 *
 * What a reachability check learns - the address the world sees, whether the
 * gateway will forward a port, whether anything answers at all - is true of one
 * network and of no other. Walk a laptop from a wired desk to a cafe's WiFi and
 * every one of those answers is stale, and nothing in the client would notice:
 * the listening socket stays open on an address that no longer routes and the
 * user is told they are connectable until they try.
 *
 * A change here is a hint to check again, and nothing more. It carries no
 * detail, because the coarse state Qt reports is not what a caller wants to act
 * on - the caller wants to run its own probe and believe that instead.
 *
 * Two things are worth knowing before relying on this.
 *
 * It lives on the thread that drew it, and that has to be the thread with the
 * event loop. QNetworkInformation delivers through signals and the timer below
 * needs to fire, and the core thread has neither: CoreThread::run() drives the
 * reactor itself and never calls exec(), so an instance made over there would
 * sit silent forever.
 *
 * And the addresses are watched by asking, not by being told. Qt reports
 * reachability and the kind of link but never the local addresses, so a DHCP
 * lease renewed onto a different address, or a VPN coming up over an interface
 * that was already online, would otherwise pass without a word - the link never
 * dropped and the medium never changed, yet everything measured about the old
 * address is now about somebody else's. Those are found by comparing the
 * addresses against the last set seen, which is why this reports a change up to
 * a poll late rather than as it happens.
 */
class NetworkWatch final : public QObject {
	Q_OBJECT

public:
	/**
	 * Loads Qt's network backend and starts listening for transitions.
	 *
	 * The backend is process wide and static, so loading it a second time is
	 * loading it once: another watcher costs nothing, and no instance of this owns
	 * what QNetworkInformation::instance() returns. A platform Qt has no backend
	 * for leaves a watcher that never fires rather than a failure to report; see
	 * isWatching().
	 *
	 * Every transition the backend offers is listened for - reachability, the
	 * transport medium, metered, captive portal - because any of them can be the
	 * first sign of a different network and none of them is reliably the last.
	 * Reachability alone misses a switch between two working links; the transport
	 * medium alone misses a cable pulled and put back. Since the change is only
	 * reported once the quiet period ends, listening too widely costs a timer
	 * restart and nothing else.
	 *
	 * Each connection is made with this as its context object, so all of them go
	 * when this does - the sender is the process' own and outlives us.
	 */
	explicit NetworkWatch(QObject* parent = nullptr);
	~NetworkWatch() override;

	/**
	 * Whether Qt's backend was found, and transitions are reported as they happen.
	 *
	 * False is not an error and needs no handling beyond deciding what to tell the
	 * user. It does not mean nothing is watched: the addresses are polled either
	 * way, so a platform Qt has no backend for still reports a network being
	 * swapped - a poll later, and only once the addresses differ, which a link
	 * dropped and restored onto the same lease does not.
	 */
	bool isWatching() const;

	/**
	 * The addresses worth comparing, as one string, in a fixed order.
	 *
	 * Loopback is dropped because it is on every machine and changes on none, and
	 * link-local with it: an address the host gave itself is what is there while
	 * DHCP is still deciding, so keeping it would report the negotiation as a
	 * change of network and then report the real answer as a second one.
	 *
	 * What is left is sorted, so that two enumerations of an unchanged machine
	 * compare equal whatever order the platform returned them in - the interface
	 * list is not ordered by contract, and comparing it as given would report a
	 * change every time the order moved.
	 *
	 * Takes the addresses as text rather than as QHostAddress so that this header
	 * names nothing from Qt's network module, which is a private dependency of
	 * this library: a signature mentioning one would put that module on the
	 * include path of everything that reads this. Nothing is lost by it - an
	 * address round trips through its own string form, scope and all.
	 *
	 * Public because it is the whole of what counts as a change, and testable
	 * only from outside: real interfaces cannot be made to change on demand.
	 */
	static QString addressFingerprint(const QStringList& addresses);

	/**
	 * Whether the machine looks reachable right now.
	 *
	 * The same judgement connectivityChanged() waits for, exposed so that a
	 * caller can skip a first check it already knows will fail. True when there
	 * is no backend, since a watcher that knows nothing should not be the reason
	 * something is not attempted.
	 */
	bool isOnline() const;

signals:
	/**
	 * The network changed, and is up again. Check whatever was cached about it.
	 *
	 * One signal per change, however many transitions the change was made of.
	 * Switching networks is not one event: the old link goes down, the transport
	 * medium changes, an address is negotiated, reachability climbs from
	 * disconnected through the local link to online, and each step arrives
	 * separately over a second or two. A probe fired at each of them would mean
	 * several STUN round trips racing each other to answer a question only the
	 * last of them was asked, so they are collected and reported once the line
	 * has stopped moving.
	 *
	 * Never emitted while still disconnected. A check started then can only fail
	 * and would replace a stale answer with a wrong one; the settled state that
	 * follows brings its own signal.
	 */
	void connectivityChanged();

private:
	/**
	 * A transition arrived: start the quiet period over.
	 *
	 * Restarted rather than left to run, which is the difference between reporting
	 * the state the network ended up in and reporting the first step of getting
	 * there. A link that flaps for a minute is worth exactly one check, and it is
	 * the one after the flapping stops.
	 */
	void noteTransition();

	/**
	 * The quiet period elapsed. Report it, if there is anything to report to.
	 *
	 * Says nothing while the machine is still offline: the link coming back is
	 * itself a transition, so the check that was owed here happens then, against a
	 * network that can answer it.
	 */
	void settle();

	/**
	 * Enumerate the addresses, and treat a different set as a transition.
	 *
	 * Reported through the same quiet period as everything else, so that an
	 * address arriving at the end of a switch this had already noticed is one
	 * check rather than a second.
	 */
	void pollAddresses();

	/**
	 * Record what the backend now says a state is, and report a change only where
	 * there was one.
	 *
	 * Qt announces the state it found as though it had just changed: a backend
	 * finishes loading after the object asking is built, so the first thing heard
	 * about an unchanged network is that it is online and on WiFi. Acting on those
	 * would mean a measurement a few seconds into every launch, of the network the
	 * client started on, which is the one already measured at start-up.
	 *
	 * So a value equal to the one held says nothing, and neither does the first
	 * value after 'not known' - that is the backend working out what was true
	 * before anybody asked. Only a move between two established values is a
	 * network that changed under us.
	 */
	void noteLearned(int& held, int now);

	/**
	 * The same, for the states Qt reports as a plain bool.
	 *
	 * These have no value for 'not known', so the first one heard is taken as
	 * what was already the case rather than as a change to it.
	 */
	void noteFlag(int& held, bool now);

	QTimer* m_settle;

	/** Drives pollAddresses(); see the note above about being told versus asking. */
	QTimer* m_poll;

	/**
	 * The last set seen, to compare the next one against.
	 *
	 * Taken before the polling starts rather than left empty, or the first poll
	 * would find every address on the machine to be new and report a network
	 * that had not changed.
	 */
	QString m_addresses;

	/**
	 * The last state heard from the backend, held as int so that this header
	 * names nothing from Qt's network module; see addressFingerprint() for why
	 * that matters. The two enums are seeded from the backend when it loads, and
	 * the two flags start at -1 for a value never heard.
	 */
	int m_reachability = 0;
	int m_medium = 0;
	int m_metered = -1;
	int m_captive = -1;

	bool m_watching = false;
};

}

#endif // HAVE_QUICKDC_QUI_NETWORKWATCH_H

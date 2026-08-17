/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_NETWORKWATCH_H
#define HAVE_QUICKDC_QUI_NETWORKWATCH_H

#include <QObject>

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
 * And it does not see an address change on its own. QNetworkInformation reports
 * reachability and the kind of link, not the local addresses, so a DHCP lease
 * renewed onto a different address, or a VPN coming up over an interface that
 * was already online, can pass without a word from here. The transitions it
 * does report - a link dropping and returning, WiFi replacing Ethernet - cover
 * the case this exists for. Anything needing more has to poll.
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
	 * Whether a backend was found and transitions will actually be reported.
	 *
	 * False is not an error and needs no handling beyond deciding what to tell
	 * the user: on a platform Qt has no backend for, this is a watcher that
	 * never fires, and everything else carries on working exactly as it did
	 * before anybody asked to be told about the network.
	 */
	bool isWatching() const;

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

	QTimer* m_settle;
	bool m_watching = false;
};

}

#endif // HAVE_QUICKDC_QUI_NETWORKWATCH_H

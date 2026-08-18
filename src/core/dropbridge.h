/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_DROPBRIDGE_H
#define HAVE_QUICKDC_QUI_DROPBRIDGE_H

#include "relay.h"

#include <QDateTime>
#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <memory>

namespace Qdc {

class CoreThread;
class DropWatcher;

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
};

/**
 * Whether anything outside can reach this client, as a settings page sees it.
 *
 * Reachability cannot be worked out from here. What arrives unsolicited from
 * outside is the only evidence there is, so the core asks a DROP server what
 * address it sees us at and to connect back to the port we advertise, and
 * reports what came of it. This carries that answer to the thread that draws.
 *
 * Lives on the thread that constructed it. The core side half is a DropWatcher,
 * built and torn down inside jobs on the core thread, and recheck() is a job
 * too - so a view never touches QuickDC::DropCheck, and the check never touches
 * a widget.
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
	 * Builds the core side watcher, and measures nothing.
	 *
	 * The watcher belongs to the core thread and has to be built there, in a job
	 * that is posted rather than waited on: this is a constructor a window is being
	 * built out of, and waiting would cost it a whole core turn at whatever the
	 * reactor happens to be busy with. Nothing is missed by not waiting. A run
	 * started at start-up reports through the core's own listener, which records
	 * it, and that recorded verdict is replayed through the watcher as soon as it
	 * exists - otherwise a settings page opened a moment later would have nothing
	 * to draw and no way to get it but to ask a third party for an answer that is
	 * already in hand.
	 *
	 * From then on the core reports to this watcher as well as to the listener it
	 * set up for itself, which goes on recording the measurement and asking the
	 * hubs to republish. Attaching displaces nothing and cancels nothing: what the
	 * core does with a verdict must not depend on whether a window is open, and a
	 * run in flight when this is built is the one the core started on launch.
	 */
	explicit DropBridge(CoreThread* thread, QObject* parent = nullptr);

	/**
	 * Detaches the relay and posts the watcher's deletion.
	 *
	 * Nothing blocks here either. The core may still call the watcher about a run
	 * it is finishing, but the call is addressed to the relay rather than to this
	 * object and the relay is detached first, so an event landing between the
	 * detach and the job running has nowhere to go. The watcher is taken off the
	 * check before it is freed, or the core would report a finishing run to a
	 * listener that no longer exists - which leaves the core's own listener in
	 * place, so a measurement taken after the last window closes is still recorded.
	 *
	 * The job finds the watcher through the shared box rather than through a
	 * member, because by the time it runs there is no bridge left to read one from.
	 * A core that has already stopped drops the job and leaks the watcher, which is
	 * the lesser evil: the object is small, and the alternative is deleting
	 * something the reactor may be inside, on a thread that no longer runs it.
	 */
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
	 * The port and the server are read on the core thread inside the job rather
	 * than taken from here, so that what is tested is what the core is actually
	 * listening on rather than what a form last wrote.
	 *
	 * Returns at once. The outcome arrives as verdictChanged() or checkFailed(),
	 * and a check involves a stranger's server connecting back, so that is
	 * seconds away rather than milliseconds. Nothing here may block: a settings
	 * page draws once without an answer and again when one arrives.
	 *
	 * The job captures nothing of this thread's and finds the watcher through the
	 * shared box, so a check asked for before the core thread got round to building
	 * one still finds it - the job that builds it is already queued.
	 *
	 * Where there is no watcher at all, which is only reachable when the core was
	 * not running when this bridge was built, checkFailed() says so rather than
	 * nothing being said: somebody is waiting in front of a settings page, and
	 * silence reads as a check that is still running. That sentence is built on the
	 * thread that is going to read it, because a QString shares its buffer with its
	 * copies and one made on the core thread would cross between the threads still
	 * held over there.
	 */
	void recheck();

	/**
	 * Bring the gateway mapping into line with [Network] UseUPnP as it now
	 * stands, starting a search or releasing what was granted.
	 *
	 * Called after the setting is written, so that turning it on forwards the
	 * port now rather than at the next start: a user who turned it on because a
	 * check said passive is waiting for that check to say something else.
	 *
	 * Returns at once and reports nothing. Whether the port ended up forwarded
	 * is answered by measuring again, which is the only thing that can answer
	 * it - a gateway reports success without knowing whether anything above it
	 * honours the mapping.
	 */
	void applyForwarding();

signals:
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
	friend class DropWatcher;

	/**
	 * What the teardown job deletes, and what recheck() finds the watcher
	 * through.
	 *
	 * Shared rather than a member because by the time either job runs there may
	 * be no bridge left to read one from: the box outlives this object for
	 * exactly as long as the jobs holding it do. Only ever touched on the core
	 * thread.
	 */
	struct WatchBox {
		DropWatcher* watcher = nullptr;
	};

	/**
	 * Guarded, because the core thread is not this object's to outlive: both are
	 * often children of the same parent, and Qt destroys them in the order they
	 * were made, which puts the thread first.
	 */
	QPointer<CoreThread> m_thread;

	std::shared_ptr<WatchBox> m_watch;

	/** Where an event from the core lands; see Qdc::Relay. */
	std::shared_ptr<Relay> m_relay;

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

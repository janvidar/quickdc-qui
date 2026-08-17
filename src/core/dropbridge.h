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

	/* The per-port outcomes, named. Empty where the test was not run. */
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
	explicit DropBridge(CoreThread* thread, QObject* parent = nullptr);
	~DropBridge() override;

	/**
	 * The last verdict seen, or one with ran false if there is none yet.
	 *
	 * Here because a check completes when it completes: a page opened after the
	 * start-up one finished would otherwise have nothing to show and no way to
	 * get it but to measure again, which is a request to a third party for an
	 * answer that is already in hand.
	 */
	ReachabilityVerdict verdict() const;

	/**
	 * Measure again, from the settings as they stand now.
	 *
	 * The port and the server are read on the core thread inside the job rather
	 * than taken from here, so that what is tested is what the core is actually
	 * listening on rather than what a form last wrote.
	 *
	 * Returns at once. The outcome arrives as verdictChanged() or checkFailed(),
	 * and a check involves a stranger's server connecting back, so that is
	 * seconds away rather than milliseconds.
	 */
	void recheck();

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

	ReachabilityVerdict m_verdict;
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

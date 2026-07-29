/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_CORETHREAD_H
#define HAVE_QUICKDC_QUI_CORETHREAD_H

#include <QString>
#include <QThread>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

namespace QuickDC {
class Core;
}

namespace Samurai {
namespace IO {
namespace Net {
class Notifier;
}
}
}

namespace Qdc {

/**
 * Owns the QuickDC core and the thread it runs on.
 *
 * The core is a single threaded reactor. Core::run() is one turn of it, and
 * everything it reaches - the protocol sessions, the download queue, both
 * preference stores, the socket monitor - is unsynchronised and belongs to
 * whichever thread constructed it. It also spends most of a turn blocked in
 * poll() with a 250ms timeout, which is why it cannot just be pumped from a
 * QTimer: the user interface would stall for a quarter of a second at a time.
 *
 * So the core gets a thread of its own, and both directions are marshalled.
 *
 *   towards the core   post() and invoke() append to a job queue and wake the
 *                      reactor through a socketpair, so a pending poll returns
 *                      at once rather than sitting out its timeout.
 *   towards the views  the bridges copy each event into a value type and hand
 *                      it to QMetaObject::invokeMethod, which puts it on the
 *                      receiving thread's event queue.
 *
 * The rule that makes the rest of it safe: nothing outside a posted job may
 * touch core(), or anything reachable from it.
 */
class CoreThread final : public QThread {
	Q_OBJECT

public:
	explicit CoreThread(QObject* parent = nullptr);
	~CoreThread() override;

	/**
	 * Build the core from explicit files instead of ~/.quickdc, and skip the
	 * share scan and the listening socket with it.
	 *
	 * This selects the core's lighter constructor. It exists so that a test can
	 * run against files of its own rather than the user's, and has to be called
	 * before startAndWait().
	 *
	 * @param queueFile may be empty, in which case there is no download queue
	 *        and nothing that needs one will work.
	 */
	void setPreferenceFiles(const QString& configFile,
	                        const QString& sessionFile,
	                        const QString& queueFile = QString());

	/**
	 * Start the thread and wait for the core to finish constructing - which
	 * reads the configuration, scans the shares and opens the listening socket,
	 * so it is not instant.
	 *
	 * @return false if the core could not be built, and then failureReason()
	 *         says what went wrong.
	 */
	bool startAndWait();

	/** Why startAndWait() returned false; empty otherwise. */
	QString failureReason() const;

	/**
	 * Stop the reactor and destroy the core, which is what flushes the download
	 * queue and the hash cache to disk. Idempotent, and the destructor calls it.
	 *
	 * Jobs already queued are run before the core goes away, so an invoke()
	 * racing a shutdown still returns rather than waiting for a thread that has
	 * finished.
	 */
	void shutdown();

	/**
	 * Whether the core is up and jobs will be run. Not the same question as the
	 * inherited QThread::isRunning(), which is true for a thread that is still
	 * constructing the core or already tearing it down.
	 */
	bool isCoreRunning() const;

	/**
	 * Run 'job' on the core thread. Safe from any thread, including from inside
	 * another job. Jobs run in the order they were posted, between turns of the
	 * reactor.
	 *
	 * A job posted once shutdown has begun is dropped rather than run, because
	 * the core it would touch is already going away.
	 */
	void post(std::function<void()> job);

	/**
	 * The same, but wait for the job to finish. Runs 'job' directly when called
	 * from the core thread, so a job may call this without deadlocking.
	 *
	 * @return false if the core is not running, in which case 'job' did not run
	 *         at all.
	 */
	bool invoke(std::function<void()> job);

	/**
	 * The core.
	 *
	 * Null unless called from the core thread, deliberately: a caller that has
	 * forgotten to go through post() or invoke() then fails immediately and
	 * always, rather than corrupting the reactor from time to time.
	 */
	QuickDC::Core* core() const;

signals:
	/** The core is up. Delivered on the thread that connected to it. */
	void coreStarted();

	/** The core has been destroyed. */
	void coreStopped();

	/** The core could not be built; the same text as failureReason(). */
	void coreFailed(const QString& reason);

protected:
	void run() override;

private:
	/**
	 * Run every job queued so far.
	 *
	 * The lock is never held while a job runs, both because a job may post
	 * another and because a job is free to take as long as it likes.
	 */
	void drainJobs();

	/** Return the reactor from poll() early, if we managed to build a notifier. */
	void wake();

	enum class State {
		Idle,
		Starting,
		Running,
		Failed,
		Stopping,
		Stopped
	};

	QuickDC::Core* m_core = nullptr;
	std::shared_ptr<Samurai::IO::Net::Notifier> m_notifier;

	mutable std::mutex m_mutex;
	std::condition_variable m_stateChanged;
	std::deque<std::function<void()>> m_jobs;
	State m_state = State::Idle;
	QString m_failure;

	/** Read every turn of the reactor, so it cannot be guarded by m_mutex. */
	std::atomic<bool> m_stopRequested{false};

	QString m_configFile;
	QString m_sessionFile;
	QString m_queueFile;
	bool m_useExplicitFiles = false;
};

}

#endif // HAVE_QUICKDC_QUI_CORETHREAD_H

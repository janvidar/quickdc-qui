/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_CORETHREAD_H
#define HAVE_QUICKDC_QUI_CORETHREAD_H

#include "relay.h"

#include <QMetaObject>
#include <QString>
#include <QThread>

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace QuickDC {
class Core;
namespace Api { struct Command; }
}

namespace Qdc {

class CoreChannel;

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
 *   towards the core   post(), call() and runOnCoreBlocking() hand a job to the
 *                      core's own inbox (QuickDC::Inbox), which wakes the
 *                      reactor so a pending poll returns at once rather than
 *                      sitting out its timeout.
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
	 * Jobs already queued are run before the core goes away, so a blocking call
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
	 * Ask the core something and be told the answer later.
	 *
	 * This is the only sanctioned round trip. 'work' runs on the core thread and
	 * returns a value type; 'answer' is handed that value on the relay's own
	 * thread, through the event queue, and is not run at all if the relay has
	 * been detached in the meantime.
	 *
	 * Nothing waits. A view that needs the answer to draw something has to draw
	 * without it first and again when it arrives, because the alternative -
	 * waiting for the reactor to finish whatever turn it is in - is the interface
	 * freezing for as long as that takes.
	 *
	 * Everything crossing between the threads is copied. 'work' must not capture
	 * anything owned by the calling thread by reference, and must return
	 * something that is safe to carry: a QString is not, since it shares its
	 * buffer with the copy the closure took.
	 */
	template<typename Work, typename Answer>
	void call(std::shared_ptr<Relay> relay, Work work, Answer answer)
	{
		if (!relay) return;

		post([relay = std::move(relay), work = std::move(work),
		      answer = std::move(answer)]() mutable {
			auto value = work();

			/*
			 * The relay rather than the bridge, and held by the closure until the
			 * answer has been delivered: that is what makes the receiver certain
			 * to still be there when Qt looks at it.
			 */
			Relay* target = relay.get();

			QMetaObject::invokeMethod(target,
				[relay = std::move(relay), answer = std::move(answer),
				 value = std::move(value)]() mutable {
					if (!relay->detached()) answer(std::move(value));
				}, Qt::QueuedConnection);
		});
	}

	/**
	 * The same, but wait for the job to finish. Runs 'job' directly when called
	 * from the core thread, so a job may call this without deadlocking.
	 *
	 * Waiting means waiting out the rest of the reactor's current turn, which is
	 * bounded by nothing: a turn writes transfer chunks to disk, rewrites the
	 * download queue, compresses the share's file list and answers other
	 * people's searches out of the whole share index. Nothing drawn on screen may
	 * call this. It is here for setting a bridge up and taking it down, where the
	 * order matters more than the wait, and call() is the answer everywhere else.
	 *
	 * @param timeout how long to wait before giving up, in milliseconds.
	 * @return false if the core is not running or the wait expired, in which case
	 *         'job' either did not run at all or is still queued.
	 * @throws whatever 'job' threw, rethrown on this thread.
	 */
	bool runOnCoreBlocking(std::function<void()> job, int timeout = 30000);

	/**
	 * The core.
	 *
	 * Null unless called from the core thread, deliberately: a caller that has
	 * forgotten to go through a job then fails immediately and always, rather than
	 * corrupting the reactor from time to time.
	 */
	QuickDC::Core* core() const;

	/**
	 * Hand 'command' to the core; what comes of it arrives as coreEvent().
	 *
	 * @return false where the core is not running and the command was dropped.
	 */
	bool submit(const QuickDC::Api::Command& command);

	/** Where what the core reports arrives, as values; see corechannel.h. */
	CoreChannel* channel() const { return m_channel; }

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
	enum class State {
		Idle,
		Starting,
		Running,
		Failed,
		Stopping,
		Stopped
	};

	CoreChannel* m_channel;

	/** Set and cleared under m_mutex, so post() never reaches a core being deleted. */
	QuickDC::Core* m_core = nullptr;

	mutable std::mutex m_mutex;
	std::condition_variable m_stateChanged;
	State m_state = State::Idle;
	QString m_failure;

	/** For a shutdown() that arrives before there is a core to tell. */
	bool m_stopRequested = false;

	QString m_configFile;
	QString m_sessionFile;
	QString m_queueFile;
	bool m_useExplicitFiles = false;
};

}

#endif // HAVE_QUICKDC_QUI_CORETHREAD_H

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "corethread.h"

#include <api/core.h>

#include <QDebug>
#include <QElapsedTimer>

#include <chrono>
#include <exception>
#include <utility>

namespace {

/**
 * What a blocking call and the job it queued say to each other.
 *
 * Shared rather than left on the waiter's stack, because the two ends do not
 * finish together: a wait that expires returns while the job is still queued, and
 * a condition variable the waiter has already destroyed is not one the core
 * thread can safely notify.
 */
struct Rendezvous {
	std::mutex mutex;
	std::condition_variable changed;

	bool started = false;
	bool done = false;

	/** Set by a waiter that gave up, and the job's instruction not to run. */
	bool abandoned = false;

	std::exception_ptr failure;
};

/**
 * How long a blocking call may park the caller before it is worth saying so.
 *
 * Generous on purpose: registering a listener behind a reactor mid-turn is
 * allowed to cost a turn. Anything past this is a call that does not belong on
 * the thread that draws.
 */
const int SLOW_CALL_MS = 100;

}

Qdc::CoreThread::CoreThread(QObject* parent)
	: QThread(parent)
{
	setObjectName(QStringLiteral("quickdc-core"));
}

Qdc::CoreThread::~CoreThread()
{
	shutdown();
}

void Qdc::CoreThread::setPreferenceFiles(const QString& configFile,
                                         const QString& sessionFile,
                                         const QString& queueFile)
{
	m_configFile = configFile;
	m_sessionFile = sessionFile;
	m_queueFile = queueFile;
	m_useExplicitFiles = true;
}

bool Qdc::CoreThread::startAndWait()
{
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		if (m_state != State::Idle) return m_state == State::Running;
		m_state = State::Starting;
	}

	start();

	std::unique_lock<std::mutex> lock(m_mutex);
	m_stateChanged.wait(lock, [this] {
		return m_state != State::Starting;
	});

	return m_state == State::Running;
}

QString Qdc::CoreThread::failureReason() const
{
	std::lock_guard<std::mutex> guard(m_mutex);
	return m_failure;
}

bool Qdc::CoreThread::isCoreRunning() const
{
	std::lock_guard<std::mutex> guard(m_mutex);
	return m_state == State::Running;
}

QuickDC::Core* Qdc::CoreThread::core() const
{
	/*
	 * No lock: m_core is only ever written by run(), on this thread, so a
	 * caller that passes the test is the thread that wrote it. Anyone else is
	 * told there is no core, which is the answer they are entitled to act on.
	 */
	return (QThread::currentThread() == this) ? m_core : nullptr;
}

void Qdc::CoreThread::shutdown()
{
	Q_ASSERT_X(QThread::currentThread() != this, "CoreThread::shutdown",
	           "a job cannot shut down the thread it is running on");

	{
		std::lock_guard<std::mutex> guard(m_mutex);

		/* Never started, so there is no thread to join. */
		if (m_state == State::Idle) return;
	}

	{
		std::lock_guard<std::mutex> guard(m_mutex);
		m_stopRequested = true;
		if (m_core) m_core->requestStop();
	}

	/*
	 * Unconditional, including for a core that failed to build or has already
	 * finished: run() having returned is not the same as the thread being
	 * joined, and ~QThread aborts on one that is still going.
	 */
	wait();
}

void Qdc::CoreThread::post(std::function<void()> job)
{
	if (!job) return;

	std::lock_guard<std::mutex> guard(m_mutex);
	if (m_state != State::Running || !m_core) return;
	m_core->post(std::move(job));
}

bool Qdc::CoreThread::runOnCoreBlocking(std::function<void()> job, int timeout)
{
	if (!job) return false;

	if (QThread::currentThread() == this)
	{
		job();
		return true;
	}

	/*
	 * One per call, so two callers waiting at once do not wake each other, and
	 * shared so that whichever end finishes last is the one that destroys it.
	 */
	auto meeting = std::make_shared<Rendezvous>();

	{
		std::lock_guard<std::mutex> guard(m_mutex);
		if (m_state != State::Running || !m_core) return false;

		const bool queued = m_core->post([meeting, job = std::move(job)] {
			{
				std::lock_guard<std::mutex> inner(meeting->mutex);

				/*
				 * The waiter gave up before this job was reached, and its stack -
				 * which is what the job captured - is gone with it. Not running
				 * the job is the whole of what abandoning one means.
				 */
				if (meeting->abandoned) return;

				meeting->started = true;
			}

			std::exception_ptr failure;

			try
			{
				job();
			}
			catch (...)
			{
				failure = std::current_exception();
			}

			{
				/*
				 * Held across the notify rather than released before it: the
				 * waiter wakes, sees the flag and returns, and a notify made
				 * after that lands on a condition variable nothing owns.
				 */
				std::lock_guard<std::mutex> inner(meeting->mutex);
				meeting->failure = failure;
				meeting->done = true;
				meeting->changed.notify_one();
			}
		});

		if (!queued) return false;
	}

	QElapsedTimer parked;
	parked.start();

	bool answered = false;

	{
		std::unique_lock<std::mutex> lock(meeting->mutex);

		const auto deadline = std::chrono::steady_clock::now()
			+ std::chrono::milliseconds(timeout);

		/*
		 * A job that has started cannot be abandoned - it is reading this thread's
		 * stack - so the deadline only decides whether a job still queued is
		 * called off. Once it is running there is nothing to do but see it out.
		 */
		answered = meeting->changed.wait_until(lock, deadline, [&meeting] {
			return meeting->done;
		});

		if (!answered && meeting->started)
		{
			meeting->changed.wait(lock, [&meeting] { return meeting->done; });
			answered = true;
		}

		if (!answered) meeting->abandoned = true;

		if (meeting->failure)
		{
			std::exception_ptr failure = meeting->failure;
			lock.unlock();
			std::rethrow_exception(failure);
		}
	}

	const qint64 spent = parked.elapsed();

	if (spent > SLOW_CALL_MS)
	{
		qWarning("a blocking core call held the calling thread for %lld ms; "
		         "if this was the thread that draws, that was a frozen window",
		         static_cast<long long>(spent));
	}

	return answered;
}

void Qdc::CoreThread::run()
{
	QString failure;
	QuickDC::Core* built = nullptr;

	try
	{
		if (m_useExplicitFiles)
		{
			built = new QuickDC::Core(m_configFile.toStdString(),
			                          m_sessionFile.toStdString(),
			                          m_queueFile.toStdString());
		}
		else
		{
			built = new QuickDC::Core();
		}
	}
	catch (const std::exception& e)
	{
		failure = QString::fromUtf8(e.what());
	}
	catch (...)
	{
		failure = QStringLiteral("the QuickDC core threw an unrecognised exception");
	}

	if (!built && failure.isEmpty())
		failure = QStringLiteral("the QuickDC core could not be constructed");

	if (!built)
	{
		{
			std::lock_guard<std::mutex> guard(m_mutex);
			m_state = State::Failed;
			m_failure = failure;
		}
		m_stateChanged.notify_all();
		emit coreFailed(failure);
		return;
	}

	{
		std::lock_guard<std::mutex> guard(m_mutex);
		m_core = built;

		/* A shutdown() that came in while the core was being built. */
		if (m_stopRequested) m_core->requestStop();
	}

	/*
	 * Running only once the core's inbox accepts work, so that a post() made the
	 * moment startAndWait() returns is never refused.
	 */
	m_core->runUntilStopped([this] {
		{
			std::lock_guard<std::mutex> guard(m_mutex);
			m_state = State::Running;
		}
		m_stateChanged.notify_all();
		emit coreStarted();
	});

	/*
	 * The inbox has closed and run what it held, so a caller blocked in
	 * runOnCoreBlocking() has had its job run or been told the core stopped.
	 */
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		m_state = State::Stopping;
		m_core = nullptr;
	}

	delete built;

	{
		std::lock_guard<std::mutex> guard(m_mutex);
		m_state = State::Stopped;
	}
	m_stateChanged.notify_all();

	emit coreStopped();
}

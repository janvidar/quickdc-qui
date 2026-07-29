/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "corethread.h"

#include <api/core.h>
#include <samurai/io/net/notifier.h>

#include <exception>
#include <utility>

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

	m_stopRequested.store(true, std::memory_order_release);
	wake();

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

	{
		std::lock_guard<std::mutex> guard(m_mutex);
		if (m_state != State::Running) return;
		m_jobs.push_back(std::move(job));
	}

	wake();
}

bool Qdc::CoreThread::invoke(std::function<void()> job)
{
	if (!job) return false;

	if (QThread::currentThread() == this)
	{
		job();
		return true;
	}

	/*
	 * Local rather than shared, so two callers waiting at once do not wake each
	 * other. Safe to capture by reference because we do not return until the
	 * wrapper has run, and the wrapper is either run by the reactor or by the
	 * final drain in run() - a queued job is never simply dropped.
	 */
	std::mutex done_mutex;
	std::condition_variable done_changed;
	bool done = false;

	{
		std::lock_guard<std::mutex> guard(m_mutex);
		if (m_state != State::Running) return false;

		m_jobs.push_back([&] {
			job();
			{
				std::lock_guard<std::mutex> inner(done_mutex);
				done = true;
			}
			done_changed.notify_one();
		});
	}

	wake();

	std::unique_lock<std::mutex> lock(done_mutex);
	done_changed.wait(lock, [&done] { return done; });
	return true;
}

void Qdc::CoreThread::wake()
{
	/*
	 * Copied out under the lock: shutdown() releases the notifier on the core
	 * thread while another thread may be in here.
	 */
	std::shared_ptr<Samurai::IO::Net::Notifier> notifier;
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		notifier = m_notifier;
	}

	if (notifier) notifier->notify();
}

void Qdc::CoreThread::drainJobs()
{
	for (;;)
	{
		std::function<void()> job;
		{
			std::lock_guard<std::mutex> guard(m_mutex);
			if (m_jobs.empty()) return;
			job = std::move(m_jobs.front());
			m_jobs.pop_front();
		}

		job();
	}
}

void Qdc::CoreThread::run()
{
	QString failure;

	try
	{
		if (m_useExplicitFiles)
		{
			m_core = new QuickDC::Core(m_configFile.toStdString(),
			                           m_sessionFile.toStdString(),
			                           m_queueFile.toStdString());
		}
		else
		{
			m_core = new QuickDC::Core();
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

	if (!m_core && failure.isEmpty())
		failure = QStringLiteral("the QuickDC core could not be constructed");

	if (!m_core)
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

	/*
	 * Built after the core, because the core takes the socket monitor that the
	 * notifier registers itself with.
	 *
	 * The callback is empty on purpose. Waking the reactor is the whole job: it
	 * runs from inside the monitor's dispatch, part way through a turn, and a
	 * job that deleted a hub or a socket from there would be mutating the
	 * registry the monitor is walking. Jobs are run by the drainJobs() below
	 * instead, once the turn is over and nothing is iterating anything.
	 *
	 * A null notifier is not fatal and so is not checked - it only costs
	 * latency, up to one poll timeout, because jobs are drained every turn
	 * regardless. It is null where there is no socketpair(), meaning Windows.
	 */
	{
		std::shared_ptr<Samurai::IO::Net::Notifier> notifier =
			Samurai::IO::Net::Notifier::create([] { });

		std::lock_guard<std::mutex> guard(m_mutex);
		m_notifier = std::move(notifier);
		m_state = State::Running;
	}
	m_stateChanged.notify_all();
	emit coreStarted();

	while (!m_stopRequested.load(std::memory_order_acquire))
	{
		m_core->run();
		drainJobs();
	}

	/*
	 * Refuse new jobs and take the ones already queued in the same breath, so
	 * that a caller blocked in invoke() is either in this batch or was told the
	 * core had stopped. Anything either of these jobs posts is dropped.
	 */
	std::deque<std::function<void()>> remaining;
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		m_state = State::Stopping;
		remaining.swap(m_jobs);
	}

	for (auto& job : remaining)
		job();

	{
		std::lock_guard<std::mutex> guard(m_mutex);
		m_notifier.reset();
	}

	delete m_core;
	m_core = nullptr;

	{
		std::lock_guard<std::mutex> guard(m_mutex);
		m_state = State::Stopped;
	}
	m_stateChanged.notify_all();

	emit coreStopped();
}

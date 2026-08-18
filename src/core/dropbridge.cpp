/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "dropbridge.h"

#include "corethread.h"

#include <api/config.h>
#include <api/core.h>
#include <api/dropverdict.h>
#include <network/portforward.h>

#include <QMetaObject>

#include <string>

namespace {

/** What the core assumes when nothing named a port; see [Network] Port. */
const int DEFAULT_PORT = 1411;

Qdc::ReachabilityMode convert(QuickDC::DropMode mode)
{
	switch (mode)
	{
		case QuickDC::DropMode::Unknown:  return Qdc::ReachabilityMode::Unknown;
		case QuickDC::DropMode::Active:   return Qdc::ReachabilityMode::Active;
		case QuickDC::DropMode::Passive:  return Qdc::ReachabilityMode::Passive;
	}

	return Qdc::ReachabilityMode::Unknown;
}

Qdc::ReachabilityReach convert(QuickDC::DropReach reach)
{
	switch (reach)
	{
		case QuickDC::DropReach::Unknown: return Qdc::ReachabilityReach::Unknown;
		case QuickDC::DropReach::Reached: return Qdc::ReachabilityReach::Reached;
		case QuickDC::DropReach::Blocked: return Qdc::ReachabilityReach::Blocked;
	}

	return Qdc::ReachabilityReach::Unknown;
}

/**
 * A verdict, copied out of what the core lent us.
 *
 * Every string is converted here, on the core thread, and QString::fromStdString
 * is what makes that a copy rather than a second reference: what crosses to the
 * views must share nothing with anything left behind, and the verdict the core
 * holds is overwritten by the next run.
 */
Qdc::ReachabilityVerdict convert(const QuickDC::DropVerdict& verdict)
{
	Qdc::ReachabilityVerdict copied;

	copied.ran             = verdict.ran;
	copied.ipv6            = verdict.ipv6;
	copied.publicAddress   = QString::fromStdString(verdict.publicAddress);
	copied.classification  = QString::fromStdString(verdict.classification);
	copied.mode            = convert(verdict.mode);
	copied.tcp             = QString::fromStdString(verdict.tcp);
	copied.udp             = QString::fromStdString(verdict.udp);
	copied.tcpReached      = convert(verdict.tcpReached);
	copied.udpReached      = convert(verdict.udpReached);
	copied.port            = static_cast<int>(verdict.port);
	copied.reason          = QString::fromStdString(verdict.reason);
	copied.note            = QString::fromStdString(verdict.note);

	copied.remedies.reserve(static_cast<qsizetype>(verdict.remedies.size()));
	for (const std::string& remedy : verdict.remedies)
		copied.remedies.append(QString::fromStdString(remedy));

	if (verdict.observedAt)
	{
		copied.observedAt =
			QDateTime::fromSecsSinceEpoch(static_cast<qint64>(verdict.observedAt));
	}

	return copied;
}

}

namespace Qdc {

/**
 * The core side half of a DropBridge.
 *
 * Built and destroyed inside jobs on the core thread, and only ever called from
 * there: it is what QuickDC::DropCheck reports a completed run to, from inside
 * the reactor and on the reactor's own stack.
 *
 * Every event does the same two things: copy what the core lent us, then hand
 * the copy to the bridge's thread. The copy is not optional. The verdict is a
 * reference to the check's own member, which the next run overwrites, and the
 * strings in it are freed with it.
 */
class DropWatcher final : public QuickDC::DropCheckListener {

public:
	DropWatcher(DropBridge* bridge, std::shared_ptr<Relay> relay)
		: m_bridge(bridge)
		, m_relay(std::move(relay))
	{
	}

	DropWatcher(const DropWatcher&) = delete;
	DropWatcher& operator=(const DropWatcher&) = delete;

	/**
	 * Report a verdict the core already had, as though it had just arrived.
	 *
	 * For the one that the start-up check finished before this bridge existed:
	 * without it a settings page opened a moment later would have nothing to draw
	 * and no way to get it but to ask a third party to measure again.
	 */
	void replay(const QuickDC::DropVerdict& verdict)
	{
		EventDropCheckFinished(verdict);
	}

	/**
	 * Measure again, from the settings as they stand.
	 *
	 * Read here rather than passed in, and read from the store rather than from
	 * the bridge's copy of it: what is worth testing is the port the core is
	 * actually listening on. The group a preference store reads from is mutable
	 * state of the store's own, so it is selected immediately before the read and
	 * nothing relies on one set earlier.
	 *
	 * An unset server is refused before anything is attempted, because that is a
	 * setting somebody has not made yet rather than a network that did not answer.
	 * Asking at all tells that server this client's address and the port it listens
	 * on, so there is deliberately no host to fall back to.
	 *
	 * The run itself goes through the core's own call rather than by touching the
	 * check, which is declared in a header naming Drop's types - a private
	 * dependency of the core precisely so that nothing above it needs them. That
	 * call reads the server and the port again on its own, which is the same pair
	 * read here; they are read here only to refuse the unconfigured case with a
	 * sentence worth showing, since a bare false says nothing about why. A refusal
	 * for any other reason means a run is already going, and that one reports
	 * through this same listener when it finishes, so there is nothing to say and
	 * nothing to correct.
	 */
	void restart(QuickDC::Core* core)
	{
		QuickDC::Preferences* config = core ? core->config : nullptr;

		if (!config)
		{
			fail(QStringLiteral("There is no configuration to read a port from."));
			return;
		}

		config->setGroup("Network");

		const std::string server = config->getString("Reachability server", "");
		const uint16_t port =
			static_cast<uint16_t>(config->getNumber("Port", DEFAULT_PORT));

		if (server.empty())
		{
			fail(QStringLiteral("No reachability server has been configured, so "
			                    "there is nobody to ask."));
			return;
		}

		(void) server;
		(void) port;

		/*
		 * IPv4 only from here, which starts the sequence rather than being all
		 * of it: the core runs the other family once this one has finished, one
		 * check having one session and one armed window. Both verdicts arrive
		 * through this same listener as they are established.
		 */
		if (!QuickDC::startDropCheck(false))
			return;
	}

protected:
	void EventDropCheckFinished(const QuickDC::DropVerdict& verdict) override
	{
		const ReachabilityVerdict copied = convert(verdict);

		report([copied](DropBridge* bridge) {
			(copied.ipv6 ? bridge->m_verdictV6 : bridge->m_verdictV4) = copied;
			emit bridge->verdictChanged(copied);
		});
	}

	void EventDropCheckFailed(const char* reason) override
	{
		fail(reason ? QString::fromUtf8(reason)
		            : QStringLiteral("The reachability check did not complete."));
	}

private:
	/**
	 * Say that nobody measured.
	 *
	 * The verdict in hand is left alone: a check that could not be run says
	 * nothing about the network, so overwriting what an earlier one established
	 * would throw away the only answer there is.
	 */
	void fail(const QString& reason)
	{
		report([reason](DropBridge* bridge) {
			emit bridge->checkFailed(reason);
		});
	}

	/**
	 * Run 'work' on the bridge's thread.
	 *
	 * Addressed to the relay rather than to the bridge, and the relay is held by
	 * the closure: an event may be in flight when the bridge is destroyed, and
	 * there is no way to test a bridge pointer for that from this thread. The
	 * relay is detached instead, which is what turns 'work' into nothing.
	 *
	 * Queued explicitly: a same-thread invokeMethod would run 'work' inline, and
	 * this is only ever called from the core thread, where inline would mean
	 * emitting a signal straight into a view.
	 */
	template<typename Work>
	void report(Work work)
	{
		DropBridge* bridge = m_bridge;
		std::shared_ptr<Relay> relay = m_relay;
		Relay* target = relay.get();

		QMetaObject::invokeMethod(target, [relay = std::move(relay), bridge, work] {
			if (!relay->detached()) work(bridge);
		}, Qt::QueuedConnection);
	}

	DropBridge* m_bridge;
	std::shared_ptr<Relay> m_relay;
};

}

Qdc::DropBridge::DropBridge(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
	, m_watch(std::make_shared<WatchBox>())
	, m_relay(Relay::create())
{
	if (!m_thread) return;

	CoreThread* core = m_thread.data();

	m_thread->post([holder = m_watch, relay = m_relay, core, bridge = this] {
		if (!core->core()) return;

		holder->watcher = new DropWatcher(bridge, relay);

		QuickDC::setDropCheckListener(holder->watcher);

		/* Both families, since either may have been measured before this existed. */
		for (const bool ipv6 : {false, true})
		{
			const QuickDC::DropVerdict known = QuickDC::lastDropVerdict(ipv6);
			if (known.ran) holder->watcher->replay(known);
		}
	});
}

Qdc::DropBridge::~DropBridge()
{
	m_relay->detach();

	if (!m_thread) return;

	m_thread->post([holder = m_watch] {
		QuickDC::setDropCheckListener(nullptr);

		delete holder->watcher;
		holder->watcher = nullptr;
	});
}

Qdc::ReachabilityVerdict Qdc::DropBridge::verdict(bool ipv6) const
{
	return ipv6 ? m_verdictV6 : m_verdictV4;
}

void Qdc::DropBridge::applyForwarding()
{
	if (!m_thread) return;

	CoreThread* thread = m_thread.data();

	m_thread->post([thread] {
		QuickDC::Core* core = thread->core();
		if (!core || !core->portforward || !core->config) return;

		core->config->setGroup("Network");

		const bool wanted = core->config->getBool("UseUPnP", false);
		const bool have = core->portforward->isForwarded() || core->portforward->isBusy();

		if (wanted == have) return;

		if (wanted)
		{
			core->portforward->start(
				static_cast<uint16_t>(core->config->getNumber("Port", DEFAULT_PORT)));
			return;
		}

		core->portforward->stop();
	});
}

void Qdc::DropBridge::recheck()
{
	if (!m_thread)
	{
		emit checkFailed(tr("The QuickDC core is not running."));
		return;
	}

	CoreThread* core = m_thread.data();

	m_thread->post([holder = m_watch, relay = m_relay, core, bridge = this] {
		if (holder->watcher)
		{
			holder->watcher->restart(core->core());
			return;
		}

		Relay* target = relay.get();

		QMetaObject::invokeMethod(target, [relay, bridge] {
			if (relay->detached()) return;

			emit bridge->checkFailed(
				tr("The QuickDC core is not running, so nothing could be "
				   "measured."));
		}, Qt::QueuedConnection);
	});
}

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "dropbridge.h"

#include "corechannel.h"
#include "corethread.h"

#include <api/dropverdict.h>



namespace {

/** What the core assumes when nothing named a port; see [Network] Port. */
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
	copied.addressOnly     = verdict.addressOnly;

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

Qdc::DropBridge::DropBridge(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
{
	if (!m_thread) return;

	connect(m_thread->channel(), &CoreChannel::received, this, &DropBridge::onCoreEvent);

	/* Both families, since either may have been measured before this existed. */
	m_thread->submit(QuickDC::Api::Commands::ReplayReachability{});
}

Qdc::DropBridge::~DropBridge() = default;

Qdc::ReachabilityVerdict Qdc::DropBridge::verdict(bool ipv6) const
{
	return ipv6 ? m_verdictV6 : m_verdictV4;
}

void Qdc::DropBridge::recheck()
{
	if (!m_thread || !m_thread->submit(QuickDC::Api::Commands::MeasureReachability{}))
		emit checkFailed(tr("The QuickDC core is not running, so nothing could be measured."));
}

void Qdc::DropBridge::lookupPublicAddress(bool ipv6)
{
	if (!m_thread || !m_thread->submit(QuickDC::Api::Commands::LookupPublicAddress{ipv6}))
		emit checkFailed(tr("The QuickDC core is not running, so nothing could be measured."));
}

void Qdc::DropBridge::onCoreEvent(const QuickDC::Api::Event& event)
{
	using namespace QuickDC::Api::Events;

	if (const auto* started = event.as<ReachabilityCheckStarted>())
	{
		emit checkStarted(started->ipv6);
	}
	else if (const auto* measured = event.as<ReachabilityMeasured>())
	{
		const ReachabilityVerdict copied = convert(measured->verdict);
		(copied.ipv6 ? m_verdictV6 : m_verdictV4) = copied;
		emit verdictChanged(copied);
	}
	else if (const auto* failed = event.as<ReachabilityCheckFailed>())
	{
		switch (failed->why)
		{
			case ReachabilityCheckFailed::Why::NoConfiguration:
				emit checkFailed(tr("There is no configuration to read a server from."));
				break;

			case ReachabilityCheckFailed::Why::NoServer:
				emit checkFailed(tr("No reachability server has been configured, so "
				                    "there is nobody to ask."));
				break;

			case ReachabilityCheckFailed::Why::CheckFailed:
				emit checkFailed(failed->reason.empty()
					? tr("The reachability check did not complete.")
					: QString::fromStdString(failed->reason));
				break;
		}
	}
}

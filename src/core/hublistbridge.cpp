/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "hublistbridge.h"

#include "corechannel.h"
#include "corethread.h"


#include <QMetaObject>
#include <QUrl>

#include <utility>

namespace {

Qdc::HubListEntry converted(const QuickDC::HubListEntry& hub)
{
	Qdc::HubListEntry entry;
	entry.name = QString::fromStdString(hub.name);
	entry.address = QString::fromStdString(hub.address);
	entry.description = QString::fromStdString(hub.description);
	entry.country = QString::fromStdString(hub.country);
	entry.users = hub.users;
	entry.shared = hub.shared;
	entry.minshare = hub.minshare;
	entry.minslots = hub.minslots;
	entry.maxhubs = hub.maxhubs;
	entry.maxusers = hub.maxusers;
	return entry;
}

}

Qdc::HubListBridge::HubListBridge(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
{
	if (m_thread)
		connect(m_thread->channel(), &CoreChannel::event, this, &HubListBridge::onCoreEvent);
}

Qdc::HubListBridge::~HubListBridge() = default;

void Qdc::HubListBridge::fetch(const QString& url)
{
	const QString wanted = url.trimmed();
	if (wanted.isEmpty()) return;

	if (m_busy.contains(wanted)) return;

	const QUrl parsed(wanted, QUrl::StrictMode);
	const QString scheme = parsed.scheme().toLower();
	if (!parsed.isValid() || parsed.host().isEmpty()
		|| (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")))
	{
		refuse(wanted, tr("'%1' is not an http or https address.").arg(wanted));
		return;
	}

	if (!m_thread || !m_thread->submit(QuickDC::Api::Commands::FetchHubList{wanted.toStdString()}))
	{
		refuse(wanted, tr("The core is not running, so nothing can be fetched."));
		return;
	}

	m_busy.insert(wanted);
}

void Qdc::HubListBridge::onCoreEvent(const QuickDC::Api::Event& event)
{
	if (const auto* done = event.as<QuickDC::Api::Events::HubListFetched>())
	{
		const QString url = QString::fromStdString(done->url);
		if (!m_busy.remove(url)) return;

		QVector<HubListEntry> hubs;
		hubs.reserve(static_cast<qsizetype>(done->hubs.size()));
		for (const QuickDC::HubListEntry& hub : done->hubs) hubs.append(converted(hub));

		emit fetched(url, hubs);
	}
	else if (const auto* failure = event.as<QuickDC::Api::Events::HubListFailed>())
	{
		const QString url = QString::fromStdString(failure->url);
		if (!m_busy.remove(url)) return;

		emit failed(url, tr("%1 could not be fetched: %2")
			.arg(url, QString::fromStdString(failure->reason)));
	}
}

bool Qdc::HubListBridge::isFetching(const QString& url) const
{
	return m_busy.contains(url.trimmed());
}

bool Qdc::HubListBridge::isFetching() const
{
	return !m_busy.isEmpty();
}

void Qdc::HubListBridge::refuse(const QString& url, const QString& reason)
{
	/*
	 * Queued, although nothing was posted anywhere: a caller asking for several
	 * sources in a loop would otherwise be called back part way through its own
	 * loop, and only for the addresses that happen to be unusable. Every answer
	 * arriving the same way is one behaviour to reason about instead of two.
	 */
	QMetaObject::invokeMethod(this, [this, url, reason] {
		emit failed(url, reason);
	}, Qt::QueuedConnection);
}

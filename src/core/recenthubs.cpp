/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "recenthubs.h"

#include "corechannel.h"
#include "corethread.h"
#include "hubbridge.h"

#include <config/userlists.h>

namespace {

namespace Ev = QuickDC::Api::Events;

QList<Qdc::RecentHub> copied(const Ev::RecentHubs& told)
{
	QList<Qdc::RecentHub> entries;
	for (const QuickDC::Api::RecentHub& hub : told.hubs)
	{
		Qdc::RecentHub entry;
		entry.url = QString::fromStdString(hub.url);
		entry.name = QString::fromStdString(hub.name);
		entry.visited = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(hub.visited));
		entries.append(entry);
	}
	return entries;
}

}

Qdc::RecentHubs::RecentHubs(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
{
	if (!m_thread) return;

	if (const Ev::RecentHubs* known = m_thread->channel()->latest<Ev::RecentHubs>())
		m_entries = copied(*known);

	connect(m_thread->channel(), &CoreChannel::received, this, &RecentHubs::take);
}

QList<Qdc::RecentHub> Qdc::RecentHubs::entries() const
{
	return m_entries;
}

int Qdc::RecentHubs::count() const
{
	return static_cast<int>(m_entries.size());
}

int Qdc::RecentHubs::limit()
{
	return QuickDC::RecentHubs::LIMIT;
}

bool Qdc::RecentHubs::contains(const QString& url) const
{
	const QString wanted = HubBridge::normalisedUrl(url);

	for (const RecentHub& entry : m_entries)
		if (entry.url == wanted) return true;

	return false;
}

void Qdc::RecentHubs::remove(const QString& url)
{
	if (m_thread) m_thread->submit(QuickDC::Api::Commands::ForgetRecentHub{url.toStdString()});
}

void Qdc::RecentHubs::clear()
{
	if (m_thread) m_thread->submit(QuickDC::Api::Commands::ClearRecentHubs{});
}

void Qdc::RecentHubs::take(const QuickDC::Api::Event& event)
{
	const auto* told = event.as<Ev::RecentHubs>();
	if (!told) return;

	m_entries = copied(*told);
	emit changed();
}

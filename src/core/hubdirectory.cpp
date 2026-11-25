/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "hubdirectory.h"

#include "corechannel.h"
#include "corethread.h"

namespace {

namespace Ev = QuickDC::Api::Events;

QVector<Qdc::HubListEntry> copied(const Ev::HubDirectory& told)
{
	QVector<Qdc::HubListEntry> hubs;
	hubs.reserve(static_cast<qsizetype>(told.hubs.size()));

	for (const QuickDC::Api::DirectoryHub& one : told.hubs)
	{
		Qdc::HubListEntry entry;
		entry.name = QString::fromStdString(one.hub.name);
		entry.address = QString::fromStdString(one.hub.address);
		entry.description = QString::fromStdString(one.hub.description);
		entry.country = QString::fromStdString(one.hub.country);
		entry.users = one.hub.users;
		entry.shared = one.hub.shared;
		entry.minshare = one.hub.minshare;
		entry.minslots = one.hub.minslots;
		entry.maxhubs = one.hub.maxhubs;
		entry.maxusers = one.hub.maxusers;
		hubs.append(entry);
	}

	return hubs;
}

QList<Qdc::HubListSource> copied(const Ev::HubListSources& told)
{
	QList<Qdc::HubListSource> sources;

	for (const QuickDC::Api::HubListSource& one : told.sources)
	{
		Qdc::HubListSource source;
		source.url = QString::fromStdString(one.url);
		if (one.fetched) source.fetched = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(one.fetched));
		source.hubs = static_cast<int>(one.hubs);
		source.cached = one.cached;
		source.error = QString::fromStdString(one.error);
		source.fetching = one.fetching;
		sources.append(source);
	}

	return sources;
}

}

Qdc::HubDirectory::HubDirectory(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
{
	if (!m_thread) return;

	if (const auto* known = m_thread->channel()->latest<Ev::HubDirectory>()) m_hubs = copied(*known);
	if (const auto* known = m_thread->channel()->latest<Ev::HubListSources>()) m_sources = copied(*known);
	m_fetching = isFetching();

	connect(m_thread->channel(), &CoreChannel::received, this, &HubDirectory::take);
}

QVector<Qdc::HubListEntry> Qdc::HubDirectory::hubs() const
{
	return m_hubs;
}

int Qdc::HubDirectory::hubCount() const
{
	return static_cast<int>(m_hubs.size());
}

QList<Qdc::HubListSource> Qdc::HubDirectory::sources() const
{
	return m_sources;
}

QDateTime Qdc::HubDirectory::lastUpdated() const
{
	QDateTime newest;

	for (const HubListSource& source : m_sources)
	{
		if (!source.fetched.isValid()) continue;
		if (!newest.isValid() || source.fetched > newest) newest = source.fetched;
	}

	return newest;
}

bool Qdc::HubDirectory::isFetching() const
{
	for (const HubListSource& source : m_sources)
	{
		if (source.fetching) return true;
	}

	return false;
}

void Qdc::HubDirectory::refresh(bool force)
{
	if (m_thread) m_thread->submit(QuickDC::Api::Commands::RefreshHubDirectory{force});
}

void Qdc::HubDirectory::addSource(const QString& url)
{
	if (m_thread) m_thread->submit(QuickDC::Api::Commands::AddHubListSource{url.toStdString()});
}

void Qdc::HubDirectory::removeSource(const QString& url)
{
	if (m_thread) m_thread->submit(QuickDC::Api::Commands::RemoveHubListSource{url.toStdString()});
}

void Qdc::HubDirectory::resetSources()
{
	if (m_thread) m_thread->submit(QuickDC::Api::Commands::ResetHubListSources{});
}

void Qdc::HubDirectory::take(const QuickDC::Api::Event& event)
{
	if (const auto* told = event.as<Ev::HubDirectory>())
	{
		m_hubs = copied(*told);
		emit changed();
		return;
	}

	const auto* told = event.as<Ev::HubListSources>();
	if (!told) return;

	m_sources = copied(*told);
	emit sourcesChanged();

	const bool fetching = isFetching();
	if (fetching == m_fetching) return;

	m_fetching = fetching;
	emit fetchingChanged(fetching);
}

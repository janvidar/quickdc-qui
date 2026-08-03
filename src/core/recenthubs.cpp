/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "recenthubs.h"

#include "configbridge.h"
#include "hubbridge.h"

#include <algorithm>

namespace {

const QString GROUP = QStringLiteral("Recent hubs");
const QString TOTAL = QStringLiteral("Total");

constexpr QChar SEPARATOR = u'|';

/** How many hubs are remembered. A shortcut back, not a history. */
constexpr int LIMIT = 32;

QString hub_key(int index)
{
	return QStringLiteral("Hub %1").arg(index);
}

QString encode(const Qdc::RecentHub& entry)
{
	/* The name last, so it may contain the separator and nothing else need. */
	return QStringLiteral("%1|%2|%3")
		.arg(entry.visited.toSecsSinceEpoch())
		.arg(entry.url, entry.name);
}

bool decode(const QString& line, Qdc::RecentHub& out)
{
	const QStringList parts = line.split(SEPARATOR);
	if (parts.size() < 3) return false;

	bool ok = false;

	const qint64 seconds = parts.at(0).toLongLong(&ok);
	if (!ok) return false;

	out.visited = QDateTime::fromSecsSinceEpoch(seconds);
	out.url = parts.at(1);
	out.name = parts.mid(2).join(SEPARATOR);

	return !out.url.isEmpty();
}

}

Qdc::RecentHubs::RecentHubs(ConfigBridge* config, QObject* parent)
	: QObject(parent)
	, m_config(config)
{
	if (!m_config) return;

	const int total = m_config->getNumber(ConfigStore::Application, GROUP, TOTAL, 0);

	for (int n = 0; n < qMin(total, LIMIT); ++n)
	{
		const QString line = m_config->getString(ConfigStore::Application, GROUP,
		                                         hub_key(n));
		if (line.isEmpty()) continue;

		RecentHub entry;
		if (!decode(line, entry)) continue;

		/* A hand-edited file may name the same hub twice, in which case the newer
		   line is the one worth keeping. */
		if (contains(entry.url)) continue;

		m_entries.append(entry);
	}

	/* Sorted rather than trusted: the order is what the list is shown in, and a
	   hand-edited file or a clock that moved would otherwise show rows out of the
	   order their own dates imply. */
	std::stable_sort(m_entries.begin(), m_entries.end(),
		[](const RecentHub& left, const RecentHub& right) {
			return left.visited > right.visited;
		});

	m_storedCount = qMin(total, LIMIT);
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
	return LIMIT;
}

bool Qdc::RecentHubs::contains(const QString& url) const
{
	const QString wanted = HubBridge::normalisedUrl(url);

	for (const RecentHub& entry : m_entries)
	{
		if (entry.url == wanted) return true;
	}

	return false;
}

void Qdc::RecentHubs::note(const QString& url, const QString& name)
{
	/*
	 * Normalised here rather than trusted from the caller, so that a hub opened by
	 * address and the same hub opened from this list are one entry. It is what the
	 * hub connection itself is keyed on.
	 */
	const QString wanted = HubBridge::normalisedUrl(url);
	if (wanted.isEmpty()) return;

	RecentHub entry;
	entry.url = wanted;
	entry.name = name;
	entry.visited = QDateTime::currentDateTime();

	for (int n = 0; n < m_entries.size(); ++n)
	{
		if (m_entries.at(n).url != wanted) continue;

		/* What the hub called itself last time is better than nothing, which is
		   what is known about it at the moment a connection is opened. */
		if (entry.name.isEmpty()) entry.name = m_entries.at(n).name;

		m_entries.removeAt(n);
		break;
	}

	m_entries.prepend(entry);

	while (m_entries.size() > LIMIT) m_entries.removeLast();

	store();
	emit changed();
}

bool Qdc::RecentHubs::setName(const QString& url, const QString& name)
{
	const QString wanted = HubBridge::normalisedUrl(url);

	for (RecentHub& entry : m_entries)
	{
		if (entry.url != wanted) continue;
		if (entry.name == name) return true;

		entry.name = name;
		store();
		emit changed();
		return true;
	}

	return false;
}

bool Qdc::RecentHubs::remove(const QString& url)
{
	const QString wanted = HubBridge::normalisedUrl(url);

	for (int n = 0; n < m_entries.size(); ++n)
	{
		if (m_entries.at(n).url != wanted) continue;

		m_entries.removeAt(n);
		store();
		emit changed();
		return true;
	}

	return false;
}

void Qdc::RecentHubs::clear()
{
	if (m_entries.isEmpty()) return;

	m_entries.clear();
	store();
	emit changed();
}

void Qdc::RecentHubs::store()
{
	if (!m_config) return;

	/*
	 * Cleared against the count that was there, not the one being written: the keys
	 * are numbered, so a shorter list would otherwise leave its old tail behind and
	 * Total would disagree with what is in the file.
	 */
	QStringList stale;
	for (int n = 0; n < m_storedCount; ++n) stale.append(hub_key(n));

	QList<QPair<QString, QString>> values;
	values.reserve(m_entries.size() + 1);

	for (int n = 0; n < m_entries.size(); ++n)
		values.append({hub_key(n), encode(m_entries.at(n))});

	values.append({TOTAL, QString::number(m_entries.size())});

	m_config->replaceGroup(ConfigStore::Application, GROUP, stale, values);
	m_config->save(ConfigStore::Application);

	m_storedCount = static_cast<int>(m_entries.size());
}

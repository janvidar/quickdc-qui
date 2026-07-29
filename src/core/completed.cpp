/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "completed.h"

#include "configbridge.h"

#include <algorithm>

namespace {

const QString GROUP = QStringLiteral("Completed");
const QString TOTAL = QStringLiteral("Total");

constexpr QChar SEPARATOR = u'|';

/** How many finished downloads are remembered. */
constexpr int LIMIT = 500;

/**
 * How close two completions of the same file have to be to count as one report
 * rather than as the file having genuinely been downloaded twice.
 */
constexpr qint64 REPEAT_WINDOW_SECONDS = 5;

QString entry_key(int index)
{
	return QStringLiteral("Entry %1").arg(index);
}

QString encode(const Qdc::CompletedDownload& entry)
{
	/* The path last, so it may contain the separator and nothing else need. */
	return QStringLiteral("%1|%2|%3|%4")
		.arg(entry.finished.toSecsSinceEpoch())
		.arg(entry.size)
		.arg(entry.tth, entry.filename);
}

bool decode(const QString& line, Qdc::CompletedDownload& out)
{
	/*
	 * At most four parts: everything after the third separator is the path, which
	 * is why splitting is bounded rather than greedy.
	 */
	const QStringList parts = line.split(SEPARATOR);
	if (parts.size() < 4) return false;

	bool ok = false;

	const qint64 seconds = parts.at(0).toLongLong(&ok);
	if (!ok) return false;

	const quint64 size = parts.at(1).toULongLong(&ok);
	if (!ok) return false;

	out.finished = QDateTime::fromSecsSinceEpoch(seconds);
	out.size = size;
	out.tth = parts.at(2);
	out.filename = parts.mid(3).join(SEPARATOR);

	return !out.filename.isEmpty();
}

}

Qdc::Completed::Completed(ConfigBridge* config, QObject* parent)
	: QObject(parent)
	, m_config(config)
{
	if (!m_config) return;

	const int total = m_config->getNumber(ConfigStore::Application, GROUP, TOTAL, 0);

	for (int n = 0; n < qMin(total, LIMIT); ++n)
	{
		const QString line = m_config->getString(ConfigStore::Application, GROUP,
		                                         entry_key(n));
		if (line.isEmpty()) continue;

		CompletedDownload entry;
		if (decode(line, entry)) m_entries.append(entry);
	}

	/*
	 * Sorted rather than trusted, because the timestamp is what the list is
	 * displayed in order of - a hand-edited file, or a clock that moved, would
	 * otherwise show rows out of the order their own dates imply.
	 */
	std::stable_sort(m_entries.begin(), m_entries.end(),
		[](const CompletedDownload& left, const CompletedDownload& right) {
			return left.finished > right.finished;
		});

	m_storedCount = qMin(total, LIMIT);
}

QList<Qdc::CompletedDownload> Qdc::Completed::entries() const
{
	return m_entries;
}

int Qdc::Completed::count() const
{
	return static_cast<int>(m_entries.size());
}

int Qdc::Completed::limit()
{
	return LIMIT;
}

void Qdc::Completed::add(const CompletedDownload& entry)
{
	if (entry.filename.isEmpty()) return;

	CompletedDownload stored = entry;
	if (!stored.finished.isValid()) stored.finished = QDateTime::currentDateTime();

	/*
	 * Downloading the same file twice is legitimate and must both be recorded, so
	 * what is suppressed is the same completion reported twice - which is to say
	 * the same name at very nearly the same moment.
	 */
	for (const CompletedDownload& existing : m_entries)
	{
		if (existing.filename != stored.filename) continue;

		if (qAbs(existing.finished.secsTo(stored.finished)) <= REPEAT_WINDOW_SECONDS)
			return;
	}

	m_entries.append(stored);

	std::stable_sort(m_entries.begin(), m_entries.end(),
		[](const CompletedDownload& left, const CompletedDownload& right) {
			return left.finished > right.finished;
		});

	while (m_entries.size() > LIMIT) m_entries.removeLast();

	store();
	emit changed();
}

bool Qdc::Completed::remove(const QString& filename)
{
	for (int n = 0; n < m_entries.size(); ++n)
	{
		if (m_entries.at(n).filename != filename) continue;

		m_entries.removeAt(n);
		store();
		emit changed();
		return true;
	}

	return false;
}

void Qdc::Completed::clear()
{
	if (m_entries.isEmpty()) return;

	m_entries.clear();
	store();
	emit changed();
}

void Qdc::Completed::store()
{
	if (!m_config) return;

	/*
	 * Cleared against the count that was there, not the one being written: the
	 * keys are numbered, so a shorter list would otherwise leave its old tail
	 * behind and Total would disagree with what is in the file.
	 */
	QStringList stale;
	for (int n = 0; n < m_storedCount; ++n) stale.append(entry_key(n));

	QList<QPair<QString, QString>> values;
	values.reserve(m_entries.size() + 1);

	for (int n = 0; n < m_entries.size(); ++n)
		values.append({entry_key(n), encode(m_entries.at(n))});

	values.append({TOTAL, QString::number(m_entries.size())});

	m_config->replaceGroup(ConfigStore::Application, GROUP, stale, values);
	m_config->save(ConfigStore::Application);

	m_storedCount = static_cast<int>(m_entries.size());
}

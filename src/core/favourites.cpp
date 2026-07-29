/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "favourites.h"

#include "configbridge.h"

namespace {

const QString GROUP = QStringLiteral("Favourites");
const QString TOTAL = QStringLiteral("Total");

QString hub_key(int index)
{
	return QStringLiteral("Hub %1").arg(index);
}

/** More than anyone has, and a bound on a corrupt Total. */
constexpr int LIMIT = 4096;

}

Qdc::Favourites::Favourites(ConfigBridge* config, QObject* parent)
	: QObject(parent)
	, m_config(config)
{
	if (!m_config) return;

	const int total = m_config->getNumber(ConfigStore::Application, GROUP, TOTAL, 0);

	for (int n = 0; n < qMin(total, LIMIT); ++n)
	{
		const QString url = m_config->getString(ConfigStore::Application, GROUP,
		                                        hub_key(n));

		/*
		 * Blanks and repeats are dropped rather than kept: a hand-edited file
		 * with a gap in the numbering should not produce empty rows in the
		 * favourites list.
		 */
		if (url.isEmpty() || m_urls.contains(url)) continue;

		m_urls.append(url);
	}

	m_storedCount = qMin(total, LIMIT);
}

QStringList Qdc::Favourites::urls() const
{
	return m_urls;
}

bool Qdc::Favourites::contains(const QString& url) const
{
	return m_urls.contains(url);
}

bool Qdc::Favourites::add(const QString& url)
{
	if (url.isEmpty() || m_urls.contains(url)) return false;

	m_urls.append(url);
	store();
	emit changed();
	return true;
}

bool Qdc::Favourites::remove(const QString& url)
{
	if (!m_urls.removeOne(url)) return false;

	store();
	emit changed();
	return true;
}

bool Qdc::Favourites::toggle(const QString& url)
{
	if (contains(url))
	{
		remove(url);
		return false;
	}

	add(url);
	return contains(url);
}

void Qdc::Favourites::store()
{
	if (!m_config) return;

	/*
	 * Cleared against the count that was there, not the one being written, so
	 * that a list which has shrunk does not leave its old tail in the file.
	 */
	QStringList stale;
	for (int n = 0; n < m_storedCount; ++n) stale.append(hub_key(n));

	QList<QPair<QString, QString>> values;
	values.reserve(m_urls.size() + 1);

	for (int n = 0; n < m_urls.size(); ++n)
		values.append({hub_key(n), m_urls.at(n)});

	values.append({TOTAL, QString::number(m_urls.size())});

	m_config->replaceGroup(ConfigStore::Application, GROUP, stale, values);
	m_config->save(ConfigStore::Application);

	m_storedCount = static_cast<int>(m_urls.size());
}

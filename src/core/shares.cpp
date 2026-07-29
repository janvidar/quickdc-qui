/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "shares.h"

#include "configbridge.h"

#include <QDir>
#include <QFileInfo>

namespace {

const QString GROUP = QStringLiteral("Shares");

/** The core spells it out in full; not "Total". */
const QString TOTAL = QStringLiteral("Total shares");

constexpr QChar SEPARATOR = u':';

/** More than anyone shares, and a bound on a corrupt total. */
constexpr int LIMIT = 1024;

QString share_key(int index)
{
	return QStringLiteral("Share %1").arg(index);
}

/** Trailing separators would make the stored pair ambiguous on reload. */
QString clean_name(const QString& name)
{
	QString cleaned = name;
	cleaned.remove(SEPARATOR);
	return cleaned.trimmed();
}

}

Qdc::Shares::Shares(ConfigBridge* config, QObject* parent)
	: QObject(parent)
	, m_config(config)
{
	if (!m_config) return;

	const int total = m_config->getNumber(ConfigStore::Application, GROUP, TOTAL, 0);

	for (int n = 0; n < qMin(total, LIMIT); ++n)
	{
		const QString line = m_config->getString(ConfigStore::Application, GROUP,
		                                         share_key(n));
		if (line.isEmpty()) continue;

		/*
		 * The first colon splits the pair, because the path may contain more of
		 * them and the name may not.
		 */
		const qsizetype split = line.indexOf(SEPARATOR);
		if (split <= 0) continue;

		SharedFolder folder;
		folder.name = line.left(split);
		folder.path = line.mid(split + 1);

		if (folder.name.isEmpty() || folder.path.isEmpty()) continue;

		m_folders.append(folder);
	}

	m_storedCount = qMin(total, LIMIT);
}

QList<Qdc::SharedFolder> Qdc::Shares::folders() const
{
	return m_folders;
}

int Qdc::Shares::count() const
{
	return static_cast<int>(m_folders.size());
}

bool Qdc::Shares::contains(const QString& path) const
{
	const QString wanted = QDir::cleanPath(path);

	for (const SharedFolder& folder : m_folders)
	{
		const QString have = QDir::cleanPath(folder.path);

		if (have == wanted) return true;

		/*
		 * A folder inside one already offered is already offered, and adding it
		 * again would put every file in it into the list twice.
		 */
		if (wanted.startsWith(have + QLatin1Char('/'))) return true;
	}

	return false;
}

bool Qdc::Shares::add(const QString& path, const QString& name)
{
	const QString cleaned = QDir::cleanPath(path.trimmed());
	if (cleaned.isEmpty() || contains(cleaned)) return false;

	SharedFolder folder;
	folder.path = cleaned;
	folder.name = clean_name(name);

	if (folder.name.isEmpty())
	{
		folder.name = QFileInfo(cleaned).fileName();

		/* A root directory has no base name; fall back to something usable. */
		if (folder.name.isEmpty()) folder.name = QStringLiteral("share");
	}

	m_folders.append(folder);
	store();
	emit changed();

	return true;
}

bool Qdc::Shares::removeAt(int index)
{
	if (index < 0 || index >= m_folders.size()) return false;

	m_folders.removeAt(index);
	store();
	emit changed();

	return true;
}

bool Qdc::Shares::renameAt(int index, const QString& name)
{
	if (index < 0 || index >= m_folders.size()) return false;

	const QString cleaned = clean_name(name);
	if (cleaned.isEmpty() || cleaned == m_folders.at(index).name) return false;

	m_folders[index].name = cleaned;
	store();
	emit changed();

	return true;
}

void Qdc::Shares::setFolders(const QList<SharedFolder>& folders)
{
	m_folders.clear();

	for (const SharedFolder& folder : folders)
	{
		if (folder.path.isEmpty()) continue;

		SharedFolder cleaned;
		cleaned.path = QDir::cleanPath(folder.path);
		cleaned.name = clean_name(folder.name);

		if (cleaned.name.isEmpty())
			cleaned.name = QFileInfo(cleaned.path).fileName();

		if (cleaned.name.isEmpty()) continue;

		m_folders.append(cleaned);
	}

	store();
	emit changed();
}

void Qdc::Shares::store()
{
	if (!m_config) return;

	QStringList stale;
	for (int n = 0; n < m_storedCount; ++n) stale.append(share_key(n));

	QList<QPair<QString, QString>> values;
	values.reserve(m_folders.size() + 1);

	for (int n = 0; n < m_folders.size(); ++n)
	{
		values.append({share_key(n),
		               m_folders.at(n).name + SEPARATOR + m_folders.at(n).path});
	}

	values.append({TOTAL, QString::number(m_folders.size())});

	m_config->replaceGroup(ConfigStore::Application, GROUP, stale, values);
	m_config->save(ConfigStore::Application);

	m_storedCount = static_cast<int>(m_folders.size());
}

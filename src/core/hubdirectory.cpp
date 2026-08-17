/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "hubdirectory.h"

#include "configbridge.h"
#include "hubbridge.h"
#include "hublistbridge.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

const QString GROUP = QStringLiteral("Hublists");
const QString TOTAL = QStringLiteral("Total");

QString list_key(int index)
{
	return QStringLiteral("List %1").arg(index);
}

/** More sources than anyone has, and a bound on a corrupt Total. */
constexpr int SOURCE_LIMIT = 32;

/** How old a cached list may be before it is fetched again. */
constexpr int MAX_AGE_HOURS = 12;

/**
 * How long a source that has just been asked for is left alone.
 *
 * The list is refreshed whenever it is looked at, which is what keeps it current
 * without a button having to be pressed - but a source that is simply down would
 * then be asked again every time somebody switched tab. This is what turns that
 * into one attempt.
 */
constexpr int RETRY_MINUTES = 15;

/**
 * How many hubs one source contributes, and how many the merged list holds.
 *
 * The core already refuses a list naming more than 65536 hubs. These are the
 * second half of that: what is written to the cache, and what is held in memory
 * after several lists have been folded together. The public lists carry a couple
 * of thousand hubs each, so neither is within sight of a real one.
 */
constexpr int PER_SOURCE_LIMIT = 20000;
constexpr int MERGED_LIMIT = 65536;

/**
 * What decides that two entries are the same hub.
 *
 * The normalised address, so that "example.org" from one list and
 * "dchub://example.org:411" from another are folded together rather than shown as
 * two hubs. An address the normaliser cannot make sense of keys on itself: it is
 * still a row somebody may want to try, and dropping it would hide a hub because
 * of how its list spelled it.
 */
QString merge_key(const QString& address)
{
	const QString normalised = Qdc::HubBridge::normalisedUrl(address);
	return normalised.isEmpty() ? address.trimmed().toLower() : normalised;
}

QJsonObject encode(const Qdc::HubListEntry& hub)
{
	QJsonObject object;

	/* Only what the list actually said. A cache of a few thousand hubs is written
	   and read on every start, and most of these fields are empty in most of them. */
	object.insert(QStringLiteral("address"), hub.address);

	if (!hub.name.isEmpty()) object.insert(QStringLiteral("name"), hub.name);
	if (!hub.description.isEmpty())
		object.insert(QStringLiteral("description"), hub.description);
	if (!hub.country.isEmpty()) object.insert(QStringLiteral("country"), hub.country);

	if (hub.users) object.insert(QStringLiteral("users"),
	                             static_cast<qint64>(hub.users));
	if (hub.shared) object.insert(QStringLiteral("shared"),
	                              static_cast<qint64>(hub.shared));
	if (hub.minshare) object.insert(QStringLiteral("minshare"),
	                                static_cast<qint64>(hub.minshare));
	if (hub.minslots) object.insert(QStringLiteral("minslots"),
	                                static_cast<qint64>(hub.minslots));
	if (hub.maxhubs) object.insert(QStringLiteral("maxhubs"),
	                               static_cast<qint64>(hub.maxhubs));
	if (hub.maxusers) object.insert(QStringLiteral("maxusers"),
	                                static_cast<qint64>(hub.maxusers));

	return object;
}

quint64 number(const QJsonObject& object, const QString& key)
{
	/* toInteger rather than toDouble: a share total is a byte count, and the
	   integer accessor is exact where the double one stops being. */
	const qint64 value = object.value(key).toInteger(0);
	return value > 0 ? static_cast<quint64>(value) : 0;
}

/** @return false for an entry naming no address, which names nowhere to connect. */
bool decode(const QJsonObject& object, Qdc::HubListEntry& hub)
{
	hub.address = object.value(QStringLiteral("address")).toString();
	if (hub.address.isEmpty()) return false;

	hub.name = object.value(QStringLiteral("name")).toString();
	hub.description = object.value(QStringLiteral("description")).toString();
	hub.country = object.value(QStringLiteral("country")).toString();

	hub.users = number(object, QStringLiteral("users"));
	hub.shared = number(object, QStringLiteral("shared"));
	hub.minshare = number(object, QStringLiteral("minshare"));
	hub.minslots = number(object, QStringLiteral("minslots"));
	hub.maxhubs = number(object, QStringLiteral("maxhubs"));
	hub.maxusers = number(object, QStringLiteral("maxusers"));

	return true;
}

}

Qdc::HubDirectory::HubDirectory(CoreThread* thread, ConfigBridge* config,
                                const QString& cacheDir, QObject* parent)
	: QObject(parent)
	, m_config(config)
	, m_cacheDir(cacheDir.isEmpty() ? defaultCacheDirectory() : cacheDir)
{
	loadSources();
	loadCaches();

	if (!thread) return;

	m_fetch = new HubListBridge(thread, this);

	connect(m_fetch, &HubListBridge::fetched, this, &HubDirectory::onFetched);
	connect(m_fetch, &HubListBridge::failed, this, &HubDirectory::onFailed);
}

Qdc::HubDirectory::~HubDirectory()
{
	/*
	 * Before this object goes, because the bridge's destructor blocks on the core
	 * thread and must run while the core is still up. It is a child, so ~QObject
	 * would do it anyway - only later than the owner of this can guarantee.
	 */
	delete m_fetch;
	m_fetch = nullptr;
}

int Qdc::HubDirectory::maxAgeHours()
{
	return MAX_AGE_HOURS;
}

QStringList Qdc::HubDirectory::defaultSources()
{
	/*
	 * The lists that have been up for years and are what other clients ship. https
	 * where the server offers it, since a hublist arrives over the internet from a
	 * host nothing else authenticates.
	 *
	 * Order is precedence, and this order is by how well described the lists are
	 * rather than by size: where two of them name the same hub, the first one's
	 * name and description are what is shown.
	 */
	return {
		QStringLiteral("https://dchublist.org/hublist.xml.bz2"),
		QStringLiteral("https://te-home.net/?do=hublist&get=hublist.xml.bz2")
	};
}

QStringList Qdc::HubDirectory::retiredSources()
{
	/*
	 * dchublist.biz answers 403 to everything, its own front page included, and
	 * has done for long enough to stop shipping. The address redirects to a query
	 * form of itself which is what refuses, so the failure arrives one hop in and
	 * reads as a client fault rather than a server that has gone.
	 */
	return { QStringLiteral("https://dchublist.biz/hublist.xml.bz2") };
}

QString Qdc::HubDirectory::defaultCacheDirectory()
{
	return QDir::homePath() + QStringLiteral("/.quickdc/hublists");
}

void Qdc::HubDirectory::loadSources()
{
	/*
	 * Asked before anything is read, because a read of an absent key writes the
	 * default back into the store - so reading Total first would record a nought
	 * and leave a later start unable to tell "no sources configured" from "sources
	 * deliberately removed".
	 */
	const bool configured = m_config
		&& m_config->contains(ConfigStore::Application, GROUP, TOTAL);

	QStringList urls;
	bool rewritten = false;
	int stored = 0;

	if (configured)
	{
		const int total = m_config->getNumber(ConfigStore::Application, GROUP, TOTAL, 0);
		const QStringList retired = retiredSources();

		stored = qMin(total, SOURCE_LIMIT);

		for (int n = 0; n < qMin(total, SOURCE_LIMIT); ++n)
		{
			const QString url = m_config->getString(ConfigStore::Application, GROUP,
			                                        list_key(n)).trimmed();

			/* A gap in the numbering of a hand-edited file, or the same list twice. */
			if (url.isEmpty() || urls.contains(url)) continue;

			/* One this program wrote and has since withdrawn; see retiredSources(). */
			if (retired.contains(url))
			{
				rewritten = true;
				continue;
			}

			urls.append(url);
		}
	}
	else
	{
		urls = defaultSources();
		rewritten = true;
	}

	/*
	 * Written out so that they can be found and edited, and written again when a
	 * withdrawn one was dropped above - otherwise it is read, skipped and left in
	 * the file to be skipped again on every start, and a user reading that file
	 * sees a list this program has quietly stopped using.
	 *
	 * Somebody who then removes them all gets a Total of nought, which is
	 * respected: 'configured' was asked before anything was read.
	 */
	if (rewritten && m_config)
	{
		/*
		 * Cleared against the count that was there rather than the one being
		 * written, so that a list which has shrunk does not leave its old tail
		 * behind - a stale 'List 2' past a Total of two is read by nobody and
		 * puzzles anyone who opens the file.
		 */
		QStringList stale;
		for (int n = 0; n < stored; ++n) stale.append(list_key(n));

		QList<QPair<QString, QString>> values;
		values.reserve(urls.size() + 1);

		for (int n = 0; n < urls.size(); ++n) values.append({list_key(n), urls.at(n)});
		values.append({TOTAL, QString::number(urls.size())});

		m_config->replaceGroup(ConfigStore::Application, GROUP, stale, values);
		m_config->save(ConfigStore::Application);
	}

	for (const QString& url : urls)
	{
		HubListSource source;
		source.url = url;
		m_sources.append(source);
	}
}

void Qdc::HubDirectory::loadCaches()
{
	for (HubListSource& source : m_sources)
	{
		QVector<HubListEntry> hubs;
		if (!readCache(source, hubs)) continue;

		source.hubs = static_cast<int>(hubs.size());
		m_perSource.insert(source.url, hubs);
	}

	merge();
}

QString Qdc::HubDirectory::cacheFileFor(const QString& url) const
{
	/*
	 * Named by a digest of the address rather than by the address itself, which
	 * carries slashes and query strings and is not a file name. The URL is written
	 * inside the file as well, so a collision or a stale file is recognised on the
	 * way in rather than read as another source's list.
	 */
	const QByteArray digest = QCryptographicHash::hash(url.toUtf8(),
	                                                   QCryptographicHash::Sha1);

	return m_cacheDir + QStringLiteral("/") + QString::fromLatin1(digest.toHex())
		+ QStringLiteral(".json");
}

bool Qdc::HubDirectory::readCache(HubListSource& source,
                                  QVector<HubListEntry>& hubs) const
{
	QFile file(cacheFileFor(source.url));
	if (!file.open(QIODevice::ReadOnly)) return false;

	const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject()) return false;

	const QJsonObject root = document.object();

	/* Whose list this is. A file that says it is another source's is not this
	   one's cache, whatever it is doing under this name. */
	if (root.value(QStringLiteral("url")).toString() != source.url) return false;

	const qint64 seconds = root.value(QStringLiteral("fetched")).toInteger(0);
	if (seconds <= 0) return false;

	const QJsonArray listed = root.value(QStringLiteral("hubs")).toArray();

	hubs.clear();
	hubs.reserve(listed.size());

	for (const QJsonValue& value : listed)
	{
		if (hubs.size() >= PER_SOURCE_LIMIT) break;

		HubListEntry hub;
		if (decode(value.toObject(), hub)) hubs.append(hub);
	}

	source.fetched = QDateTime::fromSecsSinceEpoch(seconds);
	return true;
}

void Qdc::HubDirectory::writeCache(const HubListSource& source,
                                   const QVector<HubListEntry>& hubs) const
{
	QDir().mkpath(m_cacheDir);

	QJsonArray listed;
	for (const HubListEntry& hub : hubs) listed.append(encode(hub));

	QJsonObject root;
	root.insert(QStringLiteral("url"), source.url);
	root.insert(QStringLiteral("fetched"),
	            source.fetched.isValid() ? source.fetched.toSecsSinceEpoch() : 0);
	root.insert(QStringLiteral("hubs"), listed);

	/*
	 * A save file rather than a plain one: the cache is read at startup, and a
	 * write interrupted half way through would leave a document that parses as far
	 * as it goes and then stops - which is a list quietly missing its tail.
	 */
	QSaveFile file(cacheFileFor(source.url));
	if (!file.open(QIODevice::WriteOnly)) return;

	file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
	file.commit();
}

void Qdc::HubDirectory::merge()
{
	QVector<HubListEntry> merged;
	QHash<QString, qsizetype> seen;

	for (const HubListSource& source : m_sources)
	{
		const auto listed = m_perSource.constFind(source.url);
		if (listed == m_perSource.constEnd()) continue;

		for (const HubListEntry& hub : *listed)
		{
			const QString key = merge_key(hub.address);
			const auto known = seen.constFind(key);

			if (known == seen.constEnd())
			{
				if (merged.size() >= MERGED_LIMIT) break;

				seen.insert(key, merged.size());
				merged.append(hub);
				continue;
			}

			/*
			 * A hub several lists name keeps the description of the first that
			 * named it, and gains from the others only what the first left blank -
			 * one list may name a hub that another describes.
			 *
			 * The user count is the exception: it is the one field a stale list is
			 * simply wrong about rather than terse, and where two lists disagree
			 * the larger figure is the one that cannot have been made up.
			 */
			HubListEntry& kept = merged[*known];

			if (kept.name.isEmpty()) kept.name = hub.name;
			if (kept.description.isEmpty()) kept.description = hub.description;
			if (kept.country.isEmpty()) kept.country = hub.country;

			kept.users = qMax(kept.users, hub.users);
		}
	}

	m_hubs = merged;
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
	if (!m_fetch) return;

	const QDateTime now = QDateTime::currentDateTime();

	for (HubListSource& source : m_sources)
	{
		if (source.fetching) continue;

		if (!force)
		{
			/* Asked for a moment ago, whatever came of it; see RETRY_MINUTES. */
			if (source.attempted.isValid()
				&& source.attempted.secsTo(now) < RETRY_MINUTES * 60) continue;

			/* Cached, recently, and not empty. An empty answer is worth asking
			   again about, since it is what a server in the middle of an update
			   returns. */
			if (source.fetched.isValid() && source.hubs > 0
				&& source.fetched.secsTo(now) < MAX_AGE_HOURS * 3600) continue;
		}

		source.fetching = true;
		source.attempted = now;

		m_fetch->fetch(source.url);
	}

	reportFetching();
}

Qdc::HubListSource* Qdc::HubDirectory::sourceFor(const QString& url)
{
	for (HubListSource& source : m_sources)
	{
		if (source.url == url) return &source;
	}

	return nullptr;
}

void Qdc::HubDirectory::onFetched(const QString& url,
                                  const QVector<HubListEntry>& hubs)
{
	HubListSource* source = sourceFor(url);

	/* A list that arrived after the sources were changed under it. */
	if (!source) return;

	QVector<HubListEntry> kept = hubs;

	/* The core bounds one list at rather more than this. Cut here so that the
	   cache and the merged list are bounded by something a machine can hold. */
	if (kept.size() > PER_SOURCE_LIMIT) kept.resize(PER_SOURCE_LIMIT);

	source->fetching = false;
	source->fetched = QDateTime::currentDateTime();
	source->hubs = static_cast<int>(kept.size());
	source->error.clear();

	m_perSource.insert(url, kept);

	writeCache(*source, kept);
	merge();

	emit changed();
	reportFetching();
}

void Qdc::HubDirectory::onFailed(const QString& url, const QString& reason)
{
	HubListSource* source = sourceFor(url);
	if (!source) return;

	source->fetching = false;
	source->error = reason;

	/*
	 * What this source had cached is left exactly as it was: a list that could not
	 * be fetched today is not a list of no hubs, and replacing yesterday's copy
	 * with nothing would empty the table because a server went down.
	 */
	emit sourceFailed(url, reason);

	/* The row for this source in a view showing them changed, even though the
	   merged list did not. */
	emit changed();
	reportFetching();
}

void Qdc::HubDirectory::reportFetching()
{
	const bool fetching = isFetching();
	if (fetching == m_fetching) return;

	m_fetching = fetching;
	emit fetchingChanged(fetching);
}

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_HUBDIRECTORY_H
#define HAVE_QUICKDC_QUI_HUBDIRECTORY_H

#include "coretypes.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Qdc {

class ConfigBridge;
class CoreThread;
class HubListBridge;

/** One public hublist, and how the last attempt at it went. */
struct HubListSource {
	QString url;

	/** When the cache for this source was written; invalid if it never has been. */
	QDateTime fetched;

	/**
	 * When it was last asked for, however that went. Not persisted, so a source
	 * that is down is tried once per run of the application rather than once per
	 * look at the list.
	 */
	QDateTime attempted;

	/** How many hubs it named, of those the cache holds. */
	int hubs = 0;

	/** Why the last attempt failed; empty if it did not fail. */
	QString error;

	/** Whether a download of this source is on its way. */
	bool fetching = false;
};

/**
 * The public hubs, from several lists merged into one.
 *
 * There is no single directory of DC hubs and never was: each hublist is one
 * server's idea of what exists, they disagree, and any one of them may be down for
 * a week. So several are read and folded together, in the order they are
 * configured - which makes that order a precedence, since where two lists describe
 * the same hub the earlier one's description is the one kept.
 *
 * The merged list is cached on disk, one file per source under
 * ~/.quickdc/hublists, so that opening the application shows hubs immediately
 * rather than an empty table waiting on three HTTP requests. A cache older than
 * maxAge() is refetched; a cache that is merely old is still shown, because a
 * week old list of hubs is worth far more than no list at all.
 *
 * The sources are configuration, as
 *
 *     [Hublists]
 *     Total=2
 *     List 0=https://dchublist.org/hublist.xml.bz2
 *     List 1=https://te-home.net/?do=hublist&get=hublist.xml.bz2
 *
 * written out with the defaults the first time so that they can be edited. The
 * core neither reads nor acts on any of it; fetching is this class asking
 * HubListBridge, and merging and caching are its own.
 */
class HubDirectory final : public QObject {
	Q_OBJECT

public:
	/**
	 * @param thread may be null, and then nothing can be fetched - what the cache
	 *        holds is all there is.
	 * @param config where the sources are listed. May be null, and then the
	 *        defaults are used and nothing is written.
	 * @param cacheDir where the per-source caches live. Empty means the usual
	 *        place, ~/.quickdc/hublists; a caller passes one so that a test does
	 *        not read or write the user's.
	 */
	explicit HubDirectory(CoreThread* thread, ConfigBridge* config,
	                      const QString& cacheDir = QString(),
	                      QObject* parent = nullptr);
	~HubDirectory() override;

	/** Every hub any source named, deduplicated; see the class comment. */
	QVector<HubListEntry> hubs() const;

	int hubCount() const;

	/** The sources as configured, in precedence order. */
	QList<HubListSource> sources() const;

	/** When the freshest source was fetched, or invalid while nothing has been. */
	QDateTime lastUpdated() const;

	/**
	 * Fetch the sources.
	 *
	 * @param force fetch every source. Otherwise only those whose cache is older
	 *        than maxAge() or which have none at all, which is what makes this
	 *        safe to call every time the list is looked at.
	 */
	void refresh(bool force = false);

	/** Whether any source is being fetched. */
	bool isFetching() const;

	/** How old a cached list may be before it is fetched again. */
	static int maxAgeHours();

	/** The lists read when the configuration names none. */
	static QStringList defaultSources();

	/**
	 * Lists this once shipped as a default and no longer does.
	 *
	 * Dropped from a stored configuration as well as from the defaults, because
	 * every one of them was written into that configuration by this program
	 * rather than chosen: withdrawing a default that has stopped answering while
	 * leaving it in the file would fix nothing for anybody who had already
	 * started the program once, which is everybody.
	 *
	 * Only what was shipped, matched exactly. A list somebody added themselves is
	 * theirs, and a server that refuses today may answer next month - that is a
	 * judgement for whoever chose it, not one to make on their behalf.
	 */
	static QStringList retiredSources();

	/** Where the caches go when no directory was given. */
	static QString defaultCacheDirectory();

	/**
	 * Where this source's cache is kept.
	 *
	 * Public so that a test can put a list there without a server to fetch one
	 * from, which is the only way to exercise the merge and the reading of it.
	 */
	QString cacheFileFor(const QString& url) const;

signals:
	/** The merged list changed: a source arrived, or a cache was read. */
	void changed();

	/** A download started or finished. */
	void fetchingChanged(bool fetching);

	/**
	 * One source could not be fetched. The others are unaffected, and what this
	 * one had cached is still in the merged list.
	 */
	void sourceFailed(const QString& url, const QString& reason);

private:
	/** Read the sources from the configuration, writing the defaults if it names
	    none. */
	void loadSources();

	/** Read every source's cache. Called once, at construction. */
	void loadCaches();

	bool readCache(HubListSource& source, QVector<HubListEntry>& hubs) const;
	void writeCache(const HubListSource& source,
	                const QVector<HubListEntry>& hubs) const;

	/** Fold every source's hubs into one list; see the class comment for the
	    rule. */
	void merge();

	void onFetched(const QString& url, const QVector<HubListEntry>& hubs);
	void onFailed(const QString& url, const QString& reason);

	/** Emit fetchingChanged() if the answer has changed since last asked. */
	void reportFetching();

	/** The source with this URL, or null. */
	HubListSource* sourceFor(const QString& url);

	ConfigBridge* m_config;
	HubListBridge* m_fetch = nullptr;

	QString m_cacheDir;

	QList<HubListSource> m_sources;

	/** What each source named, by its URL, kept apart so that one arriving does
	    not disturb what the others said. */
	QHash<QString, QVector<HubListEntry>> m_perSource;

	/** The merged list, rebuilt whenever a source changes. */
	QVector<HubListEntry> m_hubs;

	/** What was last reported, so fetchingChanged() only fires on a change. */
	bool m_fetching = false;
};

}

#endif // HAVE_QUICKDC_QUI_HUBDIRECTORY_H

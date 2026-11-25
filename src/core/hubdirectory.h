/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_HUBDIRECTORY_H
#define HAVE_QUICKDC_QUI_HUBDIRECTORY_H

#include "coretypes.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QVector>

namespace QuickDC { namespace Api { struct Event; } }

namespace Qdc {

class CoreThread;

/** One public hublist, and how the last attempt at it went; QuickDC::Api::HubListSource. */
struct HubListSource {
	QString url;

	/** When the list in hand was fetched; invalid where there is none. */
	QDateTime fetched;

	/** How many hubs the list in hand names. */
	int hubs = 0;

	/** Whether the list in hand was read from the cache rather than fetched by this run. */
	bool cached = false;

	/** Why the last attempt failed; empty if it did not. */
	QString error;

	bool fetching = false;
};

/**
 * The public hubs, as the core says they are.
 *
 * The core keeps the directory: which lists are read, when each is fetched, how
 * what they say is merged, and the cache that shows it at once. This holds the last
 * directory and source states it announced, and asks it to be current.
 */
class HubDirectory final : public QObject {
	Q_OBJECT

public:
	explicit HubDirectory(CoreThread* thread, QObject* parent = nullptr);

	/** Every hub any source named, merged by the core. */
	QVector<HubListEntry> hubs() const;

	int hubCount() const;

	/** In precedence order. */
	QList<HubListSource> sources() const;

	/** When the freshest source was fetched, or invalid while nothing has been. */
	QDateTime lastUpdated() const;

	/**
	 * Ask the core to bring the directory up to date: it fetches what is stale and
	 * leaves the rest, so this is cheap to ask whenever the list is looked at.
	 * @param force fetch every source.
	 */
	void refresh(bool force = false);

	bool isFetching() const;

	/* Changes to the sources, which show once the core has said so through sourcesChanged(). */
	void addSource(const QString& url);
	void removeSource(const QString& url);
	void resetSources();

	/** Take what the core announced; public so that a test can stand in for it. */
	void take(const QuickDC::Api::Event& event);

signals:
	/** The merged list changed. */
	void changed();

	/** A source was added or removed, or how one stands changed. */
	void sourcesChanged();

	/** A download started or finished. */
	void fetchingChanged(bool fetching);

private:
	CoreThread* m_thread;
	QVector<HubListEntry> m_hubs;
	QList<HubListSource> m_sources;
	bool m_fetching = false;
};

}

#endif // HAVE_QUICKDC_QUI_HUBDIRECTORY_H

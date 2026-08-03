/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_HUBLISTBRIDGE_H
#define HAVE_QUICKDC_QUI_HUBLISTBRIDGE_H

#include "coretypes.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

namespace Qdc {

class CoreThread;
class HubListFetch;

/**
 * Downloading a public hublist, as a view sees it.
 *
 * The download itself is the core's - QuickDC::HubList fetches the XML over HTTP,
 * unpacks it if it arrived compressed, and refuses a list it could not read in
 * full - and all of that happens on the core thread, because the HTTP client is a
 * socket registered with the reactor. So each source gets a core side half, built
 * inside a job and only ever called from there, and the hubs it names cross back
 * as a value type on a queued signal.
 *
 * Lives on the thread that constructed it. One outstanding download per source
 * URL: asking again for a list already on its way is ignored rather than queued,
 * since the answer would be the same list.
 */
class HubListBridge final : public QObject {
	Q_OBJECT

public:
	explicit HubListBridge(CoreThread* thread, QObject* parent = nullptr);
	~HubListBridge() override;

	/**
	 * Fetch the hublist at 'url'.
	 *
	 * Answered exactly once by fetched() or failed(), always after this has
	 * returned and always with the URL as it was given, so a caller with several
	 * in flight can tell them apart. An address that is not an http or https URL
	 * is refused without troubling the core thread, and a repeat while one is
	 * outstanding does nothing.
	 */
	void fetch(const QString& url);

	/** Whether a download of 'url' is on its way. */
	bool isFetching(const QString& url) const;

	/** Whether any download is on its way. */
	bool isFetching() const;

signals:
	/** The hubs the list named, which may legitimately be none. */
	void fetched(const QString& url, const QVector<Qdc::HubListEntry>& hubs);

	/** @param reason a sentence fit to show whoever asked for the list. */
	void failed(const QString& url, const QString& reason);

private:
	friend class HubListFetch;

	/** Report a failure without going anywhere near the core. */
	void refuse(const QString& url, const QString& reason);

	CoreThread* m_thread;

	/**
	 * The core side half per source, built on first use.
	 *
	 * Kept rather than deleted when a download ends: a HubList owns the HTTP
	 * client that is calling back into it, so tearing one down from inside its own
	 * event would destroy the object dispatching it. There are a handful of
	 * sources and each is a client with no socket open between downloads.
	 *
	 * Only ever touched from a job on the core thread.
	 */
	QHash<QString, HubListFetch*> m_fetches;

	/** What is outstanding, as this thread sees it; see isFetching(). */
	QSet<QString> m_busy;
};

}

#endif // HAVE_QUICKDC_QUI_HUBLISTBRIDGE_H

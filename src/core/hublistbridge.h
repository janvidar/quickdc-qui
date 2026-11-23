/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_HUBLISTBRIDGE_H
#define HAVE_QUICKDC_QUI_HUBLISTBRIDGE_H

#include "coretypes.h"

#include <QObject>
#include <QSet>
#include <QString>

#include <memory>
#include <QVector>

namespace QuickDC { namespace Api { struct Event; } }

namespace Qdc {

class CoreThread;

/**
 * Downloading a public hublist, as a view sees it.
 *
 * The download itself is the core's - QuickDC::HubList fetches the XML over HTTP,
 * unpacks it if it arrived compressed, and refuses a list it could not read in
 * full. This asks for one with a FetchHubList command and hears how it went from
 * the core's events, so nothing here touches a core object or outlives one.
 *
 * Lives on the thread that constructed it. One outstanding download per source
 * URL: asking again for a list already on its way is ignored rather than queued,
 * since the answer would be the same list. A bridge destroyed while one is on
 * its way leaves it to finish in the core and simply hears nothing of it.
 */
class HubListBridge final : public QObject {
	Q_OBJECT

public:
	explicit HubListBridge(CoreThread* thread, QObject* parent = nullptr);
	~HubListBridge() override;

	/**
	 * Download the hublist at 'url' and report it with fetched() or failed().
	 *
	 * An address that is not http or https, or a core that is not running,
	 * is refused without troubling the core thread, and a repeat while one is
	 * on its way is ignored.
	 */
	void fetch(const QString& url);

	bool isFetching(const QString& url) const;
	bool isFetching() const;

signals:
	void fetched(const QString& url, const QVector<Qdc::HubListEntry>& hubs);
	void failed(const QString& url, const QString& reason);

private:
	void onCoreEvent(const QuickDC::Api::Event& event);
	void refuse(const QString& url, const QString& reason);

	CoreThread* m_thread;
	QSet<QString> m_busy;
};

}

#endif // HAVE_QUICKDC_QUI_HUBLISTBRIDGE_H

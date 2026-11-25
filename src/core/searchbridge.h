/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_SEARCHBRIDGE_H
#define HAVE_QUICKDC_QUI_SEARCHBRIDGE_H

#include "coretypes.h"

#include <QObject>
#include <QString>

#include <QSet>

namespace QuickDC { namespace Api { struct Event; } }

namespace Qdc {

class CoreThread;

/**
 * Sending a search to every hub at once, and letting it go again.
 *
 * Only the sending half. Results do not come back here: the core delivers a reply
 * to the listener of the hub it arrived on, which is a HubBridge, and each one
 * emits what it received. Whoever wants them all watches every hub - HubsView
 * forwards them, because it is what owns the hubs.
 *
 * The core numbers each search, and each result says the number of the search it
 * answers; searchSent() is where a caller learns which number its request got.
 */
class SearchBridge final : public QObject {
	Q_OBJECT

public:
	explicit SearchBridge(CoreThread* thread, QObject* parent = nullptr);
	~SearchBridge() override;

	/**
	 * Ask every hub we are logged in to.
	 *
	 * Returns at once with the number the request goes by, which searchSent()
	 * names along with the search's own; 0 if there is no core to ask.
	 */
	quint64 start(const Qdc::SearchQuery& query);

	/** No more results for the core's search 'search'. */
	void forget(quint64 search);

	/**
	 * The extensions a category stands for, lower case and without the dot.
	 *
	 * Public and static because it is the one part of a query worth testing on its
	 * own, and because a view showing what it is about to search for wants the
	 * same list.
	 */
	static QStringList extensionsFor(Qdc::SearchCategory category);

	/**
	 * Which category a filename belongs to, by its extension.
	 *
	 * Any for a name whose extension is in no group, which includes a name with no
	 * extension at all.
	 */
	static Qdc::SearchCategory categoryOf(const QString& filename);

signals:
	/**
	 * The request went out as the core's search @a search, to @a hubsAsked hubs.
	 *
	 * Asked, not searched. A hub is told regardless of what its protocol can carry,
	 * and NMDC currently carries nothing - DC::HubSession::sendSearchRequest() is a
	 * stub - so this counts hubs that were given the request rather than hubs that
	 * put it on the wire. Zero means nothing is connected.
	 */
	void searchSent(quint64 request, quint64 search, int hubsAsked);

private:
	void onCoreEvent(const QuickDC::Api::Event& event);

	CoreThread* m_thread;

	/** Requests handed to the core and not yet said to have gone. */
	QSet<quint64> m_sent;

};

}

#endif // HAVE_QUICKDC_QUI_SEARCHBRIDGE_H

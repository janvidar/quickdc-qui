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
 * Sending a search to every hub at once.
 *
 * Only the sending half. Results do not come back here: the core delivers a reply
 * to the listener of the hub it arrived on, which is a HubBridge, and each one
 * emits what it received. Whoever wants them all watches every hub - HubsView
 * forwards them, because it is what owns the hubs.
 *
 * That asymmetry is the core's, not a choice made here. There is a
 * Share::SearchManager built to hold the requests and sort replies onto them by
 * token, but nothing in the core owns one or feeds it, so a reply reaches only the
 * hub's own listener.
 *
 * The token is what ties the two halves together. It goes out with the request and
 * comes back on every reply to it, so a caller running more than one search at once
 * can tell which is which.
 */
class SearchBridge final : public QObject {
	Q_OBJECT

public:
	explicit SearchBridge(CoreThread* thread, QObject* parent = nullptr);
	~SearchBridge() override;

	/**
	 * Ask every hub we are logged in to.
	 *
	 * Returns at once, and returns the token rather than a count: how many hubs
	 * were asked is only known on the core thread, and arrives as searchSent().
	 * Getting the token synchronously is what lets a caller have somewhere to put
	 * the replies before the first one arrives.
	 *
	 * @return the token, or an empty string if there is no core to ask.
	 */
	QString start(const Qdc::SearchQuery& query);

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
	 * The request went out to @a hubsAsked hubs.
	 *
	 * Asked, not searched. A hub is told regardless of what its protocol can carry,
	 * and NMDC currently carries nothing - DC::HubSession::sendSearchRequest() is a
	 * stub - so this counts hubs that were given the request rather than hubs that
	 * put it on the wire. Zero means nothing is connected.
	 */
	void searchSent(const QString& token, int hubsAsked);

private:
	void onCoreEvent(const QuickDC::Api::Event& event);

	CoreThread* m_thread;

	/** Tokens of searches handed to the core and not yet said to have gone. */
	QSet<QString> m_sent;

	/**
	 * Numbers the searches this object has started.
	 *
	 * Only has to be unique among the searches in flight from this client, since
	 * that is all a token is ever compared against. Nothing is gained by making it
	 * unpredictable: a token is echoed back to us by anyone who answers, so it is
	 * public the moment it is used.
	 */
	quint64 m_nextToken = 1;
};

}

#endif // HAVE_QUICKDC_QUI_SEARCHBRIDGE_H

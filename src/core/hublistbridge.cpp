/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "hublistbridge.h"

#include "corethread.h"

#include <api/core.h>
#include <network/hublist.h>

#include <QMetaObject>
#include <QUrl>

#include <functional>
#include <utility>

namespace Qdc {

/**
 * The core side half of a HubListBridge: one source, and the download of it.
 *
 * Built and destroyed inside jobs on the core thread, and only ever called from
 * there. Every event copies what the core lent it before handing the copy across,
 * because the strings point into buffers the core frees as the call returns.
 */
class HubListFetch final : public QuickDC::HubListListener {

public:
	HubListFetch(HubListBridge* bridge, std::shared_ptr<Relay> relay, QString url)
		: m_bridge(bridge)
		, m_relay(std::move(relay))
		, m_url(std::move(url))
		, m_list(this)
	{
	}

	/**
	 * Start the download.
	 *
	 * A URL the core cannot use is reported as a failure like any other, rather
	 * than left as a request that is never answered.
	 */
	void download(const std::string& target)
	{
		const Samurai::IO::Net::URL url(target);

		if (!url.isValid())
		{
			fail(QStringLiteral("'%1' is not an address a hublist can be fetched "
			                    "from.").arg(m_url));
			return;
		}

		m_list.download(url);
	}

	void EventHubList(const std::vector<QuickDC::HubListEntry>& hubs) override
	{
		QVector<HubListEntry> copied;
		copied.reserve(static_cast<qsizetype>(hubs.size()));

		for (const QuickDC::HubListEntry& hub : hubs)
		{
			HubListEntry entry;
			entry.name = QString::fromStdString(hub.name);
			entry.address = QString::fromStdString(hub.address);
			entry.description = QString::fromStdString(hub.description);
			entry.country = QString::fromStdString(hub.country);
			entry.users = hub.users;
			entry.shared = hub.shared;
			entry.minshare = hub.minshare;
			entry.minslots = hub.minslots;
			entry.maxhubs = hub.maxhubs;
			entry.maxusers = hub.maxusers;

			copied.append(entry);
		}

		const QString url = m_url;

		report([url, copied](HubListBridge* bridge) {
			emit bridge->fetched(url, copied);
		});
	}

	void EventHubListFailure(const char* reason) override
	{
		/*
		 * The core's reasons are sentence fragments - "the server answered 404 Not
		 * Found" - so the address goes in front of one, which is what makes it
		 * readable when several sources are being fetched at once.
		 */
		fail(QStringLiteral("%1 could not be fetched: %2")
			.arg(m_url, QString::fromUtf8(reason ? reason : "")));
	}

private:
	using Work = std::function<void(HubListBridge*)>;

	void fail(const QString& reason)
	{
		report([reason, url = m_url](HubListBridge* bridge) {
			emit bridge->failed(url, reason);
		});
	}

	void report(Work work)
	{
		HubListBridge* bridge = m_bridge;

		std::shared_ptr<Relay> relay = m_relay;
		Relay* target = relay.get();

		QMetaObject::invokeMethod(target, [relay = std::move(relay), bridge, work] {
			if (!relay->detached()) work(bridge);
		}, Qt::QueuedConnection);
	}

	HubListBridge* m_bridge;
	std::shared_ptr<Relay> m_relay;
	QString m_url;
	QuickDC::HubList m_list;
};

}

Qdc::HubListBridge::HubListBridge(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
	, m_fetches(std::make_shared<FetchBox>())
	, m_relay(Relay::create())
{
	/*
	 * Connected before anybody else can be, so that a handler of either signal
	 * already sees the download as finished rather than as still on its way.
	 */
	connect(this, &HubListBridge::fetched, this, [this](const QString& url,
	                                                    const QVector<HubListEntry>&) {
		m_busy.remove(url);
	});

	connect(this, &HubListBridge::failed, this, [this](const QString& url,
	                                                   const QString&) {
		m_busy.remove(url);
	});
}

Qdc::HubListBridge::~HubListBridge()
{
	/* Nothing in flight has anywhere to report to from here on. */
	m_relay->detach();

	if (!m_thread) return;

	/*
	 * On the core thread, because a fetch still in flight owns a socket the reactor
	 * is watching - abandoning it would leave the monitor holding a client that has
	 * gone away. Posted rather than waited on, and queued after any job already
	 * posted, so a download started a moment ago is aborted rather than missed.
	 */
	m_thread->post([holder = m_fetches] {
		for (HubListFetch* fetch : holder->fetches) delete fetch;
		holder->fetches.clear();
	});
}

void Qdc::HubListBridge::fetch(const QString& url)
{
	const QString wanted = url.trimmed();
	if (wanted.isEmpty()) return;

	/* The answer would be the same list, so a repeat is dropped rather than
	   queued behind the one already on its way. */
	if (m_busy.contains(wanted)) return;

	const QUrl parsed(wanted, QUrl::StrictMode);
	const QString scheme = parsed.scheme().toLower();

	if (!parsed.isValid() || parsed.host().isEmpty()
		|| (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")))
	{
		refuse(wanted, tr("'%1' is not an http or https address.").arg(wanted));
		return;
	}

	if (!m_thread || !m_thread->isCoreRunning())
	{
		refuse(wanted, tr("The core is not running, so nothing can be fetched."));
		return;
	}

	m_busy.insert(wanted);

	const std::string target = wanted.toStdString();

	m_thread->post([thread = m_thread, holder = m_fetches, relay = m_relay,
	                bridge = this, wanted, target] {
		QuickDC::Core* core = thread->core();

		if (!core)
		{
			const QString reason = tr("The core is not running, so nothing can be "
			                          "fetched.");

			Relay* answer = relay.get();

			QMetaObject::invokeMethod(answer,
				[relay, bridge, wanted, reason] {
					if (!relay->detached()) emit bridge->failed(wanted, reason);
				}, Qt::QueuedConnection);
			return;
		}

		/*
		 * One per source, built on first use and kept afterwards; see the member.
		 * A job posted while the core is stopping is dropped rather than run, which
		 * leaves this download unanswered - acceptable only because the alternative
		 * is answering a view that is being torn down with it.
		 */
		HubListFetch*& fetch = holder->fetches[wanted];
		if (!fetch) fetch = new HubListFetch(bridge, relay, wanted);

		fetch->download(target);
	});
}

bool Qdc::HubListBridge::isFetching(const QString& url) const
{
	return m_busy.contains(url.trimmed());
}

bool Qdc::HubListBridge::isFetching() const
{
	return !m_busy.isEmpty();
}

void Qdc::HubListBridge::refuse(const QString& url, const QString& reason)
{
	/*
	 * Queued, although nothing was posted anywhere: a caller asking for several
	 * sources in a loop would otherwise be called back part way through its own
	 * loop, and only for the addresses that happen to be unusable. Every answer
	 * arriving the same way is one behaviour to reason about instead of two.
	 */
	QMetaObject::invokeMethod(this, [this, url, reason] {
		emit failed(url, reason);
	}, Qt::QueuedConnection);
}

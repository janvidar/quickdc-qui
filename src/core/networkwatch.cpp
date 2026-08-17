/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "networkwatch.h"

#include <QHostAddress>
#include <QNetworkInformation>
#include <QNetworkInterface>
#include <QStringList>
#include <QTimer>

namespace {

/**
 * How long the network has to hold still before the change is reported.
 *
 * Long enough to cover a whole switch rather than its first step: associating
 * with an access point, taking a lease and having a route again is seconds of
 * work, and each part of it arrives as its own signal. Short enough that a user
 * who reconnected on purpose and is watching for the answer does not think
 * nothing happened.
 */
constexpr int SETTLE_MS = 3000;

/**
 * How often the local addresses are enumerated.
 *
 * The only cost of asking is a walk of the interface list, which is why this can
 * be short enough that a lease renewed onto a different address is noticed while
 * the user is still wondering why nothing works, rather than at the next thing
 * that happened to shake the link. Nothing is sent anywhere by asking; the check
 * that does talk to a server is started by what this reports, and only when the
 * answer actually differs.
 */
constexpr int POLL_MS = 15000;

/**
 * What both of Qt's state enums use for a value it has not established.
 *
 * Reachability and TransportMedium each name their zero 'Unknown', which is what
 * a backend reports until it has worked the answer out - so a move off it is the
 * backend learning what was already true rather than anything about the network
 * having changed.
 */
constexpr int NOT_KNOWN = 0;

/** Every address on the machine, as text; see NetworkWatch::addressFingerprint. */
QStringList localAddresses()
{
	const QList<QHostAddress> found = QNetworkInterface::allAddresses();

	QStringList text;
	text.reserve(found.size());

	for (const QHostAddress& one : found)
		text.append(one.toString());

	return text;
}

/**
 * Whether this state is one a reachability check could succeed from.
 *
 * Site is not enough. It means the local network answers but the internet
 * beyond it does not, which is the state a captive portal leaves a laptop in,
 * and a STUN probe from there reaches nothing. Local is less than that again.
 *
 * Unknown counts as online, because it is what a backend reports when it cannot
 * tell rather than a report that nothing is reachable - the glib backend gives
 * it while NetworkManager is still deciding, and a backend without the
 * Reachability feature never says anything else. Treating it as offline would
 * make this silent on exactly the platforms that need it.
 */
bool state_is_usable(QNetworkInformation::Reachability reachability)
{
	return reachability == QNetworkInformation::Reachability::Online
	    || reachability == QNetworkInformation::Reachability::Unknown;
}

}

Qdc::NetworkWatch::NetworkWatch(QObject* parent)
	: QObject(parent)
	, m_settle(new QTimer(this))
	, m_poll(new QTimer(this))
	, m_addresses(addressFingerprint(localAddresses()))
{
	m_settle->setSingleShot(true);
	m_settle->setInterval(SETTLE_MS);
	connect(m_settle, &QTimer::timeout, this, &NetworkWatch::settle);

	/*
	 * Started whether or not a backend is found below, since where there is none
	 * this is the only thing that will ever report anything.
	 */
	m_poll->setInterval(POLL_MS);
	connect(m_poll, &QTimer::timeout, this, &NetworkWatch::pollAddresses);
	m_poll->start();

	if (!QNetworkInformation::loadDefaultBackend()) return;

	QNetworkInformation* information = QNetworkInformation::instance();
	if (!information) return;

	m_watching = true;

	m_reachability = static_cast<int>(information->reachability());
	m_medium = static_cast<int>(information->transportMedium());

	connect(information, &QNetworkInformation::reachabilityChanged,
	        this, [this](QNetworkInformation::Reachability now) {
		noteLearned(m_reachability, static_cast<int>(now));
	});
	connect(information, &QNetworkInformation::transportMediumChanged,
	        this, [this](QNetworkInformation::TransportMedium now) {
		noteLearned(m_medium, static_cast<int>(now));
	});
	connect(information, &QNetworkInformation::isMeteredChanged,
	        this, [this](bool now) { noteFlag(m_metered, now); });
	connect(information, &QNetworkInformation::isBehindCaptivePortalChanged,
	        this, [this](bool now) { noteFlag(m_captive, now); });
}

Qdc::NetworkWatch::~NetworkWatch() = default;

bool Qdc::NetworkWatch::isWatching() const
{
	return m_watching;
}

bool Qdc::NetworkWatch::isOnline() const
{
	if (!m_watching) return true;

	QNetworkInformation* information = QNetworkInformation::instance();
	if (!information) return true;

	return state_is_usable(information->reachability());
}

QString Qdc::NetworkWatch::addressFingerprint(const QStringList& addresses)
{
	QStringList kept;
	kept.reserve(addresses.size());

	for (const QString& text : addresses)
	{
		const QHostAddress one(text);

		if (one.isNull() || one.isLoopback() || one.isLinkLocal()) continue;

		kept.append(one.toString());
	}

	kept.sort();
	kept.removeDuplicates();

	return kept.join(QLatin1Char(' '));
}

void Qdc::NetworkWatch::pollAddresses()
{
	const QString seen = addressFingerprint(localAddresses());

	if (seen == m_addresses) return;

	m_addresses = seen;
	noteTransition();
}

void Qdc::NetworkWatch::noteLearned(int& held, int now)
{
	const int was = held;
	held = now;

	if (was == now) return;
	if (was == NOT_KNOWN) return;

	noteTransition();
}

void Qdc::NetworkWatch::noteFlag(int& held, bool now)
{
	const int was = held;
	held = now ? 1 : 0;

	if (was < 0 || was == held) return;

	noteTransition();
}

void Qdc::NetworkWatch::noteTransition()
{
	m_settle->start();
}

void Qdc::NetworkWatch::settle()
{
	if (!isOnline()) return;

	emit connectivityChanged();
}

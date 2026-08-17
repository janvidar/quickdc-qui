/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "networkwatch.h"

#include <QNetworkInformation>
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
{
	m_settle->setSingleShot(true);
	m_settle->setInterval(SETTLE_MS);
	connect(m_settle, &QTimer::timeout, this, &NetworkWatch::settle);

	if (!QNetworkInformation::loadDefaultBackend()) return;

	QNetworkInformation* information = QNetworkInformation::instance();
	if (!information) return;

	m_watching = true;

	connect(information, &QNetworkInformation::reachabilityChanged,
	        this, [this](QNetworkInformation::Reachability) { noteTransition(); });
	connect(information, &QNetworkInformation::transportMediumChanged,
	        this, [this](QNetworkInformation::TransportMedium) { noteTransition(); });
	connect(information, &QNetworkInformation::isMeteredChanged,
	        this, [this](bool) { noteTransition(); });
	connect(information, &QNetworkInformation::isBehindCaptivePortalChanged,
	        this, [this](bool) { noteTransition(); });
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

void Qdc::NetworkWatch::noteTransition()
{
	m_settle->start();
}

void Qdc::NetworkWatch::settle()
{
	if (!isOnline()) return;

	emit connectivityChanged();
}

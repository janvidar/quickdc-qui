/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "networkwatch.h"

#include <QNetworkInformation>

namespace {

/*
 * Qt's zero for both enums is 'Unknown': a move off it is the backend learning
 * what was already true, which it announces as a change on every launch.
 */
constexpr int NOT_KNOWN = 0;

}

Qdc::NetworkWatch::NetworkWatch(QObject* parent)
	: QObject(parent)
{
	if (!QNetworkInformation::loadDefaultBackend()) return;

	QNetworkInformation* information = QNetworkInformation::instance();
	if (!information) return;

	m_watching = true;
	m_reachability = static_cast<int>(information->reachability());
	m_medium = static_cast<int>(information->transportMedium());

	connect(information, &QNetworkInformation::reachabilityChanged,
	        this, [this](QNetworkInformation::Reachability now) { note(m_reachability, static_cast<int>(now)); });
	connect(information, &QNetworkInformation::transportMediumChanged,
	        this, [this](QNetworkInformation::TransportMedium now) { note(m_medium, static_cast<int>(now)); });
	connect(information, &QNetworkInformation::isMeteredChanged,
	        this, [this](bool now) { note(m_metered, now ? 1 : 2); });
	connect(information, &QNetworkInformation::isBehindCaptivePortalChanged,
	        this, [this](bool now) { note(m_captive, now ? 1 : 2); });
}

Qdc::NetworkWatch::~NetworkWatch() = default;

bool Qdc::NetworkWatch::isWatching() const
{
	return m_watching;
}

void Qdc::NetworkWatch::note(int& held, int now)
{
	const int was = held;
	held = now;
	if (was == now || was == NOT_KNOWN || was < 0) return;
	emit changed(online());
}

/*
 * Site is not enough: it is the state a captive portal leaves a laptop in, where
 * a probe reaches nothing. Unknown counts as online, since a backend that cannot
 * tell says it, and treating it as offline would silence this where it is needed.
 */
bool Qdc::NetworkWatch::online() const
{
	QNetworkInformation* information = QNetworkInformation::instance();
	if (!information) return true;
	const auto reachability = information->reachability();
	return reachability == QNetworkInformation::Reachability::Online
	    || reachability == QNetworkInformation::Reachability::Unknown;
}

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "networkmode.h"

#include <api/networkmode.h>

namespace {

QuickDC::NetworkMode toCore(Qdc::NetworkMode mode)
{
	switch (mode)
	{
		case Qdc::NetworkMode::Automatic: return QuickDC::NetworkMode::Automatic;
		case Qdc::NetworkMode::Advanced:  return QuickDC::NetworkMode::Advanced;
		case Qdc::NetworkMode::Passive:   return QuickDC::NetworkMode::Passive;
	}

	return QuickDC::NetworkMode::Advanced;
}

Qdc::NetworkMode fromCore(QuickDC::NetworkMode mode)
{
	switch (mode)
	{
		case QuickDC::NetworkMode::Automatic: return Qdc::NetworkMode::Automatic;
		case QuickDC::NetworkMode::Advanced:  return Qdc::NetworkMode::Advanced;
		case QuickDC::NetworkMode::Passive:   return Qdc::NetworkMode::Passive;
	}

	return Qdc::NetworkMode::Advanced;
}

}

QString Qdc::networkModeToken(Qdc::NetworkMode mode)
{
	return QString::fromLatin1(QuickDC::toString(toCore(mode)));
}

Qdc::NetworkMode Qdc::networkModeFromToken(const QString& token,
                                           Qdc::NetworkMode fallback)
{
	return fromCore(QuickDC::networkModeFrom(token.toStdString(),
	                                         toCore(fallback)));
}

Qdc::NetworkModeKeys Qdc::networkModeKeys(Qdc::NetworkMode mode,
                                          bool storedUseUpnp,
                                          bool storedForceActive,
                                          const QString& storedServer)
{
	const QuickDC::NetworkModeKeys keys = QuickDC::networkModeKeys(
		toCore(mode), storedUseUpnp, storedForceActive, storedServer.toStdString());

	Qdc::NetworkModeKeys copied;

	copied.activeMode = keys.activeMode;
	copied.useUpnp = keys.useUpnp;
	copied.forceActive = keys.forceActive;
	copied.reachabilityServer = QString::fromStdString(keys.reachabilityServer);

	return copied;
}

QString Qdc::defaultReachabilityServer()
{
	return QString::fromLatin1(QuickDC::defaultReachabilityServer());
}

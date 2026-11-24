/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include "uisettings.h"

#include <config/settings.h>

namespace {

constexpr Qdc::Settings::Text from(const QuickDC::Settings::Text& s) { return {s.group, s.key, s.fallback}; }
constexpr Qdc::Settings::Number from(const QuickDC::Settings::Number& s) { return {s.group, s.key, s.fallback}; }
constexpr Qdc::Settings::Flag from(const QuickDC::Settings::Flag& s) { return {s.group, s.key, s.fallback}; }

}

/* constinit, so a view's own static built from one of these never sees it empty. */
namespace Qdc::Settings {

constinit const Text   Nickname             = from(QuickDC::Settings::Nickname);
constinit const Text   Description          = from(QuickDC::Settings::Description);
constinit const Text   Email                = from(QuickDC::Settings::Email);
constinit const Text   Password             = from(QuickDC::Settings::Password);
constinit const Text   ClientId             = from(QuickDC::Settings::ClientId);
constinit const Text   ConnectionMode       = from(QuickDC::Settings::ConnectionMode);
constinit const Flag   ActiveMode           = from(QuickDC::Settings::ActiveMode);
constinit const Number Port                 = from(QuickDC::Settings::Port);
constinit const Text   Bind                 = from(QuickDC::Settings::Bind);
constinit const Text   Address              = from(QuickDC::Settings::Address);
constinit const Text   ReachabilityServer   = from(QuickDC::Settings::ReachabilityServer);
constinit const Flag   ForceActiveMode      = from(QuickDC::Settings::ForceActiveMode);
constinit const Flag   UseUpnp              = from(QuickDC::Settings::UseUpnp);
constinit const Number Slots                = from(QuickDC::Settings::Slots);
constinit const Number MaxDownloads         = from(QuickDC::Settings::MaxDownloads);
constinit const Number ChunkSize            = from(QuickDC::Settings::ChunkSize);
constinit const Text   DownloadDirectory    = from(QuickDC::Settings::DownloadDirectory);
constinit const Flag   HubEnabled           = from(QuickDC::Settings::HubEnabled);
constinit const Text   HubName              = from(QuickDC::Settings::HubName);
constinit const Text   HubDescription       = from(QuickDC::Settings::HubDescription);
constinit const Number HubMaxUsers          = from(QuickDC::Settings::HubMaxUsers);
constinit const Number HubMaxHistory        = from(QuickDC::Settings::HubMaxHistory);
constinit const Number HubMaxSendQueue      = from(QuickDC::Settings::HubMaxSendQueue);
constinit const Number HubMaxPerAddress     = from(QuickDC::Settings::HubMaxPerAddress);
constinit const Number HubMaxHandshakes     = from(QuickDC::Settings::HubMaxHandshakes);
constinit const Number HubHandshakeTimeout  = from(QuickDC::Settings::HubHandshakeTimeout);
constinit const Number HubIdleTimeout       = from(QuickDC::Settings::HubIdleTimeout);
constinit const Number HubLingerTimeout     = from(QuickDC::Settings::HubLingerTimeout);
constinit const Flag   HubCheckClaims       = from(QuickDC::Settings::HubCheckClaims);

}

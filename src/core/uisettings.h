/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QDC_UISETTINGS_H
#define HAVE_QDC_UISETTINGS_H

#include <QString>

/**
 * Every setting the interface reads, each named once with its default.
 *
 * The core's own are its schema's (quickdc/core/config/settings.h), handed on by
 * uisettings.cpp because a view may not include a core header: a field left unset on
 * the settings page shows what the core will really do. The page once kept copies of
 * those defaults, and an empty nickname field stood for "QuickDC" while its "Active
 * mode" default disagreed with the core's. The ones defined here are the interface's
 * alone - the core stores them and never reads them.
 */
namespace Qdc::Settings {

struct Text   { const char* group; const char* key; const char* fallback; };
struct Number { const char* group; const char* key; int fallback; };
struct Flag   { const char* group; const char* key; bool fallback; };

extern const Text   Nickname;
extern const Text   Description;
extern const Text   Email;
extern const Text   Password;
extern const Text   ClientId;
extern const Text   ConnectionMode;
extern const Flag   ActiveMode;
extern const Number Port;
extern const Text   Bind;
extern const Text   Address;
extern const Text   ReachabilityServer;
extern const Flag   ForceActiveMode;
extern const Flag   UseUpnp;
extern const Number Slots;
extern const Number MaxDownloads;
extern const Number ChunkSize;
extern const Text   DownloadDirectory;
extern const Flag   HubEnabled;
extern const Text   HubName;
extern const Text   HubDescription;
extern const Number HubMaxUsers;
extern const Number HubMaxHistory;
extern const Number HubMaxSendQueue;
extern const Number HubMaxPerAddress;
extern const Number HubMaxHandshakes;
extern const Number HubHandshakeTimeout;
extern const Number HubIdleTimeout;
extern const Number HubLingerTimeout;
extern const Flag   HubCheckClaims;

extern const Flag   ChatMediaFetch;
extern const Number ChatMediaLimit;
extern const Number ChatMediaKeepDays;

/**
 * Bulletin boards; a hub's own group in the session store may override the first two.
 *
 * Fetching ahead is off because a post is fetched from whoever holds it, which at
 * first is only its author: fetching unasked tells them who read it and when.
 */
inline constexpr Flag   BoardPrefetch     {"Boards", "Fetch a thread ahead", false};
inline constexpr Flag   BoardServe        {"Boards", "Offer what I have read", true};
inline constexpr Number BoardKeepDays     {"Boards", "Keep posts for days", 90};
inline constexpr Number BoardCache        {"Boards", "Cache size", 256 * 1024 * 1024};

/** The colour scheme by name; empty for the built-in default, see defaultTheme(). */
inline constexpr Text   ThemeName         {"Interface", "Theme", ""};

/** For matching a change notice, or reading a hub's own value in the session store. */
template <typename Setting> QString groupOf(const Setting& setting) { return QString::fromUtf8(setting.group); }
template <typename Setting> QString keyOf(const Setting& setting) { return QString::fromUtf8(setting.key); }

}

#endif // HAVE_QDC_UISETTINGS_H

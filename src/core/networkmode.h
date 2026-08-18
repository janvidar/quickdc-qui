/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_NETWORKMODE_H
#define HAVE_QUICKDC_QUI_NETWORKMODE_H

#include <QString>

namespace Qdc {

/**
 * How much of getting connected the client works out for itself;
 * QuickDC::NetworkMode.
 *
 * Mirrored rather than reused, as the reachability verdict is, so that nothing
 * above this layer includes a core header - the views are built with no path to
 * quickdc/core at all.
 */
enum class NetworkMode {
	/** Pick the port, ask the gateway to forward it, and measure the result. */
	Automatic,

	/** The plumbing is the user's to set, and they are shown whether it worked. */
	Advanced,

	/** Nothing is forwarded and nothing connects in. */
	Passive
};

/** The spelling stored in [Network] Mode. */
QString networkModeToken(NetworkMode mode);

/**
 * The mode a token names, or 'fallback' where it names none.
 *
 * Never guesses: an unrecognised word is a file somebody edited by hand, and
 * reading a typo as automatic would begin measuring against a third party and
 * announcing this machine to the local segment on the strength of it.
 */
NetworkMode networkModeFromToken(const QString& token, NetworkMode fallback);

/**
 * What a mode implies for the settings under it.
 *
 * The answers come from the core's own function and not from a second copy of the
 * rules. Two readers that worked this out separately would eventually disagree, and
 * the disagreement would be between what a page shows and what the client does.
 *
 * The three 'stored' arguments are what the file holds now; advanced answers with
 * them, because advanced means the plumbing is as somebody left it.
 */
struct NetworkModeKeys {
	bool activeMode = true;
	bool useUpnp = false;
	bool forceActive = false;
	QString reachabilityServer;
};

NetworkModeKeys networkModeKeys(NetworkMode mode, bool storedUseUpnp,
                                bool storedForceActive,
                                const QString& storedServer);

/** The server a client measures against unless its file names another. */
QString defaultReachabilityServer();

}

#endif // HAVE_QUICKDC_QUI_NETWORKMODE_H

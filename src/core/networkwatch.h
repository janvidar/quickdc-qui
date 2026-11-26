/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_NETWORKWATCH_H
#define HAVE_QUICKDC_QUI_NETWORKWATCH_H

#include <QObject>

namespace Qdc {

/**
 * What the platform says about the network, for the core, which cannot hear
 * it: Qt's network information - reachability, the transport medium, metered,
 * captive portal - each transition reported as it comes. The core watches the
 * addresses itself, waits for a change to settle and decides what to check;
 * this only passes on what Qt knows.
 *
 * It lives on the thread with the event loop, since QNetworkInformation
 * delivers through signals. A platform Qt has no backend for leaves a watcher
 * that never fires.
 */
class NetworkWatch final : public QObject {
	Q_OBJECT

public:
	explicit NetworkWatch(QObject* parent = nullptr);
	~NetworkWatch() override;

	/** Whether Qt's backend was found. */
	bool isWatching() const;

signals:
	/** A transition, and whether anything beyond the local network answers now. */
	void changed(bool online);

private:
	/** Report a move off a state the backend had already established. */
	void note(int& held, int now);

	bool online() const;

	bool m_watching = false;
	int m_reachability = -1;
	int m_medium = -1;
	int m_metered = -1;
	int m_captive = -1;
};

}

#endif // HAVE_QUICKDC_QUI_NETWORKWATCH_H

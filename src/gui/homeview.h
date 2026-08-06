/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_HOMEVIEW_H
#define HAVE_QUICKDC_QUI_HOMEVIEW_H

#include "view.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTimer;

namespace Qdc {

class Completed;
class ConfigBridge;
class Favourites;
class HubBrowser;
class HubDirectory;
class RecentHubs;
class TransferBridge;

/**
 * Where the window opens: who you are, where to find a hub, and what has been
 * happening.
 *
 * All four of the things the plan asks for are here. The welcome and the summary of
 * recent transfers are this class's own; finding a hub is a HubBrowser, which is
 * the public lists, the favourites and the hubs already visited - and beside it an
 * address box, because a hub nobody has published can still be typed in.
 */
class HomeView final : public View {
	Q_OBJECT

public:
	/** Every argument may be null; the matching section then says it is empty. */
	HomeView(ConfigBridge* config, Favourites* favourites, RecentHubs* recent,
	         HubDirectory* directory, Completed* completed,
	         TransferBridge* transfers, QWidget* parent = nullptr);

	QString title() const override;
	Glyph glyph() const override;

	/** Re-read everything. Called on becoming visible and when a store changes. */
	void refresh();

	/** The hub lists, the favourites and the hubs visited. */
	HubBrowser* hubs() const;

	/** For tests. */
	int favouriteCount() const;
	int recentHubCount() const;

	/** How many finished downloads the activity summary is listing. */
	int completedCount() const;

signals:
	/**
	 * Open this hub.
	 *
	 * Home cannot do it itself - the hubs live in another view - so the window
	 * routes it. Emitted for a favourite and for a typed address alike.
	 */
	void hubRequested(const QString& url);

protected:
	/** The rate line is polled, so the timer only runs while it can be seen. */
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	QWidget* buildGreeting();
	QWidget* buildActivity();

	/**
	 * A tab page: the content given its margins, and scrolled when the window is too
	 * short for it. As SettingsView does it, and for the same reason.
	 */
	QWidget* makePage(QWidget* page);

	void refreshGreeting();
	void refreshActivity();

	void connectToTypedAddress();

	ConfigBridge* m_config;
	Completed* m_completed;
	TransferBridge* m_transfers;

	QLabel* m_greeting;
	QLabel* m_identity;

	QLineEdit* m_address;
	QPushButton* m_connect;

	HubBrowser* m_hubs;

	QLabel* m_transferSummary;
	QListWidget* m_recentList;
	QLabel* m_recentEmpty;

	QTimer* m_poll;
};

}

#endif // HAVE_QUICKDC_QUI_HOMEVIEW_H

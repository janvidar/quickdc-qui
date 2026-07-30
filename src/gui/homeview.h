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
class TransferBridge;

/**
 * Where the window opens: who you are, the hubs worth keeping, and what has been
 * happening.
 *
 * The plan asks for four things here. Three are built - a welcome, the favourites,
 * and a summary of recent transfers. The fourth, hub discovery, is not: it means
 * fetching a public hub list over HTTP, and the core has no HTTP client -
 * network/http/client.h and .cpp contain nothing but a copyright notice, and the
 * core's own documentation/TODO schedules hublist parsing for a later release. An
 * address box stands in its place, since typing an address is the other way of
 * getting to a hub.
 */
class HomeView final : public View {
	Q_OBJECT

public:
	/** Every argument may be null; the matching section then says it is empty. */
	HomeView(ConfigBridge* config, Favourites* favourites, Completed* completed,
	         TransferBridge* transfers, QWidget* parent = nullptr);

	QString title() const override;
	Glyph glyph() const override;

	/** Re-read everything. Called on becoming visible and when a store changes. */
	void refresh();

	/** For tests. */
	int favouriteCount() const;
	int recentCount() const;

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
	QWidget* buildFavourites();
	QWidget* buildActivity();

	void refreshGreeting();
	void refreshFavourites();
	void refreshActivity();

    /** Open whatever is selected in the favourites list. */
	void openSelectedFavourite();
	void removeSelectedFavourite();
	void connectToTypedAddress();

	ConfigBridge* m_config;
	Favourites* m_favourites;
	Completed* m_completed;
	TransferBridge* m_transfers;

	QLabel* m_greeting;
	QLabel* m_identity;

	QLineEdit* m_address;
	QPushButton* m_connect;

	QListWidget* m_favouriteList;
	QPushButton* m_openFavourite;
	QPushButton* m_removeFavourite;
	QLabel* m_favouritesEmpty;

	QLabel* m_transferSummary;
	QListWidget* m_recentList;
	QLabel* m_recentEmpty;

	QTimer* m_poll;
};

}

#endif // HAVE_QUICKDC_QUI_HOMEVIEW_H

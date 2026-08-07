/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_MAINWINDOW_H
#define HAVE_QUICKDC_QUI_MAINWINDOW_H

#include <QList>
#include <QMainWindow>

class QLabel;
class QStackedWidget;

namespace Qdc {

class Completed;
class ConfigBridge;
class CornerMask;
class CoreThread;
class Favourites;
class FilelistBridge;
class HomeView;
class HubDirectory;
class HubsView;
class RecentHubs;
class SearchBridge;
class SearchView;
class Shares;
class NavigationRail;
class TransferBridge;
class View;

/**
 * The application window: a navigation rail, and one view at a time beside it.
 *
 * The window owns the bridges and hands each view the ones it needs, so a view
 * never reaches for the core itself and the lifetimes stay in one place. That
 * ordering matters on the way out as well - see the destructor.
 *
 * Secondary navigation and the context sidebar from the layout are deliberately
 * not here. Only the hub view has any use for either, and a window that laid
 * them out would have to keep asking every view whether it wanted them.
 */
class MainWindow final : public QMainWindow {
	Q_OBJECT

public:
	/**
	 * @param thread a started core thread. A stopped one is accepted - the
	 *        bridges report their own failures - but nothing will work.
	 */
	explicit MainWindow(CoreThread* thread, QWidget* parent = nullptr);
	~MainWindow() override;

	/** For tests; the router is otherwise driven by the rail. */
	int currentIndex() const;
	void setCurrentIndex(int index);

	NavigationRail* rail() const;

protected:
	/** Keeps the corner mask over the content stack as the window is resized. */
	bool eventFilter(QObject* watched, QEvent* event) override;

	/**
	 * Keeps the native title bar coloured. Qt undoes it whenever it rewrites the
	 * window's style mask, so it is reapplied after anything that might have.
	 */
	bool event(QEvent* event) override;

private:
	/**
	 * Add a view as a rail destination, wiring its badge to the rail and taking
	 * ownership of it.
	 */
	void addView(View* view);

	/** Follow the current view: rail selection, stack, window title. */
	void showView(int index);

	CoreThread* m_thread;

	/**
	 * The bridges, and the favourites store built on one of them. Owned here so
	 * that every view shares them, and destroyed in a fixed order - see the
	 * destructor.
	 */
	ConfigBridge* m_config;
	TransferBridge* m_transfers;
	FilelistBridge* m_filelists;
	SearchBridge* m_searches;
	Favourites* m_favourites;
	Completed* m_completed;
	Shares* m_shares;
	RecentHubs* m_recent;

	/**
	 * The public hub lists.
	 *
	 * Owned here rather than by the view that shows them, because it fetches over
	 * the core thread and so has to be destroyed while the core is still up - which
	 * is what the order in the destructor arranges.
	 */
	HubDirectory* m_directory;

	/** Kept for openHub(), which the Home view will want. */
	HubsView* m_hubs;

	/** Kept so that it can be destroyed before the stores it was handed. */
	HomeView* m_home;

	/** Kept so that hub replies can be routed to it; see the constructor. */
	SearchView* m_search;

	NavigationRail* m_rail;
	QStackedWidget* m_stack;
	QList<View*> m_views;

	/** Rounds the corners of the content, from above. Never laid out. */
	CornerMask* m_mask = nullptr;

};

}

#endif // HAVE_QUICKDC_QUI_MAINWINDOW_H

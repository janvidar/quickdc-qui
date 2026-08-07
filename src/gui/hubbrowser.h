/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_HUBBROWSER_H
#define HAVE_QUICKDC_QUI_HUBBROWSER_H

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSortFilterProxyModel;
class QTabWidget;
class QTableView;

namespace Qdc {

class FavouriteDelegate;

class Favourites;
class HubDirectory;
class HubListModel;
class RecentHubs;

/**
 * Where to find a hub: the public lists, the ones kept, and the ones visited.
 *
 * Three tabs rather than three boxes stacked up, because the three are alternative
 * answers to the same question and only one of them is ever being read. The public
 * list is also thousands of rows and wants the height to itself.
 *
 * The tab strip is offered rather than kept private - see tabs(). Home puts its own
 * pages in it, so that a window showing this has one row of tabs and not a row under
 * a row, and the tabs here are addressed by their page rather than by an index for
 * exactly that reason.
 *
 * Nothing here opens a hub. The chat lives in another view, so a choice made here
 * leaves as hubRequested() and whoever placed this widget routes it - the same
 * arrangement HomeView already has with the address box.
 */
class HubBrowser final : public QWidget {
	Q_OBJECT

public:
	enum Tab {
		PublicTab,
		FavouritesTab,

		/** The hubs already visited; labelled "Visited". */
		VisitedTab
	};

	/**
	 * Every argument may be null, and the matching tab then says it is empty
	 * rather than being left out - a tab that appears and disappears is harder to
	 * follow than one that explains itself.
	 */
	HubBrowser(HubDirectory* directory, Favourites* favourites, RecentHubs* recent,
	           QWidget* parent = nullptr);

	void showTab(Tab tab);

	/**
	 * Which of the three is showing.
	 *
	 * PublicTab when what is showing is a page somebody else added to the strip,
	 * because the question this answers is which hub list is current and the answer
	 * is then none of them.
	 */
	Tab currentTab() const;

	/**
	 * The tab strip, so that the page hosting this can add its own tabs to it.
	 *
	 * The same arrangement as ChatPane: this owns the strip and whoever places it
	 * appends to it, which is what keeps one window to one row of tabs.
	 */
	QTabWidget* tabs() const;

	/** Fetch the public lists that are stale, or all of them. */
	void refreshPublicLists(bool force);

	/** What the public list is being narrowed by; empty shows everything. */
	void setFilter(const QString& text);
	QString filter() const;

	/** For tests: the rows each tab is showing, the public one after filtering. */
	int publicCount() const;
	int favouriteCount() const;
	int recentCount() const;

signals:
	/** Open this hub. */
	void hubRequested(const QString& url);

protected:
	/**
	 * Fetch what is stale on the way in, so that looking at the list is what keeps
	 * it current and there is nothing to press.
	 */
	void showEvent(QShowEvent* event) override;

private:
	QWidget* buildPublic();
	QWidget* buildFavourites();
	QWidget* buildRecent();

	void refreshPublic();
	void refreshFavourites();
	void refreshRecent();

	/**
	 * Set the star on every visited row again, without rebuilding the list.
	 *
	 * Keeping a hub changes one thing about a row that is otherwise the same, and a
	 * rebuild would take the selection and the scroll position with it - and quietly
	 * re-date every "last visited" while it was at it.
	 */
	void refreshRecentStars();

	/** The line under the public list: how many hubs, from how many lists, when. */
	void refreshStatus();

	/** Fetch if the public list is what is being looked at; see showEvent(). */
	void refreshIfPublic();

	/** The address selected in the public table, or empty. */
	QString selectedPublicAddress() const;

	/** The address selected in a favourites or recent row, or empty. */
	static QString selectedAddress(const QListWidget* list);

	/**
	 * Say that a list is empty, in the list.
	 *
	 * Rather than a label beside it that appears and disappears: these lists live in
	 * a stacked layout inside another one, and a widget being shown in a page that
	 * is not the current one was enough to bring that page to the front - so the
	 * favourites tab took over from the public list as soon as there was a
	 * favourite. Nothing here changes what is visible any more.
	 *
	 * The row cannot be selected, so the buttons stay as they were.
	 */
	static void explain(QListWidget* list, const QString& why);

	/** How many rows name a hub, which a row explaining an empty list does not. */
	static int hubRows(const QListWidget* list);

	/** Bookmark the selected public hub, or take the bookmark off again. */
	void toggleBookmark();

	/** Keep @a address, or stop keeping it. What every star reports to. */
	void toggleFavourite(const QString& address);

	/** What can be done with the hub under the pointer, on each of the three tabs. */
	void showPublicMenu(const QPoint& where);
	void showListMenu(QListWidget* list, const QPoint& where);

	/**
	 * The menu itself, which is the same on all three tabs bar one entry.
	 *
	 * @param visited whether to offer taking the hub off the list of those visited,
	 *        which is the one thing only that tab can do.
	 */
	void hubMenu(const QPoint& globalPos, const QString& name,
	             const QString& address, bool visited);

	/** Follow the selection: which buttons make sense, and what they say. */
	void followPublicSelection();
	void followFavouriteSelection();
	void followRecentSelection();

	HubDirectory* m_directory;
	Favourites* m_favourites;
	RecentHubs* m_recent;

	QTabWidget* m_tabs;

	/**
	 * The three pages, kept so that a tab can be found by what is on it.
	 *
	 * By page rather than by index because the strip is shared: Home inserts its own
	 * tabs into it, and an index that meant one thing on its own means another once
	 * somebody has added a page in front.
	 */
	QWidget* m_visitedPage = nullptr;
	QWidget* m_publicPage = nullptr;
	QWidget* m_favouritesPage = nullptr;

	QLineEdit* m_search;
	QPushButton* m_refresh;
	QTableView* m_table;
	HubListModel* m_model;

	/** Sorts on the model's edit role and filters on every column; see the model. */
	QSortFilterProxyModel* m_filtered;

	QLabel* m_status;
	QPushButton* m_openPublic;
	QPushButton* m_bookmark;

	/**
	 * Which way the public list is sorted, kept here rather than read back off the
	 * table.
	 *
	 * Replacing the rows resets the header, which puts its sort indicator back on the
	 * first column - and the view answers that by sorting the table that way. So the
	 * header cannot be asked what the sorting is: what it says after a list arrives is
	 * not what anybody chose. Held instead, and applied again after every arrival.
	 */
	int m_sortColumn;
	Qt::SortOrder m_sortOrder = Qt::DescendingOrder;

	/** Set while the rows are being replaced, so the header being reset in the
	    middle of it is not mistaken for somebody sorting the table. */
	bool m_replacingRows = false;

	/**
	 * The stars. Two, because the table has a column for one and the two lists put
	 * it in front of the row's own text; see FavouriteDelegate.
	 */
	FavouriteDelegate* m_columnStar;
	FavouriteDelegate* m_listStar;

	QListWidget* m_favouriteList;
	QPushButton* m_openFavourite;
	QPushButton* m_unbookmark;

	QListWidget* m_recentList;
	QPushButton* m_openRecent;
	/**
	 * Bookmark, or unbookmark, whichever hub is selected here.
	 *
	 * The star in the row does the same thing with the pointer; this is how it is
	 * done without one. The other two tabs already have their own such button, and
	 * on macOS a context menu has no key to raise it - so without this, keeping a
	 * hub from the list of those visited could only be done by clicking.
	 */
	QPushButton* m_bookmarkRecent;

	QPushButton* m_forget;
	QPushButton* m_clearRecent;
};

}

#endif // HAVE_QUICKDC_QUI_HUBBROWSER_H

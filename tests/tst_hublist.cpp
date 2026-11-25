/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/completed.h>
#include <core/configbridge.h>
#include <core/corethread.h>
#include <core/favourites.h>
#include <core/hubdirectory.h>
#include <core/hubbridge.h>
#include <core/recenthubs.h>
#include <core/testing.h>
#include <gui/homeview.h>
#include <gui/hubbrowser.h>
#include <gui/favouritedelegate.h>
#include <gui/theme.h>
#include <gui/favouritedelegate.h>
#include <gui/hubheader.h>
#include <gui/hublistmodel.h>

#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QPainter>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableView>
#include <QTextBlock>
#include <QTextDocument>
#include <QHeaderView>
#include <QTemporaryDir>
#include <QTest>

/**
 * Finding a hub: the public lists, and the two stores of
 * hubs somebody has already been to or kept.
 *
 * The directory is the core's - its sources, the merge, the cache, when each list is
 * fetched - and is tested there and by qdc's scenario. Here it is what a view draws:
 * a bridge given a directory as though the core had announced it, and one real core
 * whose only source knocks on a closed port of the loopback address, which is how a
 * failure inside the core reaches a view. Nothing here reaches another host.
 *
 * What a list says is a stranger's text, so how it is drawn belongs here too: the
 * status line under the table, the rows of the two other tabs, and the header of a
 * hub opened from one of them.
 */
class TestHubList : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();

	void recentHubsAreWhatTheCoreRecorded();
	void favouritesAreWhatTheCoreKeeps();



	void theDirectoryIsWhatTheCoreAnnounces();
	void aRefusedConnectionIsShownAsTheSourcesError();

	void theModelSortsOnNumbersAndFiltersOnEveryColumn();

	void theBrowserListsWhatTheDirectoryHolds();
	void theBrowserStaysOnTheTabItIsShowing();
	void theBrowserBookmarksAndUnbookmarks();
	void theBrowserForgetsARecentHub();

	void theStarColumnKeepsAPublicHub();
	void theStarColumnIsFixedAndStaysOutOfTheSort();
	void theVisitedListShowsAndTogglesTheStar();
	void theExplanationRowHasNoStar();

	void theStatusLineDrawsWhatASourceSaidAsText();
	void aVisitedRowIsPlainWhereItsTooltipIsMarkup();
	void theHeaderDrawsAHubsDescriptionAsText();
	void aFinishedDownloadShowsItsWholePathAsText();

private:
	/**
	 * What a tooltip is drawn as.
	 *
	 * The two steps a QLabel left on Qt::AutoText takes, which is where QToolTip leaves
	 * the label it draws in: ask Qt::mightBeRichText(), and parse the string as HTML if
	 * the answer is yes. Whether a hub's description is drawn as markup is Qt's decision
	 * about a string rather than something the string looks like, so Qt is what is asked.
	 */
	static QString asShown(const QString& tooltip);

	/**
	 * Whether anything in 'tooltip' survived being parsed as markup.
	 *
	 * Asked of the parsed document rather than of the text, since an escaped "&lt;b&gt;"
	 * is exactly the right output and still contains "b". A picture is a resource the
	 * document goes and fetches and a link is somewhere the reader can be sent; emphasis
	 * is milder but is still the hub setting the type in the client's own chrome.
	 */
	static bool drawsMarkup(const QString& tooltip);

	/** Have 'directory' hold 'hubs' as though the core had merged them from the one list at 'url'. */
	static void listed(Qdc::HubDirectory& directory, const QString& url,
	                   const QVector<Qdc::HubListEntry>& hubs);

	/** Let go of every hub the core keeps, and wait for 'kept' to hear it. */
	void forgetFavourites(Qdc::Favourites& kept);

	/** A visit, as the core records one: by opening the hub. */
	void visit(const QString& url);

	QTemporaryDir m_dir;
	Qdc::CoreThread* m_thread = nullptr;
	Qdc::ConfigBridge* m_config = nullptr;
};

void TestHubList::initTestCase()
{
	QVERIFY2(m_dir.isValid(), "could not create a temporary directory");

	const QDir dir(m_dir.path());
	m_thread = new Qdc::CoreThread(this);
	m_thread->setPreferenceFiles(dir.filePath(QStringLiteral("quickdc.conf")),
	                             dir.filePath(QStringLiteral("session.conf")),
	                             dir.filePath(QStringLiteral("queue.xml")));

	QVERIFY2(m_thread->startAndWait(),
	         qPrintable(QStringLiteral("the core did not start: ")
	                    + m_thread->failureReason()));

	m_config = new Qdc::ConfigBridge(m_thread, this);
}

void TestHubList::cleanupTestCase()
{
	delete m_config;
	m_config = nullptr;

	if (m_thread) m_thread->shutdown();
}

QString TestHubList::asShown(const QString& tooltip)
{
	QTextDocument document;

	if (Qt::mightBeRichText(tooltip))
		document.setHtml(tooltip);
	else
		document.setPlainText(tooltip);

	return document.toPlainText();
}

bool TestHubList::drawsMarkup(const QString& tooltip)
{
	QTextDocument document;
	document.setHtml(tooltip);

	for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
	{
		for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
		{
			const QTextCharFormat format = it.fragment().charFormat();

			if (format.isImageFormat() || format.isAnchor()) return true;
			if (format.fontItalic() || format.fontWeight() > QFont::Normal) return true;
		}
	}

	return false;
}

namespace {

Qdc::RecentHub visited(const QString& url, const QString& name = QString(), qint64 at = 1769000000)
{
	Qdc::RecentHub hub;
	hub.url = url;
	hub.name = name;
	hub.visited = QDateTime::fromSecsSinceEpoch(at);
	return hub;
}

}

void TestHubList::listed(Qdc::HubDirectory& directory, const QString& url,
                         const QVector<Qdc::HubListEntry>& hubs)
{
	Qdc::HubListSource source;
	source.url = url;
	source.fetched = QDateTime::currentDateTime();
	source.hubs = static_cast<int>(hubs.size());

	Qdc::Testing::announce(directory, hubs, {source});
}

void TestHubList::forgetFavourites(Qdc::Favourites& kept)
{
	for (const QString& url : kept.urls()) kept.remove(url);
	QTRY_VERIFY(kept.urls().isEmpty());
}

void TestHubList::visit(const QString& url)
{
	Qdc::HubBridge opened(m_thread, url);
	Qdc::RecentHubs recent(m_thread);
	QTRY_VERIFY(recent.contains(url));
}

/* Opening a hub is a visit the core records, and the bridge shows; the rules are the core's. */
void TestHubList::recentHubsAreWhatTheCoreRecorded()
{
	Qdc::RecentHubs recent(m_thread);
	recent.clear();
	QTRY_COMPARE(recent.count(), 0);

	QSignalSpy changes(&recent, &Qdc::RecentHubs::changed);

	visit(QStringLiteral("adcs://one.example.org:412"));
	visit(QStringLiteral("two.example.org"));
	QTRY_COMPARE(recent.count(), 2);
	QVERIFY(changes.count() >= 2);

	/* Newest first, and filed under the address the hub connection is keyed on. */
	QCOMPARE(recent.entries().at(0).url, QStringLiteral("dchub://two.example.org:411"));
	QVERIFY(recent.entries().at(0).visited.isValid());
	QVERIFY(recent.contains(QStringLiteral("dchub://two.example.org")));

	/* Stored as it was before the core kept it, so nobody's list is lost. */
	QTRY_VERIFY(m_config->getString(Qdc::ConfigStore::Application, QStringLiteral("Recent hubs"),
	                                QStringLiteral("Hub 0"))
	            .endsWith(QStringLiteral("|dchub://two.example.org:411|")));

	Qdc::RecentHubs reread(m_thread);
	QCOMPARE(reread.count(), 2);

	recent.remove(QStringLiteral("two.example.org"));
	QTRY_COMPARE(recent.count(), 1);
	QCOMPARE(recent.entries().at(0).url, QStringLiteral("adcs://one.example.org:412"));

	recent.clear();
	QTRY_COMPARE(recent.count(), 0);
	QCOMPARE(Qdc::RecentHubs::limit(), 32);
}

void TestHubList::favouritesAreWhatTheCoreKeeps()
{
	Qdc::Favourites kept(m_thread);
	forgetFavourites(kept);

	kept.add(QStringLiteral("example.org"));
	QTRY_COMPARE(kept.urls(), QStringList{QStringLiteral("dchub://example.org:411")});
	QVERIFY(kept.contains(QStringLiteral("dchub://example.org")));

	kept.toggle(QStringLiteral("example.org"));
	QTRY_VERIFY(kept.urls().isEmpty());
}

/* A bridge built once the core is up starts with what the core announced, and asks it to be current. */
void TestHubList::theDirectoryIsWhatTheCoreAnnounces()
{
	Qdc::HubDirectory watching(m_thread);
	watching.resetSources();
	QTRY_COMPARE(watching.sources().size(), 3);
	QCOMPARE(watching.sources().at(0).url, QStringLiteral("https://dchublist.org/hublist.xml.bz2"));

	/* What the core holds is in a bridge built afterwards from the first moment, without asking. */
	Qdc::HubDirectory later(m_thread);
	QCOMPARE(later.sources().size(), 3);
	QCOMPARE(later.hubCount(), 0);
	QVERIFY(!later.isFetching());
}

/*
 * A source the core could not fetch is drawn with the core's reason. Port 1 of the
 * loopback address, so the fetch fails by connection refused within a moment and the
 * suite stays offline.
 */
void TestHubList::aRefusedConnectionIsShownAsTheSourcesError()
{
	Qdc::HubDirectory directory(m_thread);
	directory.resetSources();
	for (const QString& url : {QStringLiteral("https://dchublist.org/hublist.xml.bz2"),
	                           QStringLiteral("https://te-home.net/?do=hublist&get=hublist.xml.bz2"),
	                           QStringLiteral("https://hublist.eu/hublist.xml.bz2")})
		directory.removeSource(url);
	QTRY_VERIFY(directory.sources().isEmpty());

	const QString url = QStringLiteral("http://127.0.0.1:1/hublist.xml");
	directory.addSource(url);

	QTRY_VERIFY_WITH_TIMEOUT(directory.sources().size() == 1
		&& !directory.sources().at(0).error.isEmpty(), 15000);
	QCOMPARE(directory.sources().at(0).url, url);
	QVERIFY(!directory.isFetching());
	QCOMPARE(directory.hubCount(), 0);

	Qdc::HubBrowser browser(&directory, nullptr, nullptr);
	QLabel* status = browser.findChild<QLabel*>(QStringLiteral("mutedText"));
	QVERIFY(status);
	QVERIFY2(asShown(status->toolTip()).startsWith(url + QStringLiteral(" — ")), qPrintable(status->toolTip()));
	QVERIFY2(status->text().contains(QStringLiteral("could not be fetched")), qPrintable(status->text()));

	/*
	 * Asked to be current a moment after the failure, the core leaves the source alone:
	 * nothing is announced between the refresh and the answer to a command sent after it,
	 * which the core carries out and announces in order.
	 */
	QSignalSpy changes(&directory, &Qdc::HubDirectory::sourcesChanged);
	Qdc::Favourites kept(m_thread);
	forgetFavourites(kept);
	directory.refresh();
	kept.add(QStringLiteral("after.example.org"));
	QTRY_VERIFY(kept.contains(QStringLiteral("after.example.org")));
	QCOMPARE(changes.count(), 0);
	forgetFavourites(kept);

	directory.removeSource(url);
	QTRY_VERIFY(directory.sources().isEmpty());
}

void TestHubList::theModelSortsOnNumbersAndFiltersOnEveryColumn()
{
	Qdc::HubListEntry small;
	small.name = QStringLiteral("Small");
	small.address = QStringLiteral("dchub://small.example.org:411");
	small.country = QStringLiteral("SE");
	small.users = 9;

	Qdc::HubListEntry large;
	large.name = QStringLiteral("Large");
	large.address = QStringLiteral("dchub://large.example.org:411");
	large.description = QStringLiteral("Films and music");
	large.country = QStringLiteral("NO");
	large.users = 1000;

	Qdc::HubListModel model;
	model.setHubs({small, large});

	QCOMPARE(model.rowCount(), 2);
	QCOMPARE(model.columnCount(), int(Qdc::HubListModel::ColumnCount));

	/* A hub with no name of its own is identified by its address. */
	Qdc::HubListEntry nameless;
	nameless.address = QStringLiteral("dchub://nameless.example.org:411");
	model.setHubs({nameless});
	QCOMPARE(model.index(0, Qdc::HubListModel::NameColumn).data().toString(),
	         nameless.address);

	model.setHubs({small, large});

	QSortFilterProxyModel proxy;
	proxy.setSourceModel(&model);
	proxy.setSortRole(Qt::EditRole);
	proxy.setFilterKeyColumn(-1);
	proxy.setFilterCaseSensitivity(Qt::CaseInsensitive);

	/* Sorted as numbers rather than as text, which would put 1000 before 9. */
	proxy.sort(Qdc::HubListModel::UsersColumn, Qt::DescendingOrder);
	QCOMPARE(proxy.index(0, Qdc::HubListModel::NameColumn).data().toString(),
	         QStringLiteral("Large"));

	/* Every column, so a country code or a word from a description narrows the
	   list as readily as a name does. */
	proxy.setFilterFixedString(QStringLiteral("se"));
	QCOMPARE(proxy.rowCount(), 1);
	QCOMPARE(proxy.index(0, Qdc::HubListModel::NameColumn).data().toString(),
	         QStringLiteral("Small"));

	proxy.setFilterFixedString(QStringLiteral("music"));
	QCOMPARE(proxy.rowCount(), 1);
	QCOMPARE(proxy.index(0, Qdc::HubListModel::AddressColumn)
			.data(Qdc::HubListModel::AddressRole).toString(),
	         large.address);

	proxy.setFilterFixedString(QString());
	QCOMPARE(proxy.rowCount(), 2);
}

void TestHubList::theBrowserListsWhatTheDirectoryHolds()
{
	const QString url = QStringLiteral("http://browser.example.org/hublist.xml");
	Qdc::HubListEntry one;
	one.name = QStringLiteral("Films");
	one.address = QStringLiteral("dchub://films.example.org:411");
	one.users = 300;

	Qdc::HubListEntry two;
	two.name = QStringLiteral("Music");
	two.address = QStringLiteral("dchub://music.example.org:411");
	two.users = 20;

	Qdc::HubDirectory filled(nullptr);
	listed(filled, url, {one, two});
	QCOMPARE(filled.hubCount(), 2);

	Qdc::Favourites favourites(nullptr);
	Qdc::RecentHubs recent(nullptr);
	Qdc::Testing::announce(recent, {visited(two.address, QStringLiteral("Music"))});

	Qdc::HubBrowser browser(&filled, &favourites, &recent);

	QCOMPARE(browser.currentTab(), Qdc::HubBrowser::PublicTab);
	QCOMPARE(browser.publicCount(), 2);
	QCOMPARE(browser.recentCount(), 1);
	QCOMPARE(browser.favouriteCount(), 0);

	/* The search narrows what is shown and nothing else. */
	browser.setFilter(QStringLiteral("music"));
	QCOMPARE(browser.publicCount(), 1);
	QCOMPARE(filled.hubCount(), 2);

	browser.setFilter(QString());
	QCOMPARE(browser.publicCount(), 2);

	/* Double clicking a row asks for the hub rather than opening it here. */
	QSignalSpy requested(&browser, &Qdc::HubBrowser::hubRequested);
	QVERIFY(requested.isValid());

	QTableView* table = browser.findChild<QTableView*>(QStringLiteral("publicHubs"));
	QVERIFY(table);

	table->setCurrentIndex(table->model()->index(0, 0));

	QPushButton* connectTo =
		browser.findChild<QPushButton*>(QStringLiteral("connectPublicHub"));
	QVERIFY(connectTo);
	QVERIFY(connectTo->isEnabled());
	connectTo->click();

	QCOMPARE(requested.count(), 1);

	/* Busiest first, so the row that was selected is the larger hub. */
	QCOMPARE(requested.at(0).at(0).toString(), one.address);

	/*
	 * And a list arriving leaves it that way. Replacing the rows clears the header's
	 * sort indicator, which the view reports as a request to sort by nothing - so
	 * without putting it back, the first list to arrive left the table in whatever
	 * order the sources merged in.
	 */
	QMetaObject::invokeMethod(&filled, "changed");

	QCOMPARE(table->model()->index(0, Qdc::HubListModel::NameColumn).data().toString(),
	         QStringLiteral("Films"));

	/* The selection follows the hub it was on rather than the row number. */
	QCOMPARE(browser.findChild<QTableView*>(QStringLiteral("publicHubs"))
			->currentIndex().data(Qdc::HubListModel::AddressRole).toString(),
	         one.address);
}

void TestHubList::theBrowserStaysOnTheTabItIsShowing()
{
	/*
	 * A store filling in is not a request to change tab. It is worth a test of its
	 * own because the browser is shown here: a widget shown inside a tab that is not
	 * the current one is enough to make that tab current, so a favourite arriving
	 * once took the reader off the list they were reading.
	 */
	Qdc::Favourites favourites(nullptr);
	Qdc::RecentHubs recent(nullptr);

	/* Already kept before the browser is built, which is the ordinary case: the
	   stores are read at startup and the window is put together afterwards. */
	Qdc::Testing::announce(favourites, {QStringLiteral("dchub://early.example.org:411")});

	Qdc::HubBrowser browser(nullptr, &favourites, &recent);
	browser.show();
	QVERIFY(QTest::qWaitForWindowExposed(&browser));

	QCOMPARE(browser.currentTab(), Qdc::HubBrowser::PublicTab);

	Qdc::Testing::announce(favourites, {QStringLiteral("dchub://early.example.org:411"),
	                                    QStringLiteral("dchub://late.example.org:411")});
	QCOMPARE(browser.favouriteCount(), 2);
	QCOMPARE(browser.currentTab(), Qdc::HubBrowser::PublicTab);

	Qdc::Testing::announce(recent, {visited(QStringLiteral("dchub://later.example.org:411"))});
	QCOMPARE(browser.recentCount(), 1);
	QCOMPARE(browser.currentTab(), Qdc::HubBrowser::PublicTab);

	/*
	 * And the same inside the arrangement the window actually builds: the browser
	 * nested in Home, Home a page of the view stack. That is where it went wrong -
	 * the tabs are a stacked layout inside another one, and the pages of the inner
	 * one are only hidden once it has been laid out.
	 */
	Qdc::Favourites kept(nullptr);
	Qdc::Testing::announce(kept, {QStringLiteral("adcs://kept.example.org:412")});

	Qdc::RecentHubs visited(nullptr);

	Qdc::HomeView* home = new Qdc::HomeView(nullptr, &kept, &visited, nullptr,
	                                        nullptr, nullptr);

	QStackedWidget stack;
	stack.addWidget(home);
	stack.setCurrentIndex(0);
	stack.resize(1100, 720);
	stack.show();

	QVERIFY(QTest::qWaitForWindowExposed(&stack));

	QCOMPARE(home->hubs()->favouriteCount(), 1);

	/*
	 * Home shares the browser's tab strip and opens on its own first page, so what is
	 * current here is the welcome rather than a hub list. Asserted through the strip
	 * rather than through currentTab(), which answers about the three hub lists and
	 * says PublicTab when none of them is showing.
	 */
	QTabWidget* strip = home->hubs()->tabs();
	QVERIFY(strip);
	QCOMPARE(strip->currentIndex(), 0);
	QCOMPARE(strip->tabText(0), QStringLiteral("Welcome"));

	/* And the hub lists are all in the one strip, after it. */
	QCOMPARE(strip->count(), 5);
	QCOMPARE(strip->tabText(1), QStringLiteral("Visited"));
	QCOMPARE(strip->tabText(2), QStringLiteral("Public hubs"));
	QCOMPARE(strip->tabText(3), QStringLiteral("Favourites"));
	QCOMPARE(strip->tabText(4), QStringLiteral("Activity"));

	/* Selecting one still lands on that list, index shift and all. */
	home->hubs()->showTab(Qdc::HubBrowser::FavouritesTab);
	QCOMPARE(home->hubs()->currentTab(), Qdc::HubBrowser::FavouritesTab);
	QCOMPARE(strip->currentIndex(), 3);
}

void TestHubList::theBrowserBookmarksAndUnbookmarks()
{
	const QString url = QStringLiteral("http://bookmark.example.org/hublist.xml");
	Qdc::HubListEntry hub;
	hub.name = QStringLiteral("Keepable");
	hub.address = QStringLiteral("adcs://keepable.example.org:412");
	hub.users = 12;

	Qdc::HubDirectory directory(nullptr);
	listed(directory, url, {hub});
	Qdc::Favourites favourites(m_thread);
	forgetFavourites(favourites);

	Qdc::HubBrowser browser(&directory, &favourites, nullptr);

	QTableView* table = browser.findChild<QTableView*>(QStringLiteral("publicHubs"));
	QVERIFY(table);
	table->setCurrentIndex(table->model()->index(0, 0));

	QPushButton* bookmark =
		browser.findChild<QPushButton*>(QStringLiteral("bookmarkHub"));
	QVERIFY(bookmark);

	/* The one button says which way it would go, so its text is part of what it
	   does. */
	QCOMPARE(bookmark->text(), QStringLiteral("Bookmark"));

	bookmark->click();
	QTRY_VERIFY(favourites.contains(hub.address));
	QCOMPARE(bookmark->text(), QStringLiteral("Unbookmark"));
	QCOMPARE(browser.favouriteCount(), 1);

	bookmark->click();
	QTRY_VERIFY(!favourites.contains(hub.address));
	QCOMPARE(bookmark->text(), QStringLiteral("Bookmark"));
	QCOMPARE(browser.favouriteCount(), 0);
}

void TestHubList::theBrowserForgetsARecentHub()
{
	Qdc::RecentHubs recent(m_thread);
	recent.clear();
	QTRY_COMPARE(recent.count(), 0);
	visit(QStringLiteral("dchub://kept.example.org:411"));
	visit(QStringLiteral("dchub://unwanted.example.org:411"));
	QTRY_COMPARE(recent.count(), 2);

	Qdc::HubBrowser browser(nullptr, nullptr, &recent);
	browser.showTab(Qdc::HubBrowser::VisitedTab);

	QCOMPARE(browser.recentCount(), 2);

	QListWidget* list = browser.findChild<QListWidget*>(QStringLiteral("recentHubs"));
	QVERIFY(list);

	/* Newest first, so the unwanted one is the row at the top. */
	list->setCurrentRow(0);

	QPushButton* forget =
		browser.findChild<QPushButton*>(QStringLiteral("forgetRecentHub"));
	QVERIFY(forget);
	QVERIFY(forget->isEnabled());
	forget->click();

	QTRY_COMPARE(browser.recentCount(), 1);
	QVERIFY(!recent.contains(QStringLiteral("dchub://unwanted.example.org:411")));
	QVERIFY(recent.contains(QStringLiteral("dchub://kept.example.org:411")));

	QPushButton* clear =
		browser.findChild<QPushButton*>(QStringLiteral("clearRecentHubs"));
	QVERIFY(clear);
	clear->click();

	QTRY_COMPARE(browser.recentCount(), 0);
	QCOMPARE(recent.count(), 0);
}

void TestHubList::theStarColumnKeepsAPublicHub()
{
	const QString url = QStringLiteral("http://star.example.org/hublist.xml");
	Qdc::HubListEntry hub;
	hub.name = QStringLiteral("Starrable");

	/* Spelled without a port, as a hublist may. Kept under the one address the
	   store files a hub by, so the star on this row agrees with the one in a
	   hub's own header. */
	hub.address = QStringLiteral("adcs://starrable.example.org");
	/* 411 whatever the scheme; see urlNormalisationFillsInSchemeAndPort. */
	hub.users = 7;

	Qdc::HubDirectory directory(nullptr);
	listed(directory, url, {hub});
	Qdc::Favourites favourites(m_thread);
	forgetFavourites(favourites);

	Qdc::HubBrowser browser(&directory, &favourites, nullptr);
	browser.showTab(Qdc::HubBrowser::PublicTab);
	browser.resize(700, 300);
	browser.show();
	QVERIFY(QTest::qWaitForWindowExposed(&browser));

	QTableView* table = browser.findChild<QTableView*>(QStringLiteral("publicHubs"));
	QVERIFY(table);
	QCOMPARE(table->model()->rowCount(), 1);

	const QModelIndex star =
		table->model()->index(0, Qdc::HubListModel::FavouriteColumn);

	QVERIFY(!star.data(Qdc::HubListModel::FavouriteRole).toBool());

	/* Through the viewport, so the delegate is what handles it. */
	QTest::mouseClick(table->viewport(), Qt::LeftButton, {},
	                  table->visualRect(star).center());

	QTRY_VERIFY(favourites.contains(hub.address));
	QCOMPARE(favourites.urls().at(0),
	         QStringLiteral("adcs://starrable.example.org:411"));

	/* Lit without the list having been fetched again; see HubListModel. */
	QVERIFY(star.data(Qdc::HubListModel::FavouriteRole).toBool());
	QCOMPARE(browser.favouriteCount(), 1);

	QTest::mouseClick(table->viewport(), Qt::LeftButton, {},
	                  table->visualRect(star).center());

	QTRY_VERIFY(!favourites.contains(hub.address));
	QVERIFY(!star.data(Qdc::HubListModel::FavouriteRole).toBool());
}

void TestHubList::theStarColumnIsFixedAndStaysOutOfTheSort()
{
	const QString url = QStringLiteral("http://sorted.example.org/hublist.xml");
	Qdc::HubListEntry small;
	small.name = QStringLiteral("Small");
	small.address = QStringLiteral("adc://small.example.org:412");
	small.users = 3;

	Qdc::HubListEntry large;
	large.name = QStringLiteral("Large");
	large.address = QStringLiteral("adc://large.example.org:412");
	large.users = 300;

	Qdc::HubDirectory directory(nullptr);
	listed(directory, url, {small, large});
	Qdc::HubBrowser browser(&directory, nullptr, nullptr);

	QTableView* table = browser.findChild<QTableView*>(QStringLiteral("publicHubs"));
	QVERIFY(table);

	QHeaderView* header = table->horizontalHeader();

	QCOMPARE(header->sectionResizeMode(Qdc::HubListModel::FavouriteColumn),
	         QHeaderView::Fixed);

	const int before = header->sortIndicatorSection();

	/*
	 * The star's column holds no value to order a list of hubs by, so asking for it
	 * puts the indicator back where it was rather than emptying the table's order.
	 */
	table->sortByColumn(Qdc::HubListModel::FavouriteColumn, Qt::AscendingOrder);
	QCOMPARE(header->sortIndicatorSection(), before);
}

void TestHubList::theVisitedListShowsAndTogglesTheStar()
{
	constexpr int FavouriteRole = Qt::UserRole + 2;

	Qdc::RecentHubs recent(nullptr);
	Qdc::Testing::announce(recent, {visited(QStringLiteral("dchub://visited.example.org:411"),
	                                        QStringLiteral("Visited"))});

	Qdc::Favourites favourites(m_thread);
	forgetFavourites(favourites);

	Qdc::HubBrowser browser(nullptr, &favourites, &recent);
	browser.showTab(Qdc::HubBrowser::VisitedTab);
	browser.resize(500, 300);
	browser.show();
	QVERIFY(QTest::qWaitForWindowExposed(&browser));

	QListWidget* list = browser.findChild<QListWidget*>(QStringLiteral("recentHubs"));
	QVERIFY(list);
	QCOMPARE(list->count(), 1);

	list->setCurrentRow(0);
	QVERIFY(!list->item(0)->data(FavouriteRole).toBool());

	/*
	 * Keeping it elsewhere lights the star here without the list being rebuilt - so
	 * the row that was current still is, and nothing has been re-dated.
	 */
	favourites.add(QStringLiteral("dchub://visited.example.org:411"));
	QTRY_VERIFY(favourites.contains(QStringLiteral("dchub://visited.example.org:411")));

	QCOMPARE(list->count(), 1);
	QCOMPARE(list->currentRow(), 0);
	QVERIFY(list->item(0)->data(FavouriteRole).toBool());

	/* And the star itself takes it off again. */
	const QRect row = list->visualItemRect(list->item(0));
	QTest::mouseClick(list->viewport(), Qt::LeftButton, {},
	                  QPoint(row.left() + 10, row.center().y()));

	QTRY_VERIFY(!favourites.contains(QStringLiteral("dchub://visited.example.org:411")));

	/* A click on the words is a selection and nothing more. */
	favourites.add(QStringLiteral("dchub://visited.example.org:411"));
	QTRY_VERIFY(favourites.contains(QStringLiteral("dchub://visited.example.org:411")));
	QTest::mouseClick(list->viewport(), Qt::LeftButton, {},
	                  QPoint(row.right() - 4, row.center().y()));

	Qdc::Testing::runOnCore(m_thread, [] {});
	QCoreApplication::processEvents();
	QVERIFY(favourites.contains(QStringLiteral("dchub://visited.example.org:411")));
	forgetFavourites(favourites);
}

void TestHubList::theExplanationRowHasNoStar()
{
	Qdc::Favourites favourites(nullptr);

	Qdc::HubBrowser browser(nullptr, &favourites, nullptr);
	browser.showTab(Qdc::HubBrowser::FavouritesTab);
	browser.resize(500, 300);
	browser.show();
	QVERIFY(QTest::qWaitForWindowExposed(&browser));

	QListWidget* list =
		browser.findChild<QListWidget*>(QStringLiteral("favouriteHubs"));

	QVERIFY(list);
	QCOMPARE(list->count(), 1);
	QCOMPARE(browser.favouriteCount(), 0);

	QSignalSpy changed(&favourites, &Qdc::Favourites::changed);
	QVERIFY(changed.isValid());

	/* The note explaining an empty list is not a hub, so it has nothing to keep. */
	const QRect row = list->visualItemRect(list->item(0));
	QTest::mouseClick(list->viewport(), Qt::LeftButton, {},
	                  QPoint(row.left() + 10, row.center().y()));

	QCOMPARE(changed.count(), 0);

	/*
	 * And nothing was drawn to press in the first place, which is the half a signal
	 * count cannot show: Favourites::add() refuses an empty address of its own accord,
	 * so a star drawn on this row and pressed would still emit nothing, and every
	 * assertion above would hold with a star sitting on it.
	 */
	const QImage note = list->viewport()->grab().toImage();
	const int strip = qMin(28, row.width());
	const QRect where(row.left(), row.top(), strip, row.height());

	/*
	 * And nothing was drawn to press in the first place, which is the half a signal
	 * count cannot show: Favourites::add() refuses an empty address of its own accord,
	 * so a star drawn on this row and pressed would still emit nothing, and every
	 * assertion above would hold with a star sitting on it.
	 *
	 * Asked of the delegate rather than of the browser, and of two rows carrying the
	 * same words. A row's text is drawn in the same corner as its star and moves aside
	 * to make room for one, so a picture of a real list differs in two ways at once and
	 * neither can be read on its own. With the text held still the only thing that can
	 * differ is the star.
	 */
	Qdc::FavouriteDelegate delegate(Qdc::FavouriteDelegate::BeforeTheText,
	                                Qt::UserRole + 1, Qt::UserRole + 2);

	QStandardItemModel rows(2, 1);

	for (int n = 0; n < 2; n++)
	{
		rows.setData(rows.index(n, 0), QStringLiteral("A hub"), Qt::DisplayRole);
		rows.setData(rows.index(n, 0), true, Qt::UserRole + 1);
	}

	/* The second is a hub and the first is the note, which is the whole difference. */
	rows.setData(rows.index(1, 0), QStringLiteral("adc://192.0.2.60:412"),
	             Qt::UserRole + 2);

	const auto painted = [&](int n) {
		QImage sheet(160, 32, QImage::Format_ARGB32_Premultiplied);
		sheet.fill(Qt::white);

		QStyleOptionViewItem shown;
		shown.initFrom(list);
		shown.rect = QRect(0, 0, sheet.width(), sheet.height());
		shown.state &= ~QStyle::State_Selected;

		QPainter painter(&sheet);
		delegate.paint(&painter, shown, rows.index(n, 0));
		painter.end();

		return sheet;
	};

	const QImage without = painted(0);
	const QImage with = painted(1);

	QVERIFY2(without != with,
	         "the note explaining an empty list was drawn exactly like a hub, so it was "
	         "given a star to press");
}

void TestHubList::theStatusLineDrawsWhatASourceSaidAsText()
{
	/*
	 * Two lists, one of them named in a way that is markup to the label QToolTip draws
	 * in. Which source said what is the one place where the wording between the fields
	 * is ours and has to survive as structure while none of the fields may add to it.
	 */
	const QString doctored =
		QStringLiteral("http://lists.example.org/<b>hublist</b>.xml");
	const QString plain = QStringLiteral("http://other.example.org/hublist.xml");

	Qdc::HubListEntry hub;
	hub.name = QStringLiteral("Listed");
	hub.address = QStringLiteral("adcs://listed.example.org:412");

	Qdc::HubListSource unfetched;
	unfetched.url = doctored;

	Qdc::HubListSource fetched;
	fetched.url = plain;
	fetched.fetched = QDateTime::currentDateTime();
	fetched.hubs = 1;

	Qdc::HubDirectory directory(nullptr);
	Qdc::Testing::announce(directory, {hub}, {unfetched, fetched});
	Qdc::HubBrowser browser(&directory, nullptr, nullptr);

	/* The one muted label in the browser; see HubBrowser's constructor. */
	QLabel* status = browser.findChild<QLabel*>(QStringLiteral("mutedText"));
	QVERIFY(status);

	const QString tooltip = status->toolTip();

	/* Said outright to be markup, once. Twice and a reader sees "&lt;b&gt;". */
	QCOMPARE(tooltip.count(QStringLiteral("<html>")), 1);
	QVERIFY2(!drawsMarkup(tooltip), qPrintable(tooltip));

	/* One line per source, in the order they are configured, and the address each was
	   asked for read back exactly as it was written. */
	const QStringList lines = asShown(tooltip).split(QLatin1Char('\n'));

	QCOMPARE(tooltip.count(QStringLiteral("<br>")), 1);
	QCOMPARE(lines.size(), 2);
	QCOMPARE(lines.at(0), QStringLiteral("%1 — not fetched yet").arg(doctored));
	QVERIFY2(lines.at(1).startsWith(plain), qPrintable(lines.at(1)));
}

void TestHubList::aVisitedRowIsPlainWhereItsTooltipIsMarkup()
{
	Qdc::RecentHubs recent(nullptr);
	Qdc::Testing::announce(recent, {visited(QStringLiteral("dchub://visited.example.org:411"),
	                                        QStringLiteral("<b>Best</b> Hub"))});

	Qdc::HubBrowser browser(nullptr, nullptr, &recent);

	QListWidget* list = browser.findChild<QListWidget*>(QStringLiteral("recentHubs"));
	QVERIFY(list);
	QCOMPARE(list->count(), 1);

	/*
	 * The label is drawn by a delegate as the plain text it is, so the name a hub chose
	 * is left spelled as it arrived. Escaping it here would put "&lt;b&gt;" in the row,
	 * which is the fault the wrapping of a tooltip exists to avoid.
	 */
	QVERIFY2(list->item(0)->text().contains(QStringLiteral("<b>Best</b> Hub")),
	         qPrintable(list->item(0)->text()));

	/* The tooltip is the other way about, being drawn in a label of Qt's making. */
	const QString tooltip = list->item(0)->toolTip();

	QCOMPARE(tooltip.count(QStringLiteral("<html>")), 1);
	QCOMPARE(asShown(tooltip), QStringLiteral("dchub://visited.example.org:411"));
}

void TestHubList::theHeaderDrawsAHubsDescriptionAsText()
{
	Qdc::HubHeader header;

	const QString url = QStringLiteral("adcs://hub.example.org:412");
	const QString description = QStringLiteral("films & <i>arguments</i> about them");

	header.setHubIdentity(QStringLiteral("<b>Best</b> Hub"), description, url);

	/*
	 * The hole itself, asserted first, so that what follows is a test of Qt's behaviour
	 * rather than of an assumption about it: a description of this shape is markup to the
	 * label a tooltip is drawn in, whatever the two lines of the header are set to.
	 */
	QVERIFY(Qt::mightBeRichText(description));

	QLabel* title = nullptr;
	QLabel* subtitle = nullptr;

	for (QLabel* line : header.findChildren<QLabel*>())
	{
		if (line->text() == header.title()) title = line;
		if (line->text() == header.subtitle()) subtitle = line;
	}

	QVERIFY(title);
	QVERIFY(subtitle);

	for (const QLabel* line : {title, subtitle})
	{
		QCOMPARE(line->toolTip().count(QStringLiteral("<html>")), 1);
		QVERIFY2(!drawsMarkup(line->toolTip()), qPrintable(line->toolTip()));
	}

	/* The address is still reachable from the title, and the description still reads as
	   the hub sent it. */
	QCOMPARE(asShown(title->toolTip()), url);
	QCOMPARE(asShown(subtitle->toolTip()), description);
}

void TestHubList::aFinishedDownloadShowsItsWholePathAsText()
{
	Qdc::Completed completed(nullptr);

	/* Long enough that a bound meant for a name would cut it, and with the leaf named
	   the way a stranger's share might name it. */
	Qdc::CompletedDownload entry;
	entry.filename = QStringLiteral("/Users/somebody/Downloads/")
		+ QString(200, QLatin1Char('m'))
		+ QStringLiteral("/films & <b>arguments</b>.iso");
	entry.size = 1024;
	entry.finished = QDateTime::fromSecsSinceEpoch(1769000000);

	Qdc::Testing::announce(completed, {entry});

	Qdc::HomeView home(nullptr, nullptr, nullptr, nullptr, &completed, nullptr);
	QCOMPARE(home.completedCount(), 1);

	QListWidget* list =
		home.findChild<QListWidget*>(QStringLiteral("recentDownloads"));

	QVERIFY(list);
	QCOMPARE(list->count(), 1);

	const QString tooltip = list->item(0)->toolTip();

	QCOMPARE(tooltip.count(QStringLiteral("<html>")), 1);
	QVERIFY2(!drawsMarkup(tooltip), qPrintable(tooltip));

	/* Whole, because a path is read in order to go and find the file with it: escaped
	   and not reduced, so nothing of it is cut and no ellipsis stands in for the rest. */
	QCOMPARE(asShown(tooltip), entry.filename);
}

QTEST_MAIN(TestHubList)

#include "tst_hublist.moc"

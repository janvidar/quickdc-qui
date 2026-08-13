/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/completed.h>
#include <core/configbridge.h>
#include <core/corethread.h>
#include <core/favourites.h>
#include <core/hubdirectory.h>
#include <core/hublistbridge.h>
#include <core/recenthubs.h>
#include <gui/homeview.h>
#include <gui/hubbrowser.h>
#include <gui/favouritedelegate.h>
#include <gui/hubheader.h>
#include <gui/hublistmodel.h>

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableView>
#include <QTextBlock>
#include <QTextDocument>
#include <QHeaderView>
#include <QTemporaryDir>
#include <QTest>

/**
 * Finding a hub: the public lists, the cache under them, and the two stores of
 * hubs somebody has already been to or kept.
 *
 * Nothing here reaches the network, and that is deliberate rather than incidental:
 * every source in these tests is either given no core to fetch with or has its list
 * put into the cache directly, which is what HubDirectory::cacheFileFor() is public
 * for. The one thing that cannot be tested without a server - a hublist coming off
 * a socket - is the core's own, and the core tests it.
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

	void recentHubsRecordsAVisitOnce();
	void recentHubsNormalisesWhatItIsGiven();
	void recentHubsNamesAHubWithoutRevisitingIt();
	void recentHubsDropsTheOldestPastItsLimit();
	void recentHubsRoundTripsThroughTheConfiguration();

	void aDirectoryWithNoConfigurationWritesTheDefaults();
	void aDirectoryReadsItsSourcesInOrder();
	void severalListsMergeIntoOne();
	void aCacheWrittenForAnotherSourceIsIgnored();

	void aBadAddressIsRefusedWithoutTheCore();
	void aFetchWithoutACoreSaysSo();

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

	/** Put a list where a source's cache goes, as if it had been fetched. */
	void cache(const Qdc::HubDirectory& directory, const QString& url,
	           const QVector<Qdc::HubListEntry>& hubs,
	           const QDateTime& when = QDateTime::currentDateTime());

	/** Name the sources in the configuration, so a test decides what is read. */
	void configureSources(const QStringList& urls);

	QString cacheDir() const;

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

QString TestHubList::cacheDir() const
{
	return QDir(m_dir.path()).filePath(QStringLiteral("hublists"));
}

void TestHubList::configureSources(const QStringList& urls)
{
	QList<QPair<QString, QString>> values;
	QStringList stale;

	/* More keys than any test writes, so a shorter list does not leave a tail
	   behind for the next one to read. */
	for (int n = 0; n < 8; ++n) stale.append(QStringLiteral("List %1").arg(n));

	for (int n = 0; n < urls.size(); ++n)
		values.append({QStringLiteral("List %1").arg(n), urls.at(n)});

	values.append({QStringLiteral("Total"), QString::number(urls.size())});

	m_config->replaceGroup(Qdc::ConfigStore::Application,
	                       QStringLiteral("Hublists"), stale, values);
	m_config->save(Qdc::ConfigStore::Application);
}

void TestHubList::cache(const Qdc::HubDirectory& directory, const QString& url,
                        const QVector<Qdc::HubListEntry>& hubs,
                        const QDateTime& when)
{
	QJsonArray listed;

	for (const Qdc::HubListEntry& hub : hubs)
	{
		QJsonObject object;
		object.insert(QStringLiteral("address"), hub.address);
		object.insert(QStringLiteral("name"), hub.name);
		object.insert(QStringLiteral("description"), hub.description);
		object.insert(QStringLiteral("country"), hub.country);
		object.insert(QStringLiteral("users"), static_cast<qint64>(hub.users));

		listed.append(object);
	}

	QJsonObject root;
	root.insert(QStringLiteral("url"), url);
	root.insert(QStringLiteral("fetched"), when.toSecsSinceEpoch());
	root.insert(QStringLiteral("hubs"), listed);

	QDir().mkpath(cacheDir());

	QSaveFile file(directory.cacheFileFor(url));
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
	QVERIFY(file.commit());
}

void TestHubList::recentHubsRecordsAVisitOnce()
{
	Qdc::RecentHubs recent(nullptr);
	QSignalSpy changes(&recent, &Qdc::RecentHubs::changed);

	recent.note(QStringLiteral("adcs://one.example.org:412"));
	recent.note(QStringLiteral("dchub://two.example.org:411"));

	QCOMPARE(recent.count(), 2);
	QCOMPARE(changes.count(), 2);

	/* Newest first, so the second one is at the front. */
	QCOMPARE(recent.entries().at(0).url,
	         QStringLiteral("dchub://two.example.org:411"));

	/* Visiting one again moves it rather than adding it. */
	recent.note(QStringLiteral("adcs://one.example.org:412"));

	QCOMPARE(recent.count(), 2);
	QCOMPARE(recent.entries().at(0).url,
	         QStringLiteral("adcs://one.example.org:412"));

	QVERIFY(recent.remove(QStringLiteral("adcs://one.example.org:412")));
	QVERIFY(!recent.remove(QStringLiteral("adcs://one.example.org:412")));
	QCOMPARE(recent.count(), 1);

	recent.clear();
	QCOMPARE(recent.count(), 0);
}

void TestHubList::recentHubsNormalisesWhatItIsGiven()
{
	Qdc::RecentHubs recent(nullptr);

	/*
	 * The same hub written three ways. A shortcut back to somewhere would be worth
	 * little if opening it by address and opening it from the list made two rows.
	 */
	recent.note(QStringLiteral("example.org"));
	recent.note(QStringLiteral("dchub://example.org"));
	recent.note(QStringLiteral("dchub://example.org:411"));

	QCOMPARE(recent.count(), 1);
	QCOMPARE(recent.entries().at(0).url, QStringLiteral("dchub://example.org:411"));

	QVERIFY(recent.contains(QStringLiteral("example.org")));
	QVERIFY(recent.remove(QStringLiteral("example.org")));
}

void TestHubList::recentHubsNamesAHubWithoutRevisitingIt()
{
	Qdc::RecentHubs recent(nullptr);

	recent.note(QStringLiteral("dchub://first.example.org:411"));
	recent.note(QStringLiteral("dchub://second.example.org:411"));

	/* The hub that was visited first says what it is called, which arrives long
	   after the connection was made and must not reorder anything. */
	QVERIFY(recent.setName(QStringLiteral("dchub://first.example.org:411"),
	                       QStringLiteral("The First Hub")));

	QCOMPARE(recent.entries().at(0).url,
	         QStringLiteral("dchub://second.example.org:411"));
	QCOMPARE(recent.entries().at(1).name, QStringLiteral("The First Hub"));

	QVERIFY(!recent.setName(QStringLiteral("dchub://absent.example.org:411"),
	                        QStringLiteral("Nowhere")));

	/* A visit with no name keeps the one that is known. */
	recent.note(QStringLiteral("dchub://first.example.org:411"));
	QCOMPARE(recent.entries().at(0).name, QStringLiteral("The First Hub"));
}

void TestHubList::recentHubsDropsTheOldestPastItsLimit()
{
	Qdc::RecentHubs recent(nullptr);

	const int limit = Qdc::RecentHubs::limit();
	QVERIFY(limit > 1);

	for (int n = 0; n < limit + 5; ++n)
		recent.note(QStringLiteral("dchub://hub%1.example.org:411").arg(n));

	QCOMPARE(recent.count(), limit);

	/* The newest survive, and the first few are what went. */
	QCOMPARE(recent.entries().at(0).url,
	         QStringLiteral("dchub://hub%1.example.org:411").arg(limit + 4));
	QVERIFY(!recent.contains(QStringLiteral("dchub://hub0.example.org:411")));
}

void TestHubList::recentHubsRoundTripsThroughTheConfiguration()
{
	{
		Qdc::RecentHubs recent(m_config);
		recent.clear();

		recent.note(QStringLiteral("dchub://stored.example.org:411"));
		recent.note(QStringLiteral("adcs://named.example.org:412"),
		            QStringLiteral("A | name with a separator"));
	}

	Qdc::RecentHubs reread(m_config);
	QCOMPARE(reread.count(), 2);

	const QList<Qdc::RecentHub> entries = reread.entries();

	QCOMPARE(entries.at(0).url, QStringLiteral("adcs://named.example.org:412"));

	/* The name is last on the line for exactly this reason. */
	QCOMPARE(entries.at(0).name, QStringLiteral("A | name with a separator"));
	QVERIFY(entries.at(0).visited.isValid());

	QCOMPARE(entries.at(1).url, QStringLiteral("dchub://stored.example.org:411"));

	reread.clear();
}

void TestHubList::aDirectoryWithNoConfigurationWritesTheDefaults()
{
	/*
	 * Before any other test here names its own sources, because there is one store
	 * and one core in a process - QuickDC::Core is a singleton, so a second one
	 * cannot be built to hold a store of its own.
	 */
	QVERIFY2(!m_config->contains(Qdc::ConfigStore::Application,
	                             QStringLiteral("Hublists"), QStringLiteral("Total")),
	         "this test has to run before the sources are configured");

	{
		/* No core, so nothing is fetched whatever the sources say. */
		Qdc::HubDirectory directory(nullptr, m_config, cacheDir());

		QStringList urls;
		for (const Qdc::HubListSource& source : directory.sources())
			urls.append(source.url);

		QCOMPARE(urls, Qdc::HubDirectory::defaultSources());
	}

	/* Written out, so that they can be found and edited. */
	QCOMPARE(m_config->getNumber(Qdc::ConfigStore::Application,
	                             QStringLiteral("Hublists"), QStringLiteral("Total")),
	         static_cast<int>(Qdc::HubDirectory::defaultSources().size()));

	/* And a store that says nought is respected rather than filled in again. */
	m_config->setNumber(Qdc::ConfigStore::Application, QStringLiteral("Hublists"),
	                    QStringLiteral("Total"), 0);
	m_config->save(Qdc::ConfigStore::Application);

	Qdc::HubDirectory empty(nullptr, m_config, cacheDir());
	QVERIFY(empty.sources().isEmpty());
	QCOMPARE(empty.hubCount(), 0);
}

void TestHubList::aDirectoryReadsItsSourcesInOrder()
{
	configureSources({QStringLiteral("http://second.example.org/hublist.xml"),
	                  QStringLiteral("http://first.example.org/hublist.xml")});

	Qdc::HubDirectory directory(nullptr, m_config, cacheDir());

	QCOMPARE(directory.sources().size(), 2);
	QCOMPARE(directory.sources().at(0).url,
	         QStringLiteral("http://second.example.org/hublist.xml"));

	/* Nothing has been fetched, and nothing pretends otherwise. */
	QVERIFY(!directory.lastUpdated().isValid());
	QVERIFY(!directory.isFetching());
	QCOMPARE(directory.hubCount(), 0);
}

void TestHubList::severalListsMergeIntoOne()
{
	const QString first = QStringLiteral("http://a.example.org/hublist.xml");
	const QString second = QStringLiteral("http://b.example.org/hublist.xml");

	configureSources({first, second});

	Qdc::HubListEntry alpha;
	alpha.name = QStringLiteral("Alpha");
	alpha.address = QStringLiteral("adcs://alpha.example.org:412");
	alpha.description = QStringLiteral("The first hub");
	alpha.users = 10;

	/* Named without a scheme or a port by the first list. */
	Qdc::HubListEntry beta;
	beta.name = QStringLiteral("Beta");
	beta.address = QStringLiteral("beta.example.org");
	beta.users = 5;

	/* And with both by the second, which also knows more about it. */
	Qdc::HubListEntry betaAgain;
	betaAgain.address = QStringLiteral("dchub://beta.example.org:411");
	betaAgain.description = QStringLiteral("The second hub");
	betaAgain.country = QStringLiteral("NO");
	betaAgain.users = 50;

	Qdc::HubListEntry gamma;
	gamma.name = QStringLiteral("Gamma");
	gamma.address = QStringLiteral("dchub://gamma.example.org:411");
	gamma.users = 1;

	{
		Qdc::HubDirectory seed(nullptr, m_config, cacheDir());
		cache(seed, first, {alpha, beta});
		cache(seed, second, {betaAgain, gamma});
	}

	Qdc::HubDirectory directory(nullptr, m_config, cacheDir());

	/* Three hubs, not four: the same hub spelled two ways is one row. */
	QCOMPARE(directory.hubCount(), 3);
	QVERIFY(directory.lastUpdated().isValid());
	QCOMPARE(directory.sources().at(0).hubs, 2);

	const QVector<Qdc::HubListEntry> hubs = directory.hubs();

	const Qdc::HubListEntry* merged = nullptr;
	for (const Qdc::HubListEntry& hub : hubs)
	{
		if (hub.address == QStringLiteral("beta.example.org")) merged = &hub;
	}

	QVERIFY2(merged, "the hub the first list named should be the one kept");

	/* The first list's description of it, filled in from the second where the
	   first said nothing, and the larger of the two user counts. */
	QCOMPARE(merged->name, QStringLiteral("Beta"));
	QCOMPARE(merged->description, QStringLiteral("The second hub"));
	QCOMPARE(merged->country, QStringLiteral("NO"));
	QCOMPARE(merged->users, 50u);
}

void TestHubList::aCacheWrittenForAnotherSourceIsIgnored()
{
	const QString url = QStringLiteral("http://mine.example.org/hublist.xml");
	configureSources({url});

	Qdc::HubListEntry hub;
	hub.name = QStringLiteral("Somebody else's");
	hub.address = QStringLiteral("dchub://elsewhere.example.org:411");

	Qdc::HubDirectory seed(nullptr, m_config, cacheDir());

	/* The file goes where this source's cache goes, but says it is another's -
	   which is what a stale file or a digest collision would look like. */
	QJsonObject root;
	root.insert(QStringLiteral("url"),
	            QStringLiteral("http://theirs.example.org/hublist.xml"));
	root.insert(QStringLiteral("fetched"),
	            QDateTime::currentDateTime().toSecsSinceEpoch());
	root.insert(QStringLiteral("hubs"), QJsonArray());

	QDir().mkpath(cacheDir());

	QSaveFile file(seed.cacheFileFor(url));
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
	QVERIFY(file.commit());

	Qdc::HubDirectory directory(nullptr, m_config, cacheDir());
	QCOMPARE(directory.hubCount(), 0);
	QVERIFY(!directory.sources().at(0).fetched.isValid());
}

void TestHubList::aBadAddressIsRefusedWithoutTheCore()
{
	Qdc::HubListBridge bridge(nullptr);

	QSignalSpy failures(&bridge, &Qdc::HubListBridge::failed);
	QVERIFY(failures.isValid());

	/* A hub address, not a hublist address; and a file, which is not fetched. */
	bridge.fetch(QStringLiteral("adcs://hub.example.org:412"));
	bridge.fetch(QStringLiteral("/home/somebody/hublist.xml"));

	/* Answered after the call returns, always, so that a caller asking for several
	   is not called back part way through its own loop. */
	QCOMPARE(failures.count(), 0);
	QVERIFY(QTest::qWaitFor([&failures] { return failures.count() >= 2; }, 2000));

	QVERIFY(failures.at(0).at(1).toString().contains(QStringLiteral("http")));
	QVERIFY(!bridge.isFetching());
}

void TestHubList::aFetchWithoutACoreSaysSo()
{
	Qdc::HubListBridge bridge(nullptr);

	QSignalSpy failures(&bridge, &Qdc::HubListBridge::failed);

	bridge.fetch(QStringLiteral("https://lists.example.org/hublist.xml.bz2"));

	QVERIFY(QTest::qWaitFor([&failures] { return failures.count() == 1; }, 2000));

	QCOMPARE(failures.at(0).at(0).toString(),
	         QStringLiteral("https://lists.example.org/hublist.xml.bz2"));
	QVERIFY(failures.at(0).at(1).toString().contains(QStringLiteral("core")));
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
	configureSources({url});

	Qdc::HubListEntry one;
	one.name = QStringLiteral("Films");
	one.address = QStringLiteral("dchub://films.example.org:411");
	one.users = 300;

	Qdc::HubListEntry two;
	two.name = QStringLiteral("Music");
	two.address = QStringLiteral("dchub://music.example.org:411");
	two.users = 20;

	Qdc::HubDirectory directory(nullptr, m_config, cacheDir());
	cache(directory, url, {one, two});

	Qdc::HubDirectory filled(nullptr, m_config, cacheDir());
	QCOMPARE(filled.hubCount(), 2);

	Qdc::Favourites favourites(nullptr);
	Qdc::RecentHubs recent(nullptr);
	recent.note(two.address, QStringLiteral("Music"));

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
	QVERIFY(favourites.add(QStringLiteral("dchub://early.example.org:411")));

	Qdc::HubBrowser browser(nullptr, &favourites, &recent);
	browser.show();
	QVERIFY(QTest::qWaitForWindowExposed(&browser));

	QCOMPARE(browser.currentTab(), Qdc::HubBrowser::PublicTab);

	QVERIFY(favourites.add(QStringLiteral("dchub://late.example.org:411")));
	QCOMPARE(browser.favouriteCount(), 2);
	QCOMPARE(browser.currentTab(), Qdc::HubBrowser::PublicTab);

	recent.note(QStringLiteral("dchub://later.example.org:411"));
	QCOMPARE(browser.recentCount(), 1);
	QCOMPARE(browser.currentTab(), Qdc::HubBrowser::PublicTab);

	/*
	 * And the same inside the arrangement the window actually builds: the browser
	 * nested in Home, Home a page of the view stack. That is where it went wrong -
	 * the tabs are a stacked layout inside another one, and the pages of the inner
	 * one are only hidden once it has been laid out.
	 */
	Qdc::Favourites kept(nullptr);
	QVERIFY(kept.add(QStringLiteral("adcs://kept.example.org:412")));

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
	configureSources({url});

	Qdc::HubListEntry hub;
	hub.name = QStringLiteral("Keepable");
	hub.address = QStringLiteral("adcs://keepable.example.org:412");
	hub.users = 12;

	Qdc::HubDirectory seed(nullptr, m_config, cacheDir());
	cache(seed, url, {hub});

	Qdc::HubDirectory directory(nullptr, m_config, cacheDir());
	Qdc::Favourites favourites(nullptr);

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
	QVERIFY(favourites.contains(hub.address));
	QCOMPARE(bookmark->text(), QStringLiteral("Unbookmark"));
	QCOMPARE(browser.favouriteCount(), 1);

	bookmark->click();
	QVERIFY(!favourites.contains(hub.address));
	QCOMPARE(bookmark->text(), QStringLiteral("Bookmark"));
	QCOMPARE(browser.favouriteCount(), 0);
}

void TestHubList::theBrowserForgetsARecentHub()
{
	Qdc::RecentHubs recent(nullptr);
	recent.note(QStringLiteral("dchub://kept.example.org:411"));
	recent.note(QStringLiteral("dchub://unwanted.example.org:411"));

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

	QCOMPARE(browser.recentCount(), 1);
	QVERIFY(!recent.contains(QStringLiteral("dchub://unwanted.example.org:411")));
	QVERIFY(recent.contains(QStringLiteral("dchub://kept.example.org:411")));

	QPushButton* clear =
		browser.findChild<QPushButton*>(QStringLiteral("clearRecentHubs"));
	QVERIFY(clear);
	clear->click();

	QCOMPARE(browser.recentCount(), 0);
	QCOMPARE(recent.count(), 0);
}

void TestHubList::theStarColumnKeepsAPublicHub()
{
	const QString url = QStringLiteral("http://star.example.org/hublist.xml");
	configureSources({url});

	Qdc::HubListEntry hub;
	hub.name = QStringLiteral("Starrable");

	/* Spelled without a port, as a hublist may. Kept under the one address the
	   store files a hub by, so the star on this row agrees with the one in a
	   hub's own header. */
	hub.address = QStringLiteral("adcs://starrable.example.org");
	/* 411 whatever the scheme; see urlNormalisationFillsInSchemeAndPort. */
	hub.users = 7;

	Qdc::HubDirectory seed(nullptr, m_config, cacheDir());
	cache(seed, url, {hub});

	Qdc::HubDirectory directory(nullptr, m_config, cacheDir());
	Qdc::Favourites favourites(nullptr);

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

	QVERIFY(favourites.contains(hub.address));
	QCOMPARE(favourites.urls().at(0),
	         QStringLiteral("adcs://starrable.example.org:411"));

	/* Lit without the list having been fetched again; see HubListModel. */
	QVERIFY(star.data(Qdc::HubListModel::FavouriteRole).toBool());
	QCOMPARE(browser.favouriteCount(), 1);

	QTest::mouseClick(table->viewport(), Qt::LeftButton, {},
	                  table->visualRect(star).center());

	QVERIFY(!favourites.contains(hub.address));
	QVERIFY(!star.data(Qdc::HubListModel::FavouriteRole).toBool());
}

void TestHubList::theStarColumnIsFixedAndStaysOutOfTheSort()
{
	const QString url = QStringLiteral("http://sorted.example.org/hublist.xml");
	configureSources({url});

	Qdc::HubListEntry small;
	small.name = QStringLiteral("Small");
	small.address = QStringLiteral("adc://small.example.org:412");
	small.users = 3;

	Qdc::HubListEntry large;
	large.name = QStringLiteral("Large");
	large.address = QStringLiteral("adc://large.example.org:412");
	large.users = 300;

	Qdc::HubDirectory seed(nullptr, m_config, cacheDir());
	cache(seed, url, {small, large});

	Qdc::HubDirectory directory(nullptr, m_config, cacheDir());
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
	recent.note(QStringLiteral("dchub://visited.example.org:411"),
	            QStringLiteral("Visited"));

	Qdc::Favourites favourites(nullptr);

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
	QVERIFY(favourites.add(QStringLiteral("dchub://visited.example.org:411")));

	QCOMPARE(list->count(), 1);
	QCOMPARE(list->currentRow(), 0);
	QVERIFY(list->item(0)->data(FavouriteRole).toBool());

	/* And the star itself takes it off again. */
	const QRect row = list->visualItemRect(list->item(0));
	QTest::mouseClick(list->viewport(), Qt::LeftButton, {},
	                  QPoint(row.left() + 10, row.center().y()));

	QVERIFY(!favourites.contains(QStringLiteral("dchub://visited.example.org:411")));

	/* A click on the words is a selection and nothing more. */
	QVERIFY(favourites.add(QStringLiteral("dchub://visited.example.org:411")));
	QTest::mouseClick(list->viewport(), Qt::LeftButton, {},
	                  QPoint(row.right() - 4, row.center().y()));

	QVERIFY(favourites.contains(QStringLiteral("dchub://visited.example.org:411")));
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

	configureSources({doctored, plain});

	Qdc::HubListEntry hub;
	hub.name = QStringLiteral("Listed");
	hub.address = QStringLiteral("adcs://listed.example.org:412");

	Qdc::HubDirectory seed(nullptr, m_config, cacheDir());
	cache(seed, plain, {hub});

	Qdc::HubDirectory directory(nullptr, m_config, cacheDir());
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
	recent.note(QStringLiteral("dchub://visited.example.org:411"),
	            QStringLiteral("<b>Best</b> Hub"));

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

	completed.add(entry);

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

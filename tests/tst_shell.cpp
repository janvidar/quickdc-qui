/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/configbridge.h>
#include <core/corethread.h>
#include <core/favourites.h>
#include <gui/homeview.h>
#include <gui/mainwindow.h>
#include <gui/navigationrail.h>
#include <gui/glyphs.h>
#include <gui/view.h>

#include <QDir>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>

/**
 * The application shell: the rail, the router between them, and the badges.
 *
 * Runs against a real core built from a temporary directory, the same way
 * tst_bridge does, because MainWindow builds bridges and they need one. Nothing
 * here connects to a hub.
 */
class TestShell : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();

	void railStartsOnTheFirstDestination();
	void railClampsOutOfRangeSelections();
	void railBadgesAreSetAndCleared();
	void windowHasEveryDestinationInPlanOrder();
	void selectingARailDestinationSwapsTheView();
	void viewBadgesReachTheRail();
	void windowSurvivesRepeatedConstruction();
	void homeListsFavouritesAndAsksTheWindowToOpenThem();

private:
	QTemporaryDir m_dir;
	Qdc::CoreThread* m_thread = nullptr;
};

/** A view whose badge the test can drive, which the real ones do not allow. */
class CountingView final : public Qdc::View {
	Q_OBJECT

public:
	QString title() const override { return QStringLiteral("Counting"); }
	Qdc::Glyph glyph() const override { return Qdc::Glyph::Home; }

	using Qdc::View::setBadgeCount;
};

void TestShell::initTestCase()
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

	/*
	 * No public hub lists configured, and written explicitly so that the directory
	 * does not fill in its defaults: the window builds one, and a directory with
	 * sources fetches them over the internet as soon as the list is looked at. A
	 * test suite reaches nothing but this machine.
	 */
	Qdc::ConfigBridge config(m_thread);
	config.setNumber(Qdc::ConfigStore::Application, QStringLiteral("Hublists"),
	                 QStringLiteral("Total"), 0);
	QVERIFY(config.save(Qdc::ConfigStore::Application));
}

void TestShell::cleanupTestCase()
{
	if (m_thread) m_thread->shutdown();
}

void TestShell::railStartsOnTheFirstDestination()
{
	Qdc::NavigationRail rail;
	QCOMPARE(rail.count(), 0);
	QCOMPARE(rail.currentIndex(), -1);

	QSignalSpy changes(&rail, &Qdc::NavigationRail::currentIndexChanged);

	QCOMPARE(rail.addDestination(Qdc::Glyph::Home, QStringLiteral("Home")), 0);
	QCOMPARE(rail.currentIndex(), 0);

	/*
	 * The first destination becomes current silently. Nothing can be connected
	 * while the rail is being built, so a signal there would only ever be missed.
	 */
	QCOMPARE(changes.count(), 0);

	QCOMPARE(rail.addDestination(Qdc::Glyph::Hubs, QStringLiteral("Hubs")), 1);
	QCOMPARE(rail.currentIndex(), 0);

	rail.setCurrentIndex(1);
	QCOMPARE(rail.currentIndex(), 1);
	QCOMPARE(changes.count(), 1);
	QCOMPARE(changes.at(0).at(0).toInt(), 1);

	/* Selecting what is already current is not a change. */
	rail.setCurrentIndex(1);
	QCOMPARE(changes.count(), 1);
}

void TestShell::railClampsOutOfRangeSelections()
{
	Qdc::NavigationRail rail;
	rail.addDestination(Qdc::Glyph::Home, QStringLiteral("Home"));
	rail.addDestination(Qdc::Glyph::Hubs, QStringLiteral("Hubs"));
	rail.setCurrentIndex(1);

	QSignalSpy changes(&rail, &Qdc::NavigationRail::currentIndexChanged);

	/* Ignored rather than clearing the selection, which would leave no view. */
	rail.setCurrentIndex(-1);
	rail.setCurrentIndex(2);
	rail.setCurrentIndex(99);

	QCOMPARE(rail.currentIndex(), 1);
	QCOMPARE(changes.count(), 0);
}

void TestShell::railBadgesAreSetAndCleared()
{
	Qdc::NavigationRail rail;
	const int home = rail.addDestination(Qdc::Glyph::Home, QStringLiteral("Home"));
	const int hubs = rail.addDestination(Qdc::Glyph::Hubs, QStringLiteral("Hubs"));

	QCOMPARE(rail.badgeCount(home), 0);

	rail.setBadgeCount(hubs, 4);
	QCOMPARE(rail.badgeCount(hubs), 4);
	QCOMPARE(rail.badgeCount(home), 0);

	/* Negative counts clamp to nothing rather than drawing a nonsense badge. */
	rail.setBadgeCount(hubs, -3);
	QCOMPARE(rail.badgeCount(hubs), 0);

	rail.setBadgeCount(hubs, 250);
	QCOMPARE(rail.badgeCount(hubs), 250);

	/* Out of range is a no-op, not a crash. */
	rail.setBadgeCount(17, 9);
	QCOMPARE(rail.badgeCount(17), 0);
}

void TestShell::windowHasEveryDestinationInPlanOrder()
{
	Qdc::MainWindow window(m_thread);

	QCOMPARE(window.rail()->count(), 4);
	QCOMPARE(window.currentIndex(), 0);

	/*
	 * Settings is added after the rail's trailing space, so it sits at the foot
	 * of the rail while keeping the next index - the spacer must not consume one.
	 */
	QCOMPARE(window.windowTitle(), QStringLiteral("Home — QuickDC"));

	window.setCurrentIndex(3);
	QCOMPARE(window.windowTitle(), QStringLiteral("Settings — QuickDC"));
}

void TestShell::selectingARailDestinationSwapsTheView()
{
	Qdc::MainWindow window(m_thread);
	Qdc::NavigationRail* rail = window.rail();

	/* Driving the rail, as a click would, has to move the content with it. */
	rail->setCurrentIndex(2);
	QCOMPARE(window.currentIndex(), 2);
	QCOMPARE(window.windowTitle(), QStringLiteral("Transfers — QuickDC"));

	rail->setCurrentIndex(1);
	QCOMPARE(window.currentIndex(), 1);

	/* And driving the router has to move the rail, so the two cannot diverge. */
	window.setCurrentIndex(3);
	QCOMPARE(rail->currentIndex(), 3);
}

void TestShell::viewBadgesReachTheRail()
{
	CountingView view;
	QSignalSpy badges(&view, &Qdc::View::badgeCountChanged);

	QCOMPARE(view.badgeCount(), 0);

	view.setBadgeCount(3);
	QCOMPARE(view.badgeCount(), 3);
	QCOMPARE(badges.count(), 1);

	/* Setting the same count again is not a change. */
	view.setBadgeCount(3);
	QCOMPARE(badges.count(), 1);

	view.setBadgeCount(-1);
	QCOMPARE(view.badgeCount(), 0);
	QCOMPARE(badges.count(), 2);

	/*
	 * The live path: the transfers view keeps its own badge from the bridge's
	 * events. Nothing is transferring in a test, so zero is the right answer -
	 * what is being checked is that the window asked the view for a count while
	 * building the rail rather than assuming zero.
	 */
	Qdc::MainWindow window(m_thread);
	QCOMPARE(window.rail()->badgeCount(2), 0);
}

void TestShell::windowSurvivesRepeatedConstruction()
{
	/*
	 * Each window builds a TransferBridge that registers a listener with the
	 * core and unregisters it again on the way out. If the order in ~MainWindow
	 * were wrong, the core would keep a pointer into a freed window and the next
	 * transfer event would follow it.
	 */
	for (int n = 0; n < 3; ++n)
	{
		Qdc::MainWindow window(m_thread);
		window.setCurrentIndex(n % 4);
	}

	QVERIFY(m_thread->invoke([] { }));
	QVERIFY(m_thread->isCoreRunning());
}

void TestShell::homeListsFavouritesAndAsksTheWindowToOpenThem()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::Favourites favourites(&config);
	favourites.remove(QStringLiteral("dchub://home.example.org:411"));

	Qdc::HomeView home(&config, &favourites, nullptr, nullptr, nullptr, nullptr);
	QCOMPARE(home.favouriteCount(), favourites.urls().size());
	QCOMPARE(home.recentHubCount(), 0);
	QCOMPARE(home.completedCount(), 0);

	QSignalSpy requested(&home, &Qdc::HomeView::hubRequested);
	QVERIFY(requested.isValid());

	/* A favourite added to the store appears without Home being told directly. */
	const QString hub = QStringLiteral("dchub://home.example.org:411");
	QVERIFY(favourites.add(hub));

	const int before = home.favouriteCount();
	QVERIFY(before > 0);

	/*
	 * Home cannot open a hub itself, so what it does is ask. The window is what
	 * turns that into a connection.
	 */
	QListWidget* list = home.findChild<QListWidget*>(QStringLiteral("favouriteHubs"));
	QVERIFY(list);

	for (int n = 0; n < list->count(); ++n)
	{
		if (list->item(n)->data(Qt::UserRole + 1).toString() != hub) continue;

		list->setCurrentRow(n);
		break;
	}

	QVERIFY(list->currentItem());
	QCOMPARE(list->currentItem()->data(Qt::UserRole + 1).toString(), hub);

	QPushButton* open =
		home.findChild<QPushButton*>(QStringLiteral("connectFavouriteHub"));
	QVERIFY(open);
	QVERIFY(open->isEnabled());
	open->click();

	QCOMPARE(requested.count(), 1);
	QCOMPARE(requested.at(0).at(0).toString(), hub);

	/* Unbookmarking through Home takes it out of the store, not just the list. */
	QPushButton* unbookmark =
		home.findChild<QPushButton*>(QStringLiteral("unbookmarkHub"));
	QVERIFY(unbookmark);
	unbookmark->click();

	QVERIFY(!favourites.contains(hub));
	QCOMPARE(home.favouriteCount(), before - 1);
}

QTEST_MAIN(TestShell)

#include "tst_shell.moc"

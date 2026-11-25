/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/boardbridge.h>
#include <core/configbridge.h>
#include <core/corechannel.h>
#include <core/corethread.h>
#include <core/filelistbridge.h>
#include <core/hubbridge.h>
#include <core/hubdirectory.h>
#include <core/networkwatch.h>
#include <core/mediabridge.h>
#include <core/searchbridge.h>
#include <core/transferbridge.h>

/*
 * The core's own headers, which nothing above this layer may reach for - and this
 * is the test of the layer itself, whose job includes checking that the two sides
 * agree about things neither can see the other decide.
 */
#include <api/bulletins.h>
#include <api/core.h>
#include <api/hub.h>
#include <network/hubmanager.h>


#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

/**
 * The isolation layer, end to end against a real core.
 *
 * Nothing here connects to a hub. A hub bridge with no session still reaches the
 * core, is told off by it, and the complaint comes back as a signal on this
 * thread - which is the whole path this layer exists to provide, and it can be
 * checked without a network.
 *
 * The core is built from files in a temporary directory rather than ~/.quickdc,
 * so a test run leaves the developer's own configuration alone.
 */
class TestBridge : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();

	void coreIsOnlyReachableFromItsOwnThread();
	void aHubsEventsReachOnlyThatHubsRoute();
	void jobsRunInOrderOnTheCoreThread();
	void aBlockingCallDoesNotWaitForThePollInterval();
	void snapshotsDoNotWaitForABusyCore();
	void anOfferedFileIsRecognisedAfterARestart();
	void configRoundTripsThroughTheStore();
	void aGroupIsReadInOneTrip();
	void configWritesTheFileItWasGiven();
	void aSettingTheCoreWritesReachesTheViews();
	void aWriteThroughOneBridgeIsSeenThroughAnother();
	void hubBridgeReportsErrorsOnTheCallingThread();
	void transfersAndQueueStartEmpty();
	void bridgesSurviveBeingDestroyedWhileRunning();
	void nothingAViewAsksForWaitsForABusyCore();
	void bridgesAreBuiltAndTornDownWithoutWaiting();
	void theCoreKnowsAHubByTheNameTheViewsFileItUnder();

	void entriesAHubSendsReachTheViewReadWhereTheyWereRead();
	void whatTheCoreSaysOfAPostIsWhatIsDrawn();
	void aPostWithNoSessionToGoOnSaysWhy();

	void anAddressChangeIsWhatCountsAsTheNetworkChanging();
	void addressesTheMachineAlwaysHasAreNotAChange();
	void theOrderInterfacesComeBackInIsNotAChange();
	void aWatchSaysNothingAboutANetworkThatHasNotChanged();

private:
	QString path(const QString& name) const;

	/**
	 * The core side listener of a hub bridge.
	 *
	 * Nothing here connects to a hub, so the events a session would deliver are
	 * delivered by hand instead - and by this route rather than through a back door
	 * in the bridge, because the core side half implements both of the core's hub
	 * interfaces on one object: the hub manager knows it as a Hub, and the same
	 * object is the HubListener the session reports through. Handing it an event on
	 * the core thread is therefore exactly what the session does.
	 */
	QuickDC::HubListener* listenerFor(const Qdc::HubBridge& hub);

	QTemporaryDir m_dir;
	Qdc::CoreThread* m_thread = nullptr;
};

namespace {

/** A tiger root in base32, which is how long a post's name is. */
const QString ENTRY_TTH =
	QStringLiteral("L6LKZG4CUXDSCJQHBLL5GFG6FWAPSLKK2S3VIFA");
const QString AUTHOR_CID =
	QStringLiteral("IPJJWEPPPLCA3PF2ZCRRYO4F2ZX2EV2JMW2KC3I");

QuickDC::BoardEntry core_entry(const QString& tth, const QString& board, quint64 timestamp)
{
	QuickDC::BoardEntry entry;

	entry.tth = tth.toStdString();
	entry.size = 412;
	entry.board = board.toStdString();
	entry.cid = AUTHOR_CID.toStdString();
	entry.thread = tth.toStdString();
	entry.timestamp = timestamp;

	return entry;
}

}

QString TestBridge::path(const QString& name) const
{
	return QDir(m_dir.path()).filePath(name);
}

QuickDC::HubListener* TestBridge::listenerFor(const Qdc::HubBridge& hub)
{
	const QByteArray url = Qdc::HubBridge::settingsUrl(hub.url()).toUtf8();

	QuickDC::HubListener* listener = nullptr;

	/* Ordered behind the job that builds the session, so this finds one. */
	if (!Qdc::Testing::runOnCore(m_thread, [&] {
		QuickDC::Core* core = Qdc::Testing::core(m_thread);
		if (!core || !core->hubs) return;

		listener = dynamic_cast<QuickDC::HubListener*>(
			core->hubs->getHubByUrl(url.constData()));
	}))
	{
		return nullptr;
	}

	return listener;
}

void TestBridge::initTestCase()
{
	QVERIFY2(m_dir.isValid(), "could not create a temporary directory");

	m_thread = new Qdc::CoreThread(this);
	m_thread->setPreferenceFiles(path(QStringLiteral("quickdc.conf")),
	                             path(QStringLiteral("session.conf")),
	                             path(QStringLiteral("queue.xml")));

	QVERIFY2(m_thread->startAndWait(),
	         qPrintable(QStringLiteral("the core did not start: ")
	                    + m_thread->failureReason()));
	QVERIFY(m_thread->isCoreRunning());
}

void TestBridge::cleanupTestCase()
{
	if (!m_thread) return;

	m_thread->shutdown();
	QVERIFY(!m_thread->isCoreRunning());
}

void TestBridge::coreIsOnlyReachableFromItsOwnThread()
{
	/* The guard that stops a caller reaching the reactor from the wrong side. */
	QVERIFY(Qdc::Testing::core(m_thread) == nullptr);

	bool visible = false;
	QThread* ran_on = nullptr;

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [&] {
		visible = Qdc::Testing::core(m_thread) != nullptr;
		ran_on = QThread::currentThread();
	}));

	QVERIFY(visible);
	QCOMPARE(ran_on, static_cast<QThread*>(m_thread));
	QVERIFY(ran_on != QThread::currentThread());
}

void TestBridge::jobsRunInOrderOnTheCoreThread()
{
	QVector<int> order;

	for (int n = 0; n < 8; ++n)
		Qdc::Testing::post(m_thread, [&order, n] { order.append(n); });

	/* Ordered behind all eight, so its completion means they have all run. */
	QVERIFY(Qdc::Testing::runOnCore(m_thread, [] { }));

	QCOMPARE(order.size(), 8);
	for (int n = 0; n < 8; ++n)
		QCOMPARE(order.at(n), n);
}

void TestBridge::aBlockingCallDoesNotWaitForThePollInterval()
{
	/*
	 * The reactor blocks in its poll for a quarter of a second at a time. A job posted
	 * from another thread is supposed to wake it, so a blocking call costs no more than
	 * the job itself. Nothing drawn on screen makes one any more, but a bridge being set
	 * up does, and a start-up that waited out a poll interval per bridge would be felt.
	 */
	QElapsedTimer timer;
	timer.start();

	const int rounds = 10;

	for (int n = 0; n < rounds; ++n)
	{
		int answered = 0;
		QVERIFY(Qdc::Testing::runOnCore(m_thread, [&answered] { ++answered; }));
		QCOMPARE(answered, 1);
	}

	const qint64 each = timer.elapsed() / rounds;

	qWarning() << "a blocking call took" << each << "ms on average over" << rounds
	           << "calls";

	QVERIFY2(each < 50,
	         qPrintable(QStringLiteral("a blocking call averaged %1 ms, which is the "
	                                   "reactor's poll interval rather than the job's "
	                                   "own cost").arg(each)));
}

void TestBridge::snapshotsDoNotWaitForABusyCore()
{
	Qdc::TransferBridge transfers(m_thread, this);

	/*
	 * The core thread is held for a good while, as it would be by a transfer moving
	 * chunks or a finished hash being collected. Everything a view asks of the bridge
	 * has to answer anyway: a view that waited here stopped drawing for as long as the
	 * core was busy, which is what made the window appear to hang.
	 */
	Qdc::Testing::post(m_thread, [] { QThread::msleep(1500); });

	QElapsedTimer timer;
	timer.start();

	const QVector<Qdc::TransferInfo> active = transfers.activeTransfers();
	const QVector<Qdc::QueueEntry> queued = transfers.queueSnapshot();
	const quint32 count = transfers.activeTransferCount();
	transfers.refresh();

	const qint64 spent = timer.elapsed();

	QVERIFY2(spent < 100,
	         qPrintable(QStringLiteral("asking the bridge took %1 ms while the core was "
	                                   "busy").arg(spent)));

	/* Whatever it reported, it reported without waiting. */
	QVERIFY(active.isEmpty());
	QVERIFY(queued.isEmpty());
	QCOMPARE(count, 0u);

	/* And the figures do arrive, once the core is free again. */
	QSignalSpy fresh(&transfers, &Qdc::TransferBridge::snapshotChanged);
	QVERIFY(fresh.wait(10000));
}

void TestBridge::anOfferedFileIsRecognisedAfterARestart()
{
	const QString tth = QStringLiteral("L6LKZG4CUXDSCJQHBLL5GFG6FWAPSLKK2S3VIFA");
	const QString path = QStringLiteral("/Users/somebody/holiday photo.jpg");

	Qdc::ConfigBridge config(m_thread);

	/* Written the way the core writes it when a file is attached. */
	config.setNumber(Qdc::ConfigStore::Application, QStringLiteral("Attachments"),
	                 QStringLiteral("Total"), 1);
	config.setString(Qdc::ConfigStore::Application, QStringLiteral("Attachments"),
	                 QStringLiteral("File 0"), tth + QLatin1Char(' ') + path);

	Qdc::MediaBridge media(m_thread, &config);

	/*
	 * Known without anything having been attached in this session. Otherwise a picture
	 * sent yesterday is not recognised as ours: it would be drawn as a file to fetch,
	 * and fetching would ask the hub for a file this client is the only source of.
	 *
	 * The path is kept whole: a name may contain spaces, so the entry is parsed by
	 * position rather than split.
	 */
	QCOMPARE(media.pathFor(tth), path);

	QVERIFY(media.pathFor(QStringLiteral("F6ED42THAKQCOCNHJOUCMOEOX4GOCT25D5WM7SY"))
	        .isEmpty());
	QVERIFY(media.pathFor(QString()).isEmpty());

	/* A bridge given no configuration simply knows nothing yet. */
	Qdc::MediaBridge blind(m_thread);
	QVERIFY(blind.pathFor(tth).isEmpty());

	config.remove(Qdc::ConfigStore::Application, QStringLiteral("Attachments"),
	              QStringLiteral("File 0"));
	config.setNumber(Qdc::ConfigStore::Application, QStringLiteral("Attachments"),
	                 QStringLiteral("Total"), 0);
}

void TestBridge::configRoundTripsThroughTheStore()
{
	Qdc::ConfigBridge config(m_thread, this);
	const Qdc::ConfigStore store = Qdc::ConfigStore::Application;
	const QString group = QStringLiteral("User");

	QVERIFY(!config.contains(store, group, QStringLiteral("Nickname")));
	QCOMPARE(config.getString(store, group, QStringLiteral("Nickname"),
	                          QStringLiteral("fallback")),
	         QStringLiteral("fallback"));

	config.setString(store, group, QStringLiteral("Nickname"),
	                 QStringLiteral("tester"));
	config.setNumber(store, QStringLiteral("Network"), QStringLiteral("Slots"), 7);
	config.setBool(store, QStringLiteral("Network"), QStringLiteral("Active mode"), true);

	QVERIFY(config.contains(store, group, QStringLiteral("Nickname")));
	QCOMPARE(config.getString(store, group, QStringLiteral("Nickname")),
	         QStringLiteral("tester"));

	/*
	 * The interesting one. Preferences keeps a single current group, so a read
	 * that did not select its own group would answer out of whichever section was
	 * touched last - here [Network], set two lines above.
	 */
	QCOMPARE(config.getNumber(store, QStringLiteral("Network"),
	                          QStringLiteral("Slots")), 7);
	QCOMPARE(config.getBool(store, QStringLiteral("Network"),
	                        QStringLiteral("Active mode")), true);
	QCOMPARE(config.getString(store, group, QStringLiteral("Nickname")),
	         QStringLiteral("tester"));

	config.remove(store, group, QStringLiteral("Nickname"));
	QVERIFY(!config.contains(store, group, QStringLiteral("Nickname")));
}

void TestBridge::aGroupIsReadInOneTrip()
{
	Qdc::ConfigBridge config(m_thread);

	const QString group = QStringLiteral("Batch");

	config.setString(Qdc::ConfigStore::Application, group,
	                 QStringLiteral("Nickname"), QStringLiteral("somebody"));
	config.setNumber(Qdc::ConfigStore::Application, group, QStringLiteral("Port"), 1511);
	config.setBool(Qdc::ConfigStore::Application, group,
	               QStringLiteral("Active mode"), true);

	/*
	 * Each key's default decides how it is read, because the store keeps everything as
	 * text and cannot say what a value was meant to be.
	 */
	const QVariantHash read = config.readGroup(Qdc::ConfigStore::Application, group, {
		{QStringLiteral("Nickname"), QString()},
		{QStringLiteral("Port"), 411},
		{QStringLiteral("Active mode"), false},
		{QStringLiteral("Absent"), QStringLiteral("fallback")},
		{QStringLiteral("Absent number"), 7}
	});

	QCOMPARE(read.value(QStringLiteral("Nickname")).toString(),
	         QStringLiteral("somebody"));
	QCOMPARE(read.value(QStringLiteral("Port")).toInt(), 1511);
	QCOMPARE(read.value(QStringLiteral("Active mode")).toBool(), true);

	/* A key that is not there answers with its default. */
	QCOMPARE(read.value(QStringLiteral("Absent")).toString(),
	         QStringLiteral("fallback"));
	QCOMPARE(read.value(QStringLiteral("Absent number")).toInt(), 7);

	/*
	 * And the default was not written back. Reading is only looking: a key nobody has
	 * ever set stays absent however often it is asked after, which is what lets a
	 * caller tell "never decided" from "decided, and the answer was the default".
	 */
	QVERIFY(!config.contains(Qdc::ConfigStore::Application, group,
	                         QStringLiteral("Absent")));

	/* Nothing asked for, nothing read. */
	QVERIFY(config.readGroup(Qdc::ConfigStore::Application, group, {}).isEmpty());

	for (const QString& key : {QStringLiteral("Nickname"), QStringLiteral("Port"),
	                           QStringLiteral("Active mode"), QStringLiteral("Absent"),
	                           QStringLiteral("Absent number")})
	{
		config.remove(Qdc::ConfigStore::Application, group, key);
	}
}

void TestBridge::configWritesTheFileItWasGiven()
{
	Qdc::ConfigBridge config(m_thread, this);

	config.setString(Qdc::ConfigStore::Application, QStringLiteral("User"),
	                 QStringLiteral("Description"), QStringLiteral("a test"));

	/* Written a moment later rather than before save() returns, so this waits. */
	config.save(Qdc::ConfigStore::Application);
	QTRY_VERIFY(QFile::exists(path(QStringLiteral("quickdc.conf"))));

	/* Not the other store: save() takes one, and must not write both. */
	QVERIFY(!QFile::exists(path(QStringLiteral("session.conf"))));

}

/**
 * A setting the core changes by itself - an identity stored, a mode settled - used
 * to reach no view at all: each bridge read the store once and then only ever
 * learnt what it wrote. Now the change arrives as an event, the shared copy takes
 * it in, and the bridge says so.
 */
void TestBridge::aSettingTheCoreWritesReachesTheViews()
{
	Qdc::ConfigBridge config(m_thread);
	QSignalSpy changed(&config, &Qdc::ConfigBridge::changed);

	const QString group = QStringLiteral("Written by the core");

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [&] {
		Qdc::Testing::core(m_thread)->config->setString(group.toStdString(), "Value", "from the core");
	}));

	QTRY_COMPARE(config.getString(Qdc::ConfigStore::Application, group, QStringLiteral("Value")),
	             QStringLiteral("from the core"));
	QTRY_VERIFY(!changed.isEmpty());
	QCOMPARE(changed.last().at(1).toString(), group);
	QCOMPARE(changed.last().at(2).toString(), QStringLiteral("Value"));
}

/* Two bridges read one copy, so neither has to wait for the core to see the other's write. */
void TestBridge::aWriteThroughOneBridgeIsSeenThroughAnother()
{
	Qdc::ConfigBridge writer(m_thread);
	Qdc::ConfigBridge reader(m_thread);

	const QString group = QStringLiteral("Shared");
	writer.setString(Qdc::ConfigStore::Application, group, QStringLiteral("Key"),
	                 QStringLiteral("now"));

	QCOMPARE(reader.getString(Qdc::ConfigStore::Application, group, QStringLiteral("Key")),
	         QStringLiteral("now"));

	/* And an older value the core reports on its way through does not undo it. */
	writer.setString(Qdc::ConfigStore::Application, group, QStringLiteral("Key"),
	                 QStringLiteral("later"));
	QVERIFY(Qdc::Testing::runOnCore(m_thread, [] { }));
	QTest::qWait(50);
	QCOMPARE(reader.getString(Qdc::ConfigStore::Application, group, QStringLiteral("Key")),
	         QStringLiteral("later"));
}

void TestBridge::hubBridgeReportsErrorsOnTheCallingThread()
{
	Qdc::HubBridge hub(m_thread, QStringLiteral("dchub://192.0.2.1:411"), this);

	QCOMPARE(hub.url(), QStringLiteral("dchub://192.0.2.1:411"));
	QCOMPARE(hub.netStatus(), Qdc::NetStatus::Invalid);
	QVERIFY(!hub.followRedirects());
	QVERIFY(hub.allowPeerConnections());

	QSignalSpy errors(&hub, &Qdc::HubBridge::systemError);
	QVERIFY(errors.isValid());

	/*
	 * Nothing has connected, so the core has no session to send this through and
	 * says so. The complaint is raised on the core thread and has to arrive here
	 * instead - if the marshalling were wrong this would either never arrive or
	 * arrive on the wrong thread.
	 */
	hub.sendChat(QStringLiteral("hello"));

	QVERIFY(errors.wait(5000));
	QCOMPARE(errors.count(), 1);
	QVERIFY(errors.at(0).at(0).toString().contains(QStringLiteral("No session")));

	/* A scheme-less address is completed rather than rejected. */
	Qdc::HubBridge shorthand(m_thread, QStringLiteral("example.org"), this);
	QCOMPARE(shorthand.url(), QStringLiteral("dchub://example.org:411"));
}

void TestBridge::transfersAndQueueStartEmpty()
{
	Qdc::TransferBridge transfers(m_thread, this);

	QVERIFY(transfers.activeTransfers().isEmpty());
	QVERIFY(transfers.queueSnapshot().isEmpty());
	QCOMPARE(transfers.activeTransferCount(), 0u);
}

void TestBridge::bridgesSurviveBeingDestroyedWhileRunning()
{
	/*
	 * Both bridges leave something behind on the core thread - a hub registered
	 * with the hub manager, a listener registered with the transfer manager - and
	 * both destructors block until it is gone. If either did not, the core would
	 * hold a pointer to freed memory and the next event would take the process
	 * with it.
	 */
	for (int n = 0; n < 4; ++n)
	{
		Qdc::HubBridge hub(m_thread, QStringLiteral("adc://192.0.2.1:412"));
		Qdc::TransferBridge transfers(m_thread);

		hub.connectToHub();
		hub.disconnectFromHub();
		QVERIFY(transfers.activeTransfers().isEmpty());
	}

	/* Still answering, so nothing above left the reactor wedged. */
	QVERIFY(Qdc::Testing::runOnCore(m_thread, [] { }));
	QVERIFY(m_thread->isCoreRunning());
}

/**
 * The rule this whole layer exists for, checked against every bridge at once.
 *
 * The core thread is held for a second and a half, as it is by a transfer writing
 * chunks, a share's file list being compressed, or somebody else's search being
 * answered out of the whole index. Everything a view can ask for has to answer
 * anyway. Anything that waits here is a frozen window for exactly as long.
 */
void TestBridge::nothingAViewAsksForWaitsForABusyCore()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::TransferBridge transfers(m_thread);
	Qdc::FilelistBridge filelists(m_thread);
	Qdc::SearchBridge search(m_thread);
	Qdc::MediaBridge media(m_thread, &config);
	Qdc::HubBridge hub(m_thread, QStringLiteral("adc://192.0.2.1:412"));
	Qdc::BoardBridge posts(m_thread);

	const QString group = QStringLiteral("Busy");

	config.setString(Qdc::ConfigStore::Application, group,
	                 QStringLiteral("Nickname"), QStringLiteral("somebody"));

	Qdc::Testing::post(m_thread, [] { QThread::msleep(1500); });

	QElapsedTimer timer;
	timer.start();

	/* Reading the configuration, which is what a settings page opening does. */
	QCOMPARE(config.getString(Qdc::ConfigStore::Application, group,
	                          QStringLiteral("Nickname")),
	         QStringLiteral("somebody"));

	config.getNumber(Qdc::ConfigStore::Application, group, QStringLiteral("Port"), 411);
	config.getBool(Qdc::ConfigStore::Application, group, QStringLiteral("Active"), true);
	config.contains(Qdc::ConfigStore::Application, group, QStringLiteral("Nickname"));

	config.readGroup(Qdc::ConfigStore::Application, group, {
		{QStringLiteral("Nickname"), QString()},
		{QStringLiteral("Port"), 411}
	});

	/* Writing it, and asking for it to be saved. */
	config.setBool(Qdc::ConfigStore::Application, group, QStringLiteral("Active"), false);
	config.remove(Qdc::ConfigStore::Application, group, QStringLiteral("Port"));
	config.save(Qdc::ConfigStore::Application);

	/* The transfer figures a list draws from. */
	transfers.activeTransfers();
	transfers.queueSnapshot();
	transfers.activeTransferCount();
	transfers.refresh();

	/* And everything else a view reaches for while it is on screen. */
	filelists.pendingCount();
	filelists.request(QStringLiteral("adc://192.0.2.1:412"), QStringLiteral("ABCDEF"));
	search.start(Qdc::SearchQuery{QStringLiteral("anything"), {}, {}, 0});
	media.pathFor(QStringLiteral("L6LKZG4CUXDSCJQHBLL5GFG6FWAPSLKK2S3VIFA"));
	media.attach(QStringLiteral("/nonexistent/file.png"));
	hub.url();
	hub.netStatus();
	hub.hubStatus();
	hub.connectToHub();
	hub.sendChat(QStringLiteral("hello"), false);
	hub.disconnectFromHub();

	/* Everything a board pane reaches for. */
	hub.subscribeBoard(QStringLiteral("general"));
	hub.requestBoardEntry(QStringLiteral("general"), ENTRY_TTH);
	hub.withdrawPost(ENTRY_TTH, QStringLiteral("general"));
	hub.unsubscribeBoard(QStringLiteral("general"));

	posts.postState(ENTRY_TTH);
	posts.attribution(ENTRY_TTH);
	posts.unread(QStringLiteral("adc://192.0.2.1:412"), QStringLiteral("general"));
	posts.loadEntries(QStringLiteral("adc://192.0.2.1:412"), QStringLiteral("general"));
	posts.read(QStringLiteral("adc://192.0.2.1:412"), QStringLiteral("general"), ENTRY_TTH, true);
	posts.markAllRead(QStringLiteral("adc://192.0.2.1:412"), QStringLiteral("general"));
	posts.setPolicy(QStringLiteral("adc://192.0.2.1:412"), Qdc::BoardBridge::Policy::Serve, true);
	posts.write(&hub, QStringLiteral("general"), QString(), QStringLiteral("A subject"),
	            QStringLiteral("A body"), false);

	const qint64 spent = timer.elapsed();

	QVERIFY2(spent < 100,
	         qPrintable(QStringLiteral("asking the bridges took %1 ms while the core "
	                                   "was busy").arg(spent)));

	config.remove(Qdc::ConfigStore::Application, group, QStringLiteral("Nickname"));
	config.remove(Qdc::ConfigStore::Application, group, QStringLiteral("Active"));
}

/**
 * Building a bridge registers a listener with the core, and taking one down
 * unregisters it. Both used to wait for the reactor, which made opening a hub
 * while a transfer was moving hang the window - and a window with several bridges
 * paid it once each before it was even shown.
 */
void TestBridge::bridgesAreBuiltAndTornDownWithoutWaiting()
{
	/*
	 * Built before the core is made busy, because filling its copy of both
	 * preference files is the one call in this layer that still waits - once, at
	 * start-up, before there is a window to freeze. Everything else is measured
	 * against a core that is not answering.
	 */
	Qdc::ConfigBridge config(m_thread);

	Qdc::Testing::post(m_thread, [] { QThread::msleep(1500); });

	QElapsedTimer timer;
	timer.start();

	{
		Qdc::TransferBridge transfers(m_thread);
		Qdc::MediaBridge media(m_thread, &config);
		Qdc::FilelistBridge filelists(m_thread);
		Qdc::HubDirectory directory(m_thread);
		Qdc::HubBridge hub(m_thread, QStringLiteral("adc://192.0.2.2:412"));
	}

	const qint64 spent = timer.elapsed();

	QVERIFY2(spent < 100,
	         qPrintable(QStringLiteral("building and destroying the bridges took %1 ms "
	                                   "while the core was busy").arg(spent)));

	/* The jobs they left behind still run, and leave the reactor answering. */
	QVERIFY(Qdc::Testing::runOnCore(m_thread, [] { }));
	QVERIFY(m_thread->isCoreRunning());
}

/**
 * The name a hub is known by has to be the same on both sides.
 *
 * The core builds a canonical "{scheme}://{host}:{port}" for every hub and files
 * everything about it - the remembered certificate, the nickname, the password -
 * under that. The views work one out for themselves, from the address somebody
 * typed, and read and write the same settings through it.
 *
 * Nothing forces the two to agree, and if they ever stop the symptom is not an
 * error: a nickname set for a hub is simply never the one the hub is told,
 * because each side is using a section the other does not write. So this asks the
 * core, which is the only side whose answer counts.
 */
void TestBridge::theCoreKnowsAHubByTheNameTheViewsFileItUnder()
{
	const QString typed =
		QStringLiteral("adc://192.0.2.9:412/?kp=SHA256/ZZZZZZZZZZZZZZZZZZZZZZZZZZ");

	Qdc::HubBridge hub(m_thread, typed);

	/* Ordered behind the job that builds the session, so this returns once it has. */
	QVERIFY(Qdc::Testing::runOnCore(m_thread, [] { }));

	const QByteArray asked = Qdc::HubBridge::settingsUrl(typed).toUtf8();
	bool found = false;

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [&] {
		QuickDC::Core* core = Qdc::Testing::core(m_thread);
		if (!core || !core->hubs) return;

		found = core->hubs->getHubByUrl(asked.constData()) != nullptr;
	}));

	QVERIFY2(found,
	         qPrintable(QStringLiteral("the core does not know a hub called '%1', which "
	                                   "is the name every view files its settings under")
	                    .arg(QString::fromUtf8(asked))));
}

/**
 * What a hub says about its boards is true of one connection and of nothing else.
 *
 * The core reports the current answer after every SUP whether or not it moved, so
 * the bridge has to compare against what it last said - and false is not only "no
 * boards": it is the whole of what says that every subscription on the connection
 * has been cancelled, whether the hub withdrew the feature or the connection ended.
 * A client that went on believing it held a subscription would wait for entries that
 * are never coming, since nothing else ever mentions it again.
 */
/**
 * What a hub sends about a board reaches the view through the core, which keeps it:
 * a board read back after the view that read it has gone says which posts were read.
 */
void TestBridge::entriesAHubSendsReachTheViewReadWhereTheyWereRead()
{
	const QString address = QStringLiteral("adc://192.0.2.60:412");
	const QString key = Qdc::BoardBridge::hubKey(address);
	const QString board = QStringLiteral("general");
	const QString first = QStringLiteral("L6LKZG4CUXDSCJQHBLL5GFG6FWAPSLKK2S3VIFA");
	const QString second = QStringLiteral("F6ED42THAKQCOCNHJOUCMOEOX4GOCT25D5WM7SY");

	Qdc::HubBridge hub(m_thread, address);
	{
		Qdc::BoardBridge posts(m_thread);
		QSignalSpy stored(&posts, &Qdc::BoardBridge::entriesStored);

		QuickDC::HubListener* listener = listenerFor(hub);
		QVERIFY(listener);

		QVERIFY(Qdc::Testing::runOnCore(m_thread, [&] {
			listener->EventBoardEntry(core_entry(first, board, 1786439662));
			listener->EventBoardEntry(core_entry(second, board, 1786439663));
		}));

		/* In one batch, as a replay is: a board is not laid out once per entry. */
		QTRY_COMPARE(stored.count(), 1);
		QCOMPARE(stored.at(0).at(0).toString(), key);
		QCOMPARE(stored.at(0).at(2).value<QVector<Qdc::BoardEntry>>().size(), 2);
		QCOMPARE(posts.unread(key, board), 2);

		/* Opened in the reading pane is read; nothing here fetched it. */
		posts.read(key, board, first, false);
		QTRY_COMPARE(posts.unread(key, board), 1);
		QVERIFY(posts.isRead(key, board, first));
		QCOMPARE(posts.postState(first), Qdc::PostState::Announced);
	}

	Qdc::BoardBridge again(m_thread);
	QSignalSpy loaded(&again, &Qdc::BoardBridge::entriesLoaded);
	again.loadEntries(address, board);

	QTRY_COMPARE(loaded.count(), 1);
	QCOMPARE(loaded.at(0).at(2).value<QVector<Qdc::BoardEntry>>().size(), 2);
	QCOMPARE(again.unread(key, board), 1);
	QVERIFY(again.isRead(key, board, first));
	QVERIFY(!again.isRead(key, board, second));
}

/**
 * A post is drawn as the core decided it: read and attributed to the index, not
 * this time and why, or never.
 */
void TestBridge::whatTheCoreSaysOfAPostIsWhatIsDrawn()
{
	namespace Ev = QuickDC::Api::Events;

	Qdc::BoardBridge posts(nullptr);
	QSignalSpy ready(&posts, &Qdc::BoardBridge::postReady);
	QSignalSpy unavailable(&posts, &Qdc::BoardBridge::postUnavailable);
	QSignalSpy unreadable(&posts, &Qdc::BoardBridge::postUnreadable);

	Ev::PostStatus disputed;
	disputed.tth = ENTRY_TTH.toStdString();
	disputed.state = Ev::PostStatus::State::Disputed;
	disputed.post.cid = "SOMEBODYELSE";
	disputed.post.body = "words";
	disputed.attributedTo = AUTHOR_CID.toStdString();
	posts.take(disputed);

	QCOMPARE(ready.count(), 1);
	const Qdc::BoardPost post = ready.at(0).at(1).value<Qdc::BoardPost>();
	QCOMPARE(post.state, Qdc::PostState::Disputed);
	QCOMPARE(post.cid, QStringLiteral("SOMEBODYELSE"));
	QCOMPARE(posts.attribution(ENTRY_TTH), AUTHOR_CID);

	Ev::PostStatus lost;
	lost.tth = "OTHER";
	lost.state = Ev::PostStatus::State::Unavailable;
	lost.why = Ev::PostStatus::Why::NobodyAnswered;
	posts.take(lost);
	QCOMPARE(unavailable.count(), 1);
	QVERIFY(unavailable.at(0).at(1).toString().contains(QStringLiteral("could be reached")));
	QCOMPARE(posts.postState(QStringLiteral("OTHER")), Qdc::PostState::Unavailable);

	Ev::PostStatus bad;
	bad.tth = "BAD";
	bad.state = Ev::PostStatus::State::Unreadable;
	bad.why = Ev::PostStatus::Why::NotAPost;
	posts.take(bad);
	QCOMPARE(unreadable.count(), 1);
	QCOMPARE(unavailable.count(), 1);
}

void TestBridge::aPostWithNoSessionToGoOnSaysWhy()
{
	Qdc::HubBridge hub(nullptr, QStringLiteral("adc://192.0.2.61:412"));
	Qdc::BoardBridge posts(nullptr);
	QSignalSpy refused(&posts, &Qdc::BoardBridge::postRefused);

	posts.write(&hub, QStringLiteral("general"), QString(), QStringLiteral("Hello"),
	            QStringLiteral("A body"), false);

	QCOMPARE(refused.count(), 1);
	QVERIFY(!refused.at(0).at(0).toString().isEmpty());
}

/*
 * What the watch is for: a lease renewed onto a different address, or a VPN
 * coming up over an interface that was already online. Neither drops the link
 * nor changes the transport medium, so neither is reported by Qt - and both
 * leave everything measured about the old address describing somebody else's.
 */
void TestBridge::anAddressChangeIsWhatCountsAsTheNetworkChanging()
{
	const QString before = Qdc::NetworkWatch::addressFingerprint(
		{(QStringLiteral("192.168.1.10"))});

	const QString after = Qdc::NetworkWatch::addressFingerprint(
		{(QStringLiteral("192.168.1.11"))});

	QVERIFY2(before != after, "a different address read as the same network");

	/* A second interface is a change too: it is where traffic may now leave from. */
	const QString added = Qdc::NetworkWatch::addressFingerprint(
		{(QStringLiteral("192.168.1.10")),
		 (QStringLiteral("10.8.0.2"))});

	QVERIFY2(added != before, "an interface appearing read as the same network");

	/* And so is one going, which is a VPN dropping. */
	QVERIFY2(Qdc::NetworkWatch::addressFingerprint({}) != before,
	         "every address disappearing read as the same network");
}

/*
 * Loopback is on every machine and changes on none, and a link-local address is
 * what the host gives itself while DHCP is still deciding. Counting either would
 * mean reporting a change that had not happened, and in the link-local case
 * reporting the negotiation and then the answer as two.
 */
void TestBridge::addressesTheMachineAlwaysHasAreNotAChange()
{
	const QString real = Qdc::NetworkWatch::addressFingerprint(
		{(QStringLiteral("192.168.1.10"))});

	const QString padded = Qdc::NetworkWatch::addressFingerprint(
		{(QStringLiteral("127.0.0.1")),
		 (QStringLiteral("::1")),
		 (QStringLiteral("169.254.3.4")),
		 (QStringLiteral("fe80::1")),
		 (QStringLiteral("192.168.1.10"))});

	QCOMPARE(padded, real);

	/* A machine with nothing but those has no addresses worth comparing. */
	QVERIFY(Qdc::NetworkWatch::addressFingerprint(
		{(QStringLiteral("127.0.0.1")),
		 (QStringLiteral("fe80::1"))}).isEmpty());
}

/*
 * The interface list is not ordered by contract, so the same machine can enumerate
 * the same addresses in a different order. Comparing them as given would report a
 * network change every time that order moved, and each one starts a measurement.
 */
void TestBridge::theOrderInterfacesComeBackInIsNotAChange()
{
	const QStringList one = {QStringLiteral("192.168.1.10"),
	                         QStringLiteral("10.8.0.2"),
	                         QStringLiteral("2001:db8::5")};

	const QStringList other = {QStringLiteral("2001:db8::5"),
	                           QStringLiteral("192.168.1.10"),
	                           QStringLiteral("10.8.0.2")};

	QCOMPARE(Qdc::NetworkWatch::addressFingerprint(one),
	         Qdc::NetworkWatch::addressFingerprint(other));

	/* The same address twice is the same address; two interfaces can carry it. */
	QCOMPARE(Qdc::NetworkWatch::addressFingerprint(
		{(QStringLiteral("192.168.1.10")),
		 (QStringLiteral("192.168.1.10"))}),
	         Qdc::NetworkWatch::addressFingerprint(
		{(QStringLiteral("192.168.1.10"))}));

}

/*
 * Nothing is reported for the network the client started on, which is the one
 * already measured at start-up.
 *
 * Two things would say otherwise and neither is a change. The addresses are
 * compared against a baseline taken when the watch is built, or the first poll
 * would find every address on the machine to be new. And Qt announces the state
 * it found as though it had just arrived - a backend finishes loading after the
 * object asking for it exists, so the first thing heard about an unchanged
 * network is that it is online and on WiFi.
 *
 * Waited out rather than sampled, because what this guards arrives late:
 * transitions are collected and reported only once the line has held still, so
 * anything armed by the backend settling appears a whole quiet period after the
 * signal that armed it, and a shorter wait would pass without having looked.
 */
void TestBridge::aWatchSaysNothingAboutANetworkThatHasNotChanged()
{
	Qdc::NetworkWatch watch;

	QSignalSpy changed(&watch, &Qdc::NetworkWatch::connectivityChanged);
	QVERIFY(changed.isValid());

	QTest::qWait(3500);

	QCOMPARE(changed.count(), 0);
}

/*
 * An event that belongs to a hub reaches that hub's route and nothing else, so a
 * hub's user list at login is not queued once to every bridge for all but one to
 * throw away; an event of no hub stays where every bridge hears it.
 */
void TestBridge::aHubsEventsReachOnlyThatHubsRoute()
{
	namespace Events = QuickDC::Api::Events;

	Qdc::CoreChannel channel;
	QSignalSpy everyone(&channel, &Qdc::CoreChannel::received);
	QSignalSpy first(channel.route(1), &Qdc::HubRoute::received);
	QSignalSpy second(channel.route(2), &Qdc::HubRoute::received);

	/* From another thread, as the core delivers them, so the queueing is the real one. */
	QThread* core = QThread::create([&channel] {
		channel.deliver(Events::UserJoined{1, "alice"});
		channel.deliver(Events::UserJoined{1, "bob"});
		channel.deliver(Events::UserJoined{2, "carol"});
		channel.deliver(Events::QueueChanged{});
		channel.deliver(Events::UserJoined{3, "nobody listens"});
	});
	core->start();
	QVERIFY(core->wait(5000));
	delete core;

	QTRY_COMPARE(everyone.count(), 1);
	QTRY_COMPARE(first.count(), 2);
	QCOMPARE(second.count(), 1);

	const auto nick = [](const QSignalSpy& spy, int n) {
		return spy.at(n).at(0).value<QuickDC::Api::Event>().as<Events::UserJoined>()->nick;
	};
	QCOMPARE(nick(first, 0), std::string("alice"));
	QCOMPARE(nick(first, 1), std::string("bob"));
	QCOMPARE(nick(second, 0), std::string("carol"));
	QVERIFY(everyone.at(0).at(0).value<QuickDC::Api::Event>().as<Events::QueueChanged>());

	/* A route given back hears nothing more, and asking again starts a fresh one. */
	channel.releaseRoute(2);
	QSignalSpy again(channel.route(2), &Qdc::HubRoute::received);
	QCOMPARE(again.count(), 0);
}

QTEST_GUILESS_MAIN(TestBridge)

#include "tst_bridge.moc"

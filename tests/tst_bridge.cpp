/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/configbridge.h>
#include <core/corethread.h>
#include <core/filelistbridge.h>
#include <core/hubbridge.h>
#include <core/hublistbridge.h>
#include <core/mediabridge.h>
#include <core/searchbridge.h>
#include <core/transferbridge.h>

/*
 * The core's own headers, which nothing above this layer may reach for - and this
 * is the test of the layer itself, whose job includes checking that the two sides
 * agree about things neither can see the other decide.
 */
#include <api/core.h>
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
	void jobsRunInOrderOnTheCoreThread();
	void aBlockingCallDoesNotWaitForThePollInterval();
	void snapshotsDoNotWaitForABusyCore();
	void anOfferedFileIsRecognisedAfterARestart();
	void configRoundTripsThroughTheStore();
	void aGroupIsReadInOneTrip();
	void configWritesTheFileItWasGiven();
	void hubBridgeReportsErrorsOnTheCallingThread();
	void transfersAndQueueStartEmpty();
	void bridgesSurviveBeingDestroyedWhileRunning();
	void nothingAViewAsksForWaitsForABusyCore();
	void bridgesAreBuiltAndTornDownWithoutWaiting();
	void theCoreKnowsAHubByTheNameTheViewsFileItUnder();

private:
	QString path(const QString& name) const;

	QTemporaryDir m_dir;
	Qdc::CoreThread* m_thread = nullptr;
};

QString TestBridge::path(const QString& name) const
{
	return QDir(m_dir.path()).filePath(name);
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
	QVERIFY(m_thread->core() == nullptr);

	bool visible = false;
	QThread* ran_on = nullptr;

	QVERIFY(m_thread->runOnCoreBlocking([&] {
		visible = m_thread->core() != nullptr;
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
		m_thread->post([&order, n] { order.append(n); });

	/* Ordered behind all eight, so its completion means they have all run. */
	QVERIFY(m_thread->runOnCoreBlocking([] { }));

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
		QVERIFY(m_thread->runOnCoreBlocking([&answered] { ++answered; }));
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
	m_thread->post([] { QThread::msleep(1500); });

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

	config.notifyChanged();
	QVERIFY(m_thread->runOnCoreBlocking([] { }));
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
	QVERIFY(m_thread->runOnCoreBlocking([] { }));
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

	const QString group = QStringLiteral("Busy");

	config.setString(Qdc::ConfigStore::Application, group,
	                 QStringLiteral("Nickname"), QStringLiteral("somebody"));

	m_thread->post([] { QThread::msleep(1500); });

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

	m_thread->post([] { QThread::msleep(1500); });

	QElapsedTimer timer;
	timer.start();

	{
		Qdc::TransferBridge transfers(m_thread);
		Qdc::MediaBridge media(m_thread, &config);
		Qdc::FilelistBridge filelists(m_thread);
		Qdc::HubListBridge hublists(m_thread);
		Qdc::HubBridge hub(m_thread, QStringLiteral("adc://192.0.2.2:412"));
	}

	const qint64 spent = timer.elapsed();

	QVERIFY2(spent < 100,
	         qPrintable(QStringLiteral("building and destroying the bridges took %1 ms "
	                                   "while the core was busy").arg(spent)));

	/* The jobs they left behind still run, and leave the reactor answering. */
	QVERIFY(m_thread->runOnCoreBlocking([] { }));
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
	QVERIFY(m_thread->runOnCoreBlocking([] { }));

	const QByteArray asked = Qdc::HubBridge::settingsUrl(typed).toUtf8();
	bool found = false;

	QVERIFY(m_thread->runOnCoreBlocking([&] {
		QuickDC::Core* core = m_thread->core();
		if (!core || !core->hubs) return;

		found = core->hubs->getHubByUrl(asked.constData()) != nullptr;
	}));

	QVERIFY2(found,
	         qPrintable(QStringLiteral("the core does not know a hub called '%1', which "
	                                   "is the name every view files its settings under")
	                    .arg(QString::fromUtf8(asked))));
}

QTEST_GUILESS_MAIN(TestBridge)

#include "tst_bridge.moc"

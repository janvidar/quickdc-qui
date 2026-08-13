/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/boardbridge.h>
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
#include <api/bulletins.h>
#include <api/core.h>
#include <api/hub.h>
#include <hash/hashjob.h>
#include <network/adc/adcbbs.h>
#include <network/downloadqueue.h>
#include <network/hubmanager.h>

#include <samurai/crypto/digest/merkletree.h>
#include <samurai/crypto/digest/tiger.h>

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

	void bulletinsGoWithTheSessionAndTakeSubscriptionsWithThem();
	void entriesArriveInOneBatchRatherThanOneSignalEach();
	void anEntryIsKeptAndTheCursorIsTheHighestSeen();
	void aBoardWithAHoleInItSaysSo();
	void nothingIsFetchedMerelyBecauseAnEntryArrived();
	void aDocumentThatIsNotThePostIsThrownAwayAndNeverServed();
	void aDocumentWhoseAuthorDisagreesIsDisputedAndAttributedToTheIndex();
	void aTransferPastItsCapIsAbandoned();
	void aPostBeingReadIsNotInSomebodysTransferList();
	void aDocumentSomebodyIsBeingSentIsNotReleased();

private:
	QString path(const QString& name) const;

	/** A directory of its own for one test's boards, documents and fetches. */
	QString boards(const QString& name) const;

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
const QString OTHER_TTH =
	QStringLiteral("F6ED42THAKQCOCNHJOUCMOEOX4GOCT25D5WM7SY");

const QString AUTHOR_CID =
	QStringLiteral("IPJJWEPPPLCA3PF2ZCRRYO4F2ZX2EV2JMW2KC3I");
const QString OTHER_CID =
	QStringLiteral("NNPPWEPPPLCA3PF2ZCRRYO4F2ZX2EV2JMW2KC3I");

/**
 * The tiger tree root of some bytes, computed the way the core computes one.
 *
 * A post is named by the hash of its own document, so a test that wants a document
 * which verifies has to name it the same way the client will. Written out here
 * rather than asked of a bridge, because what is being checked is that the bridge
 * arrives at this answer for itself.
 */
QString tth_of(const QByteArray& bytes)
{
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree tree(
		&tiger, Samurai::Crypto::Digest::DEFAULT_MERKLE_BLOCK_SIZE,
		QUICKDC_TTH_TREE_LEVELS);

	tree.update(bytes.constData(), static_cast<size_t>(bytes.size()));

	char printed[128];
	if (!tree.digest().getFormattedString(
			Samurai::Crypto::Digest::HashValue::Format::Base32, printed,
			sizeof(printed)))
	{
		return QString();
	}

	return QString::fromLatin1(printed);
}

/** A post document: its canonical header, the newline that ends it, and a body. */
QByteArray post_document(const QString& cid, const QString& subject,
                         const QByteArray& body)
{
	QuickDC::Post post;
	post.cid = cid.toStdString();
	post.subject = subject.toStdString();

	const std::string header = ADC::writePostHeader(post);

	QByteArray document = QByteArray::fromStdString(header);
	document.append('\n');
	document.append(body);

	return document;
}

QuickDC::BoardEntry core_entry(const QString& tth, const QString& board,
                               const QString& cid, quint64 timestamp)
{
	QuickDC::BoardEntry entry;

	entry.tth = tth.toStdString();
	entry.size = 412;
	entry.board = board.toStdString();
	entry.cid = cid.toStdString();
	entry.thread = tth.toStdString();
	entry.timestamp = timestamp;

	return entry;
}

Qdc::BoardEntry bridge_entry(const QString& tth, const QString& board,
                             const QString& cid, quint64 size)
{
	Qdc::BoardEntry entry;

	entry.tth = tth;
	entry.size = size;
	entry.board = board;
	entry.cid = cid;
	entry.thread = tth;
	entry.timestamp = 1786439662;

	return entry;
}

}

QString TestBridge::path(const QString& name) const
{
	return QDir(m_dir.path()).filePath(name);
}

QString TestBridge::boards(const QString& name) const
{
	const QString directory = path(name);
	QDir().mkpath(directory);

	return directory;
}

QuickDC::HubListener* TestBridge::listenerFor(const Qdc::HubBridge& hub)
{
	const QByteArray url = Qdc::HubBridge::settingsUrl(hub.url()).toUtf8();

	QuickDC::HubListener* listener = nullptr;

	/* Ordered behind the job that builds the session, so this finds one. */
	if (!m_thread->runOnCoreBlocking([&] {
		QuickDC::Core* core = m_thread->core();
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
	Qdc::BoardBridge posts(m_thread, &transfers, boards(QStringLiteral("busy")));

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

	/* Everything a board pane reaches for, including the questions about a hub. */
	hub.supportsBulletins();
	hub.subscribedBoards();
	hub.subscribeBoard(QStringLiteral("general"), 0);
	hub.requestBoardEntry(QStringLiteral("general"), ENTRY_TTH);
	hub.submitPost(ENTRY_TTH, 412, QStringLiteral("general"),
	               QStringLiteral("A subject"), QString());
	hub.withdrawPost(ENTRY_TTH, QStringLiteral("general"));
	hub.unsubscribeBoard(QStringLiteral("general"));

	posts.follow(&hub);
	posts.postState(ENTRY_TTH);
	posts.attribution(ENTRY_TTH);
	posts.prefetches(QStringLiteral("adc://192.0.2.1:412"));
	posts.serves(QStringLiteral("adc://192.0.2.1:412"));
	posts.setServing(QStringLiteral("adc://192.0.2.1:412"), true);
	posts.loadEntries(QStringLiteral("adc://192.0.2.1:412"),
	                  QStringLiteral("general"));
	posts.requestState(QStringLiteral("adc://192.0.2.1:412"),
	                   QStringLiteral("general"));
	posts.resume(&hub, QStringLiteral("general"));
	posts.fetch(QStringLiteral("adc://192.0.2.1:412"),
	            bridge_entry(ENTRY_TTH, QStringLiteral("general"), AUTHOR_CID, 412));
	posts.evictNow();
	posts.releasePending();

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
void TestBridge::bulletinsGoWithTheSessionAndTakeSubscriptionsWithThem()
{
	Qdc::HubBridge hub(m_thread, QStringLiteral("adc://192.0.2.30:412"), this);

	QuickDC::HubListener* listener = listenerFor(hub);
	QVERIFY(listener != nullptr);

	QSignalSpy support(&hub, &Qdc::HubBridge::bulletinSupportChanged);
	QVERIFY(support.isValid());
	QVERIFY(!hub.supportsBulletins());

	/* Said twice, as a hub sending two SUPs would have it said. */
	QVERIFY(m_thread->runOnCoreBlocking([listener] {
		listener->EventBoardsAvailable(true);
		listener->EventBoardsAvailable(true);
	}));

	QTRY_COMPARE(support.count(), 1);
	QCOMPARE(support.at(0).at(0).toBool(), true);
	QVERIFY(hub.supportsBulletins());

	hub.subscribeBoard(QStringLiteral("general"), 0);
	hub.subscribeBoard(QStringLiteral("announcements"), 1786439662);

	QVERIFY(hub.isSubscribed(QStringLiteral("general")));
	QCOMPARE(hub.subscribedBoards().size(), 2);

	/* The hub withdraws the feature, which cancels both of them. */
	QVERIFY(m_thread->runOnCoreBlocking([listener] {
		listener->EventBoardsAvailable(false);
	}));

	QTRY_COMPARE(support.count(), 2);
	QCOMPARE(support.at(1).at(0).toBool(), false);
	QVERIFY(!hub.supportsBulletins());
	QVERIFY(hub.subscribedBoards().isEmpty());
	QVERIFY(!hub.isSubscribed(QStringLiteral("general")));

	/* And again, with the connection ending rather than the hub saying anything. */
	QVERIFY(m_thread->runOnCoreBlocking([listener] {
		listener->EventBoardsAvailable(true);
	}));

	QTRY_COMPARE(support.count(), 3);
	hub.subscribeBoard(QStringLiteral("general"), 0);
	QVERIFY(hub.isSubscribed(QStringLiteral("general")));

	QVERIFY(m_thread->runOnCoreBlocking([listener] {
		listener->EventNetStatus(QuickDC::HubListener::NetDisconnected);
	}));

	QTRY_COMPARE(support.count(), 4);
	QCOMPARE(support.at(3).at(0).toBool(), false);
	QVERIFY(hub.subscribedBoards().isEmpty());
}

/**
 * A resumed subscription is a burst, and one model transaction each is what froze
 * the window when searches did the same thing.
 */
void TestBridge::entriesArriveInOneBatchRatherThanOneSignalEach()
{
	Qdc::HubBridge hub(m_thread, QStringLiteral("adc://192.0.2.31:412"), this);

	QuickDC::HubListener* listener = listenerFor(hub);
	QVERIFY(listener != nullptr);

	QSignalSpy batches(&hub, &Qdc::HubBridge::boardEntriesReceived);
	QVERIFY(batches.isValid());

	QVERIFY(m_thread->runOnCoreBlocking([listener] {
		for (int n = 0; n < 6; ++n)
		{
			QuickDC::BoardEntry entry = core_entry(ENTRY_TTH,
			                                       QStringLiteral("general"),
			                                       AUTHOR_CID, 1786439662 + n);

			/* One hash per entry, since an index is keyed on it. */
			entry.tth[0] = static_cast<char>('A' + n);

			listener->EventBoardEntry(entry);
		}
	}));

	QTRY_COMPARE(batches.count(), 1);

	const QVector<Qdc::BoardEntry> arrived =
		batches.at(0).at(0).value<QVector<Qdc::BoardEntry>>();

	QCOMPARE(arrived.size(), 6);
	QCOMPARE(arrived.at(0).board, QStringLiteral("general"));
	QCOMPARE(arrived.at(0).cid, AUTHOR_CID);
	QCOMPARE(arrived.at(5).timestamp, 1786439662u + 5);
}

/**
 * An entry is written down as it arrives, and the cursor is the highest timestamp
 * seen rather than one past it.
 *
 * One second past the highest is the natural thing to write and it is wrong:
 * timestamps are seconds and are not unique, so a post the hub accepts in the same
 * second as the last entry received would fall behind the cursor and be delivered to
 * nobody. This asks the bridge for the cursor and holds it against what the hub
 * actually said.
 */
void TestBridge::anEntryIsKeptAndTheCursorIsTheHighestSeen()
{
	Qdc::TransferBridge transfers(m_thread, this);
	Qdc::BoardBridge posts(m_thread, &transfers, boards(QStringLiteral("kept")),
	                       this);
	Qdc::HubBridge hub(m_thread, QStringLiteral("adc://192.0.2.32:412"), this);

	posts.follow(&hub);
	QVERIFY(posts.follows(&hub));

	QuickDC::HubListener* listener = listenerFor(hub);
	QVERIFY(listener != nullptr);

	QSignalSpy stored(&posts, &Qdc::BoardBridge::entriesStored);
	QVERIFY(stored.isValid());

	QVERIFY(m_thread->runOnCoreBlocking([listener] {
		listener->EventBoardEntry(core_entry(ENTRY_TTH, QStringLiteral("general"),
		                                     AUTHOR_CID, 1786439000));
		listener->EventBoardEntry(core_entry(OTHER_TTH, QStringLiteral("general"),
		                                     AUTHOR_CID, 1786439662));

		/* The same entry again, as the resumption window replays it. */
		listener->EventBoardEntry(core_entry(OTHER_TTH, QStringLiteral("general"),
		                                     AUTHOR_CID, 1786439662));
	}));

	QTRY_COMPARE(stored.count(), 1);

	const QVector<Qdc::BoardEntry> kept =
		stored.at(0).at(2).value<QVector<Qdc::BoardEntry>>();

	/* The duplicate was dropped, and the two real entries were kept. */
	QCOMPARE(kept.size(), 2);

	QSignalSpy state(&posts, &Qdc::BoardBridge::boardStateReady);
	QVERIFY(state.isValid());

	posts.requestState(Qdc::BoardBridge::hubKey(hub.url()),
	                   QStringLiteral("general"));

	QTRY_COMPARE(state.count(), 1);
	QCOMPARE(state.at(0).at(2).toULongLong(), 1786439662u);

	/* Nothing has described the board, so whether it is whole cannot be answered. */
	QCOMPARE(state.at(0).at(3).value<Qdc::BoardBridge::View>(),
	         Qdc::BoardBridge::View::Unknown);

	/* And the entries read back as a board rather than as what arrived. */
	QSignalSpy loaded(&posts, &Qdc::BoardBridge::entriesLoaded);
	posts.loadEntries(Qdc::BoardBridge::hubKey(hub.url()),
	                  QStringLiteral("general"));

	QTRY_COMPARE(loaded.count(), 1);
	QCOMPARE(loaded.at(0).at(2).value<QVector<Qdc::BoardEntry>>().size(), 2);
}

/**
 * A hub that will not replay far enough back leaves a hole no subscription can
 * close, and an interface has to be able to say so rather than present an incomplete
 * board as the whole of one.
 */
void TestBridge::aBoardWithAHoleInItSaysSo()
{
	Qdc::BoardBridge posts(m_thread, nullptr, boards(QStringLiteral("gap")), this);
	Qdc::HubBridge hub(m_thread, QStringLiteral("adc://192.0.2.33:412"), this);

	posts.follow(&hub);

	QuickDC::HubListener* listener = listenerFor(hub);
	QVERIFY(listener != nullptr);

	QSignalSpy state(&posts, &Qdc::BoardBridge::boardStateReady);
	QVERIFY(state.isValid());

	QVERIFY(m_thread->runOnCoreBlocking([listener] {
		listener->EventBoardEntry(core_entry(ENTRY_TTH, QStringLiteral("general"),
		                                     AUTHOR_CID, 1786439000));
	}));

	/*
	 * The hub then says it replays nothing older than a timestamp above what is
	 * held, which is a hub raising its oldest rather than a client falling behind -
	 * and the entries between the two are not coming either way.
	 */
	QVERIFY(m_thread->runOnCoreBlocking([listener] {
		QuickDC::Board board;
		board.name = "general";
		board.permissions = QuickDC::Subscribe;
		board.maxSize = 262144;
		board.newest = 1786500000;
		board.oldest = 1786440000;

		listener->EventBoardDescribed(board);
	}));

	QTRY_VERIFY(state.count() >= 1);

	const QVariantList last = state.at(state.count() - 1);

	QCOMPARE(last.at(1).toString(), QStringLiteral("general"));
	QCOMPARE(last.at(3).value<Qdc::BoardBridge::View>(),
	         Qdc::BoardBridge::View::Gap);
}

/**
 * An entry arriving is not a reason to ask anybody for anything.
 *
 * A subscription is cheap and a body is not. Fetching one says "I have read this" to
 * whoever answers - and the author is the only source until somebody else has read
 * it, so an author who records who fetches has a complete census of the readers a
 * subscription delivered the entry to within the second. So this checks the queue
 * itself: not that no signal was emitted, but that the core was never asked.
 */
void TestBridge::nothingIsFetchedMerelyBecauseAnEntryArrived()
{
	Qdc::TransferBridge transfers(m_thread, this);
	Qdc::BoardBridge posts(m_thread, &transfers,
	                       boards(QStringLiteral("unfetched")), this);
	Qdc::HubBridge hub(m_thread, QStringLiteral("adc://192.0.2.34:412"), this);

	posts.follow(&hub);

	QuickDC::HubListener* listener = listenerFor(hub);
	QVERIFY(listener != nullptr);

	QSignalSpy stored(&posts, &Qdc::BoardBridge::entriesStored);

	QVERIFY(m_thread->runOnCoreBlocking([listener] {
		listener->EventBoardEntry(core_entry(ENTRY_TTH, QStringLiteral("general"),
		                                     AUTHOR_CID, 1786439662));
	}));

	QTRY_COMPARE(stored.count(), 1);

	/* Nothing is announced about the post's body, because nothing was attempted. */
	QCOMPARE(posts.postState(ENTRY_TTH), Qdc::PostState::Announced);

	const QByteArray hash = ENTRY_TTH.toUtf8();
	bool queued = true;

	QVERIFY(m_thread->runOnCoreBlocking([this, hash, &queued] {
		QuickDC::Core* core = m_thread->core();
		if (!core || !core->queue) return;

		queued = core->queue->getFileByTTH(hash.constData()) != nullptr;
	}));

	QVERIFY2(!queued, "an entry arriving put its body in the download queue");

	/* And prefetching is off until a hub is named, so a thread opens the same way. */
	QVERIFY(!posts.prefetches(Qdc::BoardBridge::hubKey(hub.url())));

	posts.prefetchThread(Qdc::BoardBridge::hubKey(hub.url()),
	                     {bridge_entry(ENTRY_TTH, QStringLiteral("general"),
	                                   AUTHOR_CID, 412)});

	QVERIFY(m_thread->runOnCoreBlocking([] { }));
	QCOMPARE(posts.postState(ENTRY_TTH), Qdc::PostState::Announced);
}

/**
 * Bytes that do not hash to the name they were asked for are not the post, are
 * thrown away entirely, and are never offered to anybody.
 *
 * This is the check everything else in the extension rests on. Until it has passed,
 * an index entry is a stranger's description of a file that may not exist - and a
 * client that answered searches for what it had not checked would spread a forgery
 * under a name other clients have verified.
 */
void TestBridge::aDocumentThatIsNotThePostIsThrownAwayAndNeverServed()
{
	const QString directory = boards(QStringLiteral("junk"));

	Qdc::BoardBridge posts(m_thread, nullptr, directory, this);

	/* Serving is on, so that nothing is being proved by its being off. */
	posts.setServing(QStringLiteral("adc://192.0.2.35:412"), true);

	const QString file = QDir(directory).filePath(QStringLiteral("arrived"));

	QFile bytes(file);
	QVERIFY(bytes.open(QIODevice::WriteOnly));
	bytes.write("IBB0 ID" + AUTHOR_CID.toUtf8() + " SJx\nnot the post it claims");
	bytes.close();

	QSignalSpy unavailable(&posts, &Qdc::BoardBridge::postUnavailable);
	QVERIFY(unavailable.isValid());

	posts.adopt(QStringLiteral("adc://192.0.2.35:412"),
	            bridge_entry(ENTRY_TTH, QStringLiteral("general"), AUTHOR_CID, 412),
	            file);

	QTRY_COMPARE(unavailable.count(), 1);
	QCOMPARE(unavailable.at(0).at(0).toString(), ENTRY_TTH);
	QCOMPARE(posts.postState(ENTRY_TTH), Qdc::PostState::Unavailable);

	/* Discarded entirely, and never filed under a name that does not describe it. */
	QVERIFY2(!QFile::exists(file), "the bytes were kept after failing to verify");
	QVERIFY(!QFile::exists(QDir(directory).filePath(QStringLiteral("posts/")
	                                                + ENTRY_TTH)));
}

/**
 * Where the document and the hub disagree about the author, the post is attributed
 * to the hub's claim and the document's is shown as a claim.
 *
 * There is no automatic resolution: the document says who wrote the post and the hub
 * says who submitted it, which are claims about different events. The honest case -
 * a post relayed to a second hub by somebody who did not write it - is
 * indistinguishable from a forgery, so what must never happen is the document's
 * claim being presented as the author.
 */
void TestBridge::aDocumentWhoseAuthorDisagreesIsDisputedAndAttributedToTheIndex()
{
	const QString directory = boards(QStringLiteral("disputed"));
	const QString hubUrl = QStringLiteral("adc://192.0.2.36:412");

	Qdc::BoardBridge posts(m_thread, nullptr, directory, this);
	posts.setServing(hubUrl, true);

	const QByteArray document =
		post_document(AUTHOR_CID, QStringLiteral("Hub upgrade on Saturday"),
		              "The hub will be offline on Saturday.\n");

	const QString tth = tth_of(document);
	QCOMPARE(tth.size(), 39);

	const QString file = QDir(directory).filePath(QStringLiteral("relayed"));

	QFile bytes(file);
	QVERIFY(bytes.open(QIODevice::WriteOnly));
	bytes.write(document);
	bytes.close();

	QSignalSpy ready(&posts, &Qdc::BoardBridge::postReady);
	QVERIFY(ready.isValid());

	/* The hub says it accepted the post from somebody other than its author. */
	posts.adopt(hubUrl,
	            bridge_entry(tth, QStringLiteral("general"), OTHER_CID,
	                         static_cast<quint64>(document.size())),
	            file);

	QTRY_COMPARE(ready.count(), 1);

	const Qdc::BoardPost post = ready.at(0).at(1).value<Qdc::BoardPost>();

	QCOMPARE(post.state, Qdc::PostState::Disputed);
	QCOMPARE(posts.postState(tth), Qdc::PostState::Disputed);

	/* The document's own claim, which is what that field means and is not the author. */
	QCOMPARE(post.cid, AUTHOR_CID);

	/* And the attribution, which is the index entry's and never the document's. */
	QCOMPARE(posts.attribution(tth), OTHER_CID);

	QCOMPARE(post.subject, QStringLiteral("Hub upgrade on Saturday"));
	QVERIFY(post.body.startsWith(QStringLiteral("The hub will be offline")));

	/* It verified, so it is held under its hash and the fetched copy has moved. */
	QVERIFY(QFile::exists(QDir(directory).filePath(QStringLiteral("posts/") + tth)));
	QVERIFY(!QFile::exists(file));

	/*
	 * And asking again reads the copy that is held rather than adopting it a second
	 * time, which would be the store handing its own document to be moved away.
	 */
	QSignalSpy again(&posts, &Qdc::BoardBridge::postReady);

	posts.fetch(hubUrl, bridge_entry(tth, QStringLiteral("general"), OTHER_CID,
	                                 static_cast<quint64>(document.size())));

	QTRY_COMPARE(again.count(), 1);
	QVERIFY2(QFile::exists(QDir(directory).filePath(QStringLiteral("posts/") + tth)),
	         "reading a post that is held threw the document away");
}

/**
 * A declared size bounds nothing, so the transfer is bounded here and abandoned when
 * it is passed.
 *
 * Nothing verifies the size before the transfer: the author states it and the hub
 * copies it, so a peer answering an entry which declared four hundred bytes may send
 * for as long as it is allowed to.
 */
void TestBridge::aTransferPastItsCapIsAbandoned()
{
	/* Both bounds, and neither is the other's excuse. */
	QVERIFY(Qdc::BoardBridge::passesCap(2000, 0, 1024));
	QVERIFY(Qdc::BoardBridge::passesCap(500, 412, 1024));
	QVERIFY(!Qdc::BoardBridge::passesCap(412, 412, 1024));
	QVERIFY(!Qdc::BoardBridge::passesCap(5000, 0, 0));

	const QString directory = boards(QStringLiteral("capped"));

	Qdc::TransferBridge transfers(m_thread, this);
	Qdc::BoardBridge posts(m_thread, &transfers, directory, this);

	posts.setFetchCap(1024);
	QCOMPARE(posts.fetchCap(), 1024u);

	/* An entry that declares more than the cap is not asked for at all. */
	QSignalSpy refused(&posts, &Qdc::BoardBridge::postUnavailable);

	posts.fetch(QStringLiteral("adc://192.0.2.37:412"),
	            bridge_entry(OTHER_TTH, QStringLiteral("general"), AUTHOR_CID,
	                         64 * 1024));

	QTRY_COMPARE(refused.count(), 1);
	QCOMPARE(refused.at(0).at(0).toString(), OTHER_TTH);

	/* One that declares nothing is fetched, and then bounded by what arrives. */
	posts.fetch(QStringLiteral("adc://192.0.2.37:412"),
	            bridge_entry(ENTRY_TTH, QStringLiteral("general"), AUTHOR_CID, 0));

	QTRY_COMPARE(posts.postState(ENTRY_TTH), Qdc::PostState::Fetching);

	Qdc::TransferInfo row;
	row.filename = QDir(QDir(directory).filePath(QStringLiteral("incoming")))
		.filePath(ENTRY_TTH);
	row.transferred = 4096;
	row.length = 4096;

	posts.abandonOverCap({row});

	QTRY_COMPARE(refused.count(), 2);
	QCOMPARE(refused.at(1).at(0).toString(), ENTRY_TTH);
	QCOMPARE(posts.postState(ENTRY_TTH), Qdc::PostState::Unavailable);

	/* And it is out of the queue, so nothing goes on fetching it. */
	const QByteArray hash = ENTRY_TTH.toUtf8();
	bool queued = true;

	QVERIFY(m_thread->runOnCoreBlocking([this, hash, &queued] {
		QuickDC::Core* core = m_thread->core();
		if (!core || !core->queue) return;

		queued = core->queue->getFileByTTH(hash.constData()) != nullptr;
	}));

	QVERIFY2(!queued, "an abandoned fetch was left in the queue");
}

/**
 * A body somebody is reading is not a download somebody chose.
 *
 * A transfer list is a list of what the user asked for, and a thread of ten replies
 * would otherwise put ten rows in it. What is hidden is the row and not the
 * transfer: the file is queued and fetched exactly as before.
 */
void TestBridge::aPostBeingReadIsNotInSomebodysTransferList()
{
	const QString directory = boards(QStringLiteral("hidden"));
	const QString incoming = QDir(directory).filePath(QStringLiteral("incoming"));

	/* A directory whose name merely begins the same way is somebody's own file. */
	QVERIFY(Qdc::TransferBridge::isUnder(incoming + QStringLiteral("/x"),
	                                     {incoming}));
	QVERIFY(!Qdc::TransferBridge::isUnder(incoming + QStringLiteral("scripts/x"),
	                                      {incoming}));
	QVERIFY(!Qdc::TransferBridge::isUnder(QString(), {incoming}));

	Qdc::TransferInfo hidden;
	hidden.id = 1;
	hidden.filename = QDir(incoming).filePath(ENTRY_TTH);
	hidden.transferred = 100;
	hidden.length = 412;

	Qdc::TransferInfo chosen;
	chosen.id = 2;
	chosen.filename = QDir(directory).filePath(QStringLiteral("holiday.jpg"));
	chosen.transferred = 100;
	chosen.length = 4096;

	const QVector<Qdc::TransferInfo> folded =
		Qdc::TransferBridge::foldByFile({hidden, chosen}, {}, {}, 0, {incoming});

	QCOMPARE(folded.size(), 1);
	QCOMPARE(folded.at(0).filename, chosen.filename);

	Qdc::TransferBridge transfers(m_thread, this);
	transfers.hidePath(incoming);
	QCOMPARE(transfers.hiddenPaths(), QStringList{incoming});

	const QString body = QDir(incoming).filePath(ENTRY_TTH);
	const QString film = path(QStringLiteral("film.mkv"));

	transfers.enqueue(body, ENTRY_TTH, 412);
	transfers.enqueue(film, OTHER_TTH, 4096);

	/* The one the user asked for turns up in the snapshot, and the other does not. */
	QTRY_VERIFY(!transfers.queueSnapshot().isEmpty());

	bool sawFilm = false;
	bool sawBody = false;

	for (const Qdc::QueueEntry& entry : transfers.queueSnapshot())
	{
		if (entry.filename == film) sawFilm = true;
		if (entry.filename == body) sawBody = true;
	}

	QVERIFY(sawFilm);
	QVERIFY2(!sawBody, "a post being read appeared in the queue a user is shown");

	/* The core has both of them, which is the point: the row is hidden, not the file. */
	const QByteArray hidden8 = body.toUtf8();
	bool queued = false;

	QVERIFY(m_thread->runOnCoreBlocking([this, hidden8, &queued] {
		QuickDC::Core* core = m_thread->core();
		if (!core || !core->queue) return;

		queued = core->queue->getFileByName(hidden8.constData()) != nullptr;
	}));

	QVERIFY(queued);

	transfers.removeFromQueue(body);
	transfers.removeFromQueue(film);

	QVERIFY(m_thread->runOnCoreBlocking([] { }));
}

/**
 * A document that has been marked to go is not released while a peer is being sent
 * it.
 *
 * An upload holds the interior handle the share manager returns for a hash - a
 * member of the object that withdrawing it destroys - and reads through it until the
 * transfer ends. The share manager hears nothing about transfers and the store knows
 * about neither, so this layer is the only one that can say; getting it wrong is a
 * use-after-free rather than a wrong pixel.
 */
void TestBridge::aDocumentSomebodyIsBeingSentIsNotReleased()
{
	Qdc::TransferInfo sending;
	sending.upload = true;
	sending.filename = QStringLiteral("/home/somebody/.quickdc/posts/") + ENTRY_TTH;

	Qdc::TransferInfo fetching;
	fetching.filename = QStringLiteral("/home/somebody/.quickdc/posts/") + OTHER_TTH;

	const QVector<QString> marked = {ENTRY_TTH, OTHER_TTH};

	const QVector<QString> allowed =
		Qdc::BoardBridge::releasable(marked, {sending, fetching});

	/* The one being sent stays marked; the one merely being read is not an upload. */
	QCOMPARE(allowed.size(), 1);
	QCOMPARE(allowed.at(0), OTHER_TTH);

	/* With nothing being sent, everything marked may go. */
	QCOMPARE(Qdc::BoardBridge::releasable(marked, {}).size(), 2);

	/* And a shared file that happens to be uploading is nothing to do with these. */
	Qdc::TransferInfo unrelated;
	unrelated.upload = true;
	unrelated.filename = QStringLiteral("/home/somebody/share/holiday.jpg");

	QCOMPARE(Qdc::BoardBridge::releasable(marked, {unrelated}).size(), 2);
}

QTEST_GUILESS_MAIN(TestBridge)

#include "tst_bridge.moc"

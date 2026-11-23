/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/boardbridge.h>
#include <core/configbridge.h>
#include <core/corethread.h>
#include <core/filelistbridge.h>
#include <core/hubbridge.h>
#include <core/hublistbridge.h>
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
	void aPostIsOfferedForTheSessionAndWithoutASlot();
	void aPostHeldFromAnEarlierSessionIsOfferedWhenItsBoardIsTakenUp();
	void aPostHeldFromAnEarlierSessionIsOfferedWhenItsBoardIsReadBack();
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
	void aHublistNobodyServesIsReportedAsFailed();
	void aHublistBridgeGoneBeforeTheAnswerHearsNothing();

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
	void aPostOpenedReadsAsReadAndTheBoardsCountFalls();
	void readStateOutlivesTheBridgeThatWroteIt();
	void aMarkThatCouldNotBeWrittenLeavesThePostUnread();
	void aBoardReadBackKnowsWhichDocumentsItHolds();
	void aHeldDocumentIsReadWithoutAskingAnybody();
	void aDocumentKnownNotToBeThePostIsNotOfferedAsFetchable();

	void anAddressChangeIsWhatCountsAsTheNetworkChanging();
	void addressesTheMachineAlwaysHasAreNotAChange();
	void theOrderInterfacesComeBackInIsNotAChange();
	void aWatchSaysNothingAboutANetworkThatHasNotChanged();

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

	/**
	 * Whether this client is answering for the document, and answering for it as a
	 * post.
	 *
	 * Slot free is what tells the two apart: an ordinary attachment is offered on the
	 * usual terms and a post document is offered without taking a slot, so a check on
	 * being shared at all would pass on either.
	 */
	bool isOffered(const QByteArray& tth);

	/**
	 * Leave a board with one post read, held and verified, and nothing offering it.
	 *
	 * The state a client starts up in, arrived at the way a client arrives at it: an
	 * entry from the hub, a document fetched, and then the offer withdrawn by hand
	 * because the share manager belongs to the process rather than to the bridge that
	 * made the offer. Without that last step every assertion about a later session
	 * would pass on the strength of this one.
	 */
	void readAPostInAnEarlierSession(const QString& directory, const QString& address,
	                                const QString& board,
	                                const QByteArray& document);

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

/**
 * A post document and a picture reach the share by the same call and must not be
 * offered on the same terms.
 *
 * Both halves matter and both were wrong. A post remembered in the configuration goes
 * on being offered at the next start-up whatever the user said about the hub it came
 * from; a post that takes an upload slot is turned away at the one moment its author is
 * its only source.
 */
void TestBridge::aPostIsOfferedForTheSessionAndWithoutASlot()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::MediaBridge media(m_thread, &config);

	const auto write = [this](const char* name, const QByteArray& content) {
		const QString path = QDir(m_dir.path()).filePath(QLatin1String(name));

		QFile file(path);
		if (!file.open(QIODevice::WriteOnly)) return QString();

		file.write(content);
		file.close();

		return path;
	};

	const QString post = write("post-document", "IBB0 IDSOMEBODY\nthe words\n");
	const QString picture = write("holiday.jpg", "not really a picture");

	QVERIFY(!post.isEmpty() && !picture.isEmpty());

	QSignalSpy attached(&media, &Qdc::MediaBridge::attached);
	QSignalSpy refused(&media, &Qdc::MediaBridge::attachFailed);

	media.attach(post, Qdc::MediaBridge::Offer::Post);
	media.attach(picture, Qdc::MediaBridge::Offer::Attachment);

	QTRY_VERIFY(attached.count() + refused.count() == 2);

	QVERIFY2(refused.isEmpty(),
	         qPrintable(refused.isEmpty() ? QString()
	                                      : refused.first().at(1).toString()));

	QHash<QString, QString> hashes;

	for (const QList<QVariant>& one : attached)
		hashes.insert(one.at(0).toString(), one.at(1).toString());

	const QByteArray postTth = hashes.value(post).toUtf8();
	const QByteArray pictureTth = hashes.value(picture).toUtf8();

	QVERIFY(!postTth.isEmpty() && !pictureTth.isEmpty());

	bool postFree = false;
	bool pictureFree = false;
	int remembered = -1;

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [&] {
		QuickDC::Core* core = Qdc::Testing::core(m_thread);
		if (!core || !core->shares || !core->config) return;

		postFree = core->shares->isSlotFreeByTTH(postTth.constData());
		pictureFree = core->shares->isSlotFreeByTTH(pictureTth.constData());

		/*
		 * Detaching the picture is what makes the group be written - nothing else in a
		 * test's lifetime does, so an assertion on the configuration without it passes
		 * whatever either attachment asked for - and it is also what makes the answer
		 * mean something: the picture is remembered and is going, so what is left is
		 * whatever else was remembered. Nothing, if the post was for the session; the
		 * post, if it was not.
		 */
		core->shares->detach(pictureTth.constData());

		remembered = core->config->peekNumber("Attachments", "Total", -1);
	}));

	QVERIFY(postFree);
	QVERIFY(!pictureFree);
	QCOMPARE(remembered, 0);
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
	QVERIFY(Qdc::Testing::runOnCore(m_thread, [] { }));
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
	Qdc::BoardBridge posts(m_thread, &transfers, boards(QStringLiteral("busy")));

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

	Qdc::Testing::post(m_thread, [] { QThread::msleep(1500); });

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
	QVERIFY(Qdc::Testing::runOnCore(m_thread, [] { }));
	QVERIFY(m_thread->isCoreRunning());
}

/**
 * A fetch goes to the core as a command and comes back as an event, so the one
 * thing a view can see of the crossing is that the answer arrives, once, for the
 * address it asked about - and that asking again while it is on its way does not
 * make a second one.
 */
void TestBridge::aHublistNobodyServesIsReportedAsFailed()
{
	Qdc::HubListBridge hublists(m_thread);
	QSignalSpy failed(&hublists, &Qdc::HubListBridge::failed);
	QSignalSpy fetched(&hublists, &Qdc::HubListBridge::fetched);

	const QString url = QStringLiteral("http://127.0.0.1:1/hublist.xml.bz2");
	hublists.fetch(url);
	hublists.fetch(url);
	QVERIFY(hublists.isFetching(url));

	QVERIFY(failed.wait(10000));
	QCOMPARE(failed.count(), 1);
	QCOMPARE(failed.first().at(0).toString(), url);
	QVERIFY(failed.first().at(1).toString().contains(url));
	QCOMPARE(fetched.count(), 0);
	QVERIFY(!hublists.isFetching(url));

	/* Nothing more is on its way for it. */
	QTest::qWait(200);
	QCOMPARE(failed.count(), 1);
}

/* The answer to a bridge that has gone arrives at nothing, and nothing breaks. */
void TestBridge::aHublistBridgeGoneBeforeTheAnswerHearsNothing()
{
	{
		Qdc::HubListBridge hublists(m_thread);
		hublists.fetch(QStringLiteral("http://127.0.0.1:1/gone.xml"));
	}

	Qdc::HubListBridge other(m_thread);
	QSignalSpy failed(&other, &Qdc::HubListBridge::failed);

	QTest::qWait(500);
	QCOMPARE(failed.count(), 0);
	QVERIFY(Qdc::Testing::runOnCore(m_thread, [] { }));
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
void TestBridge::bulletinsGoWithTheSessionAndTakeSubscriptionsWithThem()
{
	Qdc::HubBridge hub(m_thread, QStringLiteral("adc://192.0.2.30:412"), this);

	QuickDC::HubListener* listener = listenerFor(hub);
	QVERIFY(listener != nullptr);

	QSignalSpy support(&hub, &Qdc::HubBridge::bulletinSupportChanged);
	QVERIFY(support.isValid());
	QVERIFY(!hub.supportsBulletins());

	/* Said twice, as a hub sending two SUPs would have it said. */
	QVERIFY(Qdc::Testing::runOnCore(m_thread, [listener] {
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
	QVERIFY(Qdc::Testing::runOnCore(m_thread, [listener] {
		listener->EventBoardsAvailable(false);
	}));

	QTRY_COMPARE(support.count(), 2);
	QCOMPARE(support.at(1).at(0).toBool(), false);
	QVERIFY(!hub.supportsBulletins());
	QVERIFY(hub.subscribedBoards().isEmpty());
	QVERIFY(!hub.isSubscribed(QStringLiteral("general")));

	/* And again, with the connection ending rather than the hub saying anything. */
	QVERIFY(Qdc::Testing::runOnCore(m_thread, [listener] {
		listener->EventBoardsAvailable(true);
	}));

	QTRY_COMPARE(support.count(), 3);
	hub.subscribeBoard(QStringLiteral("general"), 0);
	QVERIFY(hub.isSubscribed(QStringLiteral("general")));

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [listener] {
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

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [listener] {
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

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [listener] {
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

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [listener] {
		listener->EventBoardEntry(core_entry(ENTRY_TTH, QStringLiteral("general"),
		                                     AUTHOR_CID, 1786439000));
	}));

	/*
	 * The hub then says it replays nothing older than a timestamp above what is
	 * held, which is a hub raising its oldest rather than a client falling behind -
	 * and the entries between the two are not coming either way.
	 */
	QVERIFY(Qdc::Testing::runOnCore(m_thread, [listener] {
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

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [listener] {
		listener->EventBoardEntry(core_entry(ENTRY_TTH, QStringLiteral("general"),
		                                     AUTHOR_CID, 1786439662));
	}));

	QTRY_COMPARE(stored.count(), 1);

	/* Nothing is announced about the post's body, because nothing was attempted. */
	QCOMPARE(posts.postState(ENTRY_TTH), Qdc::PostState::Announced);

	const QByteArray hash = ENTRY_TTH.toUtf8();

	const auto queuedNow = [this, hash] {
		bool queued = false;

		Qdc::Testing::runOnCore(m_thread, [this, hash, &queued] {
			QuickDC::Core* core = Qdc::Testing::core(m_thread);
			if (!core || !core->queue) return;

			queued = core->queue->getFileByTTH(hash.constData()) != nullptr;
		});

		return queued;
	};

	QVERIFY2(!queuedNow(), "an entry arriving put its body in the download queue");

	/* And prefetching is off until a hub is named, so a thread opens the same way. */
	const QString key = Qdc::BoardBridge::hubKey(hub.url());
	QVERIFY(!posts.prefetches(key));

	const QVector<Qdc::BoardEntry> thread = {
		bridge_entry(ENTRY_TTH, QStringLiteral("general"), AUTHOR_CID, 412)
	};

	posts.prefetchThread(key, thread);

	/*
	 * Given a chance to happen before it is called absent. What a fetch changes is
	 * written by a completion delivered to this thread's event loop, and waiting on the
	 * core runs none of them - so a check that only blocks on the core reads back the
	 * value it read before and would say the same whether or not anything was fetched.
	 */
	QTest::qWait(120);

	QCOMPARE(posts.postState(ENTRY_TTH), Qdc::PostState::Announced);
	QVERIFY2(!queuedNow(), "a thread was prefetched from a hub nobody had named");

	/*
	 * And named, the same call does fetch it. This half is what makes the other half
	 * mean anything: without it every assertion above would hold just as well for a
	 * prefetch that never worked at all, which is the one thing they exist to tell
	 * apart from a prefetch that is off.
	 */
	posts.setPrefetch(key, true);
	QVERIFY(posts.prefetches(key));

	posts.prefetchThread(key, thread);

	QTRY_COMPARE(posts.postState(ENTRY_TTH), Qdc::PostState::Fetching);
	QTRY_VERIFY2(queuedNow(), "a thread prefetched from a named hub was not fetched");

	/*
	 * And taken out again, because the queue belongs to the core and the core outlives
	 * every case in this binary: a file left in it under a hash the other cases use is
	 * this one deciding what they find.
	 */
	QVERIFY(Qdc::Testing::runOnCore(m_thread, [this, hash] {
		QuickDC::Core* core = Qdc::Testing::core(m_thread);
		if (!core || !core->queue) return;

		if (QuickDC::QueueFile* file = core->queue->getFileByTTH(hash.constData()))
			core->queue->remove(file);
	}));

	QVERIFY(!queuedNow());
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

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [this, hash, &queued] {
		QuickDC::Core* core = Qdc::Testing::core(m_thread);
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

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [this, hidden8, &queued] {
		QuickDC::Core* core = Qdc::Testing::core(m_thread);
		if (!core || !core->queue) return;

		queued = core->queue->getFileByName(hidden8.constData()) != nullptr;
	}));

	QVERIFY(queued);

	transfers.removeFromQueue(body);
	transfers.removeFromQueue(film);

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [] { }));
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

/**
 * Opening a post reads it, the board's count falls, and dismissing the board reads the
 * rest of it.
 *
 * Read means opened by somebody in the reading pane, which is a different fact from the
 * document having been fetched: this test never fetches anything. The count is asked of
 * the bridge rather than worked out here, because answering it while a row is being
 * drawn is what it is for.
 */
void TestBridge::aPostOpenedReadsAsReadAndTheBoardsCountFalls()
{
	const QString board = QStringLiteral("general");

	Qdc::BoardBridge posts(m_thread, nullptr, boards(QStringLiteral("opened")), this);
	Qdc::HubBridge hub(m_thread, QStringLiteral("adc://192.0.2.51:412"), this);

	const QString key = Qdc::BoardBridge::hubKey(hub.url());

	posts.follow(&hub);

	QuickDC::HubListener* listener = listenerFor(hub);
	QVERIFY(listener != nullptr);

	/* Nothing has been heard of this board, which is not the same as nothing unread. */
	QCOMPARE(posts.unread(key, board), 0);

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [listener, board] {
		listener->EventBoardEntry(core_entry(ENTRY_TTH, board, AUTHOR_CID,
		                                     1786439000));
		listener->EventBoardEntry(core_entry(OTHER_TTH, board, AUTHOR_CID,
		                                     1786439662));
	}));

	QTRY_COMPARE(posts.unread(key, board), 2);

	QVERIFY(!posts.isRead(key, board, ENTRY_TTH));
	QVERIFY(!posts.isRead(key, board, OTHER_TTH));

	posts.markRead(key, board, ENTRY_TTH);

	QTRY_COMPARE(posts.unread(key, board), 1);

	QVERIFY(posts.isRead(key, board, ENTRY_TTH));
	QVERIFY2(!posts.isRead(key, board, OTHER_TTH),
	         "a post nobody opened was marked read beside one that was");

	/* And a post read twice is not read twice: nothing moves and nothing is said. */
	QSignalSpy moved(&posts, &Qdc::BoardBridge::readStateChanged);
	QVERIFY(moved.isValid());

	posts.markRead(key, board, ENTRY_TTH);

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [] { }));
	QCoreApplication::processEvents();

	QCOMPARE(moved.count(), 0);
	QCOMPARE(posts.unread(key, board), 1);

	/*
	 * Dismissing the board reads the rest of it - one watermark and a mark for each
	 * entry standing at its second, rather than a record for every post.
	 */
	posts.markAllRead(key, board);

	QTRY_COMPARE(posts.unread(key, board), 0);
	QVERIFY(posts.isRead(key, board, OTHER_TTH));
}

/**
 * Read state survives the bridge being destroyed and built again on the same directory,
 * which is the whole point of keeping it in the index rather than in a view.
 *
 * A board nobody has opened for a week has to still say so after a restart. A read set
 * held for as long as a window is open cannot answer that, and the answer it gives
 * instead - every post unread again - is wrong in the direction that makes a badge
 * useless.
 */
void TestBridge::readStateOutlivesTheBridgeThatWroteIt()
{
	const QString board = QStringLiteral("general");
	const QString directory = boards(QStringLiteral("remembered"));

	Qdc::HubBridge hub(m_thread, QStringLiteral("adc://192.0.2.52:412"), this);
	const QString key = Qdc::BoardBridge::hubKey(hub.url());

	{
		Qdc::BoardBridge posts(m_thread, nullptr, directory, this);
		posts.follow(&hub);

		QuickDC::HubListener* listener = listenerFor(hub);
		QVERIFY(listener != nullptr);

		QVERIFY(Qdc::Testing::runOnCore(m_thread, [listener, board] {
			listener->EventBoardEntry(core_entry(ENTRY_TTH, board, AUTHOR_CID,
			                                     1786439000));
			listener->EventBoardEntry(core_entry(OTHER_TTH, board, AUTHOR_CID,
			                                     1786439662));
		}));

		QTRY_COMPARE(posts.unread(key, board), 2);

		posts.markRead(key, board, ENTRY_TTH);
		QTRY_COMPARE(posts.unread(key, board), 1);
	}

	/* The store is deleted inside a job, so this is where that job has finished. */
	QVERIFY(Qdc::Testing::runOnCore(m_thread, [] { }));

	Qdc::BoardBridge again(m_thread, nullptr, directory, this);

	QSignalSpy loaded(&again, &Qdc::BoardBridge::entriesLoaded);
	QVERIFY(loaded.isValid());

	/* A bridge that has just been built has heard nothing at all yet. */
	QCOMPARE(again.unread(key, board), 0);
	QVERIFY(!again.isRead(key, board, ENTRY_TTH));

	again.loadEntries(key, board);

	QTRY_COMPARE(loaded.count(), 1);
	QCOMPARE(loaded.at(0).at(2).value<QVector<Qdc::BoardEntry>>().size(), 2);

	/*
	 * Both entries came back and one of them is still read, so the board still counts
	 * the one nobody has opened - which is what the user left it saying.
	 */
	QVERIFY2(again.isRead(key, board, ENTRY_TTH),
	         "a post read before the restart came back unread");

	QVERIFY(!again.isRead(key, board, OTHER_TTH));
	QCOMPARE(again.unread(key, board), 1);
}

/**
 * A mark that could not be written leaves the post unread, deliberately.
 *
 * Of the two ways to be wrong about this, a count that overstates asks somebody to look
 * again at a post they have already read, and one that understates hides a post nobody
 * has seen. So a read record that did not reach the disk is not pretended to have: the
 * index is asked what the post reads as afterwards rather than told.
 */
void TestBridge::aMarkThatCouldNotBeWrittenLeavesThePostUnread()
{
	const QString board = QStringLiteral("general");
	const QString directory = boards(QStringLiteral("unwritable"));

	Qdc::BoardBridge posts(m_thread, nullptr, directory, this);
	Qdc::HubBridge hub(m_thread, QStringLiteral("adc://192.0.2.53:412"), this);

	const QString key = Qdc::BoardBridge::hubKey(hub.url());

	posts.follow(&hub);

	QuickDC::HubListener* listener = listenerFor(hub);
	QVERIFY(listener != nullptr);

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [listener, board] {
		listener->EventBoardEntry(core_entry(ENTRY_TTH, board, AUTHOR_CID,
		                                     1786439000));
	}));

	QTRY_COMPARE(posts.unread(key, board), 1);

	/*
	 * The board's own index file, which is named after a digest of the hub and the
	 * board rather than after either of them - so it is found by being the one file
	 * this board wrote rather than by a name built here.
	 */
	const QDir written(QDir(directory).filePath(QStringLiteral("boards")));
	const QStringList files = written.entryList(QDir::Files);

	QCOMPARE(files.size(), 1);

	const QString index = written.filePath(files.at(0));
	QVERIFY(QFile::setPermissions(index, QFileDevice::ReadOwner));

	posts.markRead(key, board, ENTRY_TTH);

	/* The mark's job is ordered before this one, and its answer before these lines. */
	QVERIFY(Qdc::Testing::runOnCore(m_thread, [] { }));
	QCoreApplication::processEvents();

	QVERIFY2(!posts.isRead(key, board, ENTRY_TTH),
	         "a mark that could not be written was reported as read");

	QCOMPARE(posts.unread(key, board), 1);

	/*
	 * And with the file writable again the same mark goes through, which is what says
	 * the refusal above was the write failing rather than nothing having happened.
	 */
	QVERIFY(QFile::setPermissions(index, QFileDevice::ReadOwner
	                                     | QFileDevice::WriteOwner));

	posts.markRead(key, board, ENTRY_TTH);

	QTRY_COMPARE(posts.unread(key, board), 0);
	QVERIFY(posts.isRead(key, board, ENTRY_TTH));
}

/**
 * A board read back off the disk knows which of its documents are here, and does not
 * pretend to know what they came to.
 *
 * Nothing else on this side ever asks the store, so without this every post on a board
 * that is reopened - or on one that comes back after a restart - reads as though its body
 * had never been fetched, including a post written on this machine. What must not happen
 * instead is the opposite mistake: a held document being called verified. Step 4 of
 * verification compares the author the document claims against the one the hub claims,
 * that comparison was made when the document was first read, and it is written down
 * nowhere - so a post the two disagree about would come back drawn as one they agreed on.
 */
void TestBridge::aBoardReadBackKnowsWhichDocumentsItHolds()
{

	const QString board = QStringLiteral("general");
	const QString directory = boards(QStringLiteral("remembers-documents"));
	const QString hubUrl = QStringLiteral("adc://192.0.2.54:412");

	Qdc::HubBridge hub(m_thread, hubUrl, this);
	const QString key = Qdc::BoardBridge::hubKey(hub.url());

	/* One document that is the post it claims to be, and one that is not. */
	const QByteArray document =
		post_document(AUTHOR_CID, QStringLiteral("Hub upgrade on Saturday"),
		              "The hub will be offline on Saturday.\n");

	const QByteArray junk = "this hashes to its name and is not a post document\n";

	const QString readable = tth_of(document);
	const QString unreadable = tth_of(junk);

	QCOMPARE(readable.size(), 39);
	QCOMPARE(unreadable.size(), 39);

	{
		Qdc::BoardBridge posts(m_thread, nullptr, directory, this);
		posts.follow(&hub);

		QuickDC::HubListener* listener = listenerFor(hub);
		QVERIFY(listener != nullptr);

		/* Three entries: the two documents, and one whose body nobody ever fetched. */
		QVERIFY(Qdc::Testing::runOnCore(m_thread, [listener, board, readable, unreadable] {
			listener->EventBoardEntry(core_entry(readable, board, AUTHOR_CID,
			                                     1786439000));
			listener->EventBoardEntry(core_entry(unreadable, board, AUTHOR_CID,
			                                     1786439001));
			listener->EventBoardEntry(core_entry(ENTRY_TTH, board, AUTHOR_CID,
			                                     1786439002));
		}));

		QTRY_COMPARE(posts.unread(key, board), 3);

		const QString first = QDir(directory).filePath(QStringLiteral("first"));
		const QString second = QDir(directory).filePath(QStringLiteral("second"));

		QFile bytes(first);
		QVERIFY(bytes.open(QIODevice::WriteOnly));
		bytes.write(document);
		bytes.close();

		QFile other(second);
		QVERIFY(other.open(QIODevice::WriteOnly));
		other.write(junk);
		other.close();

		QSignalSpy ready(&posts, &Qdc::BoardBridge::postReady);

		/*
		 * The final one of the two failures, and not the "not this time" one: these
		 * bytes hash to the name the entry gave and are not a post document, so no
		 * copy anywhere differs and asking again would produce nothing.
		 */
		QSignalSpy refused(&posts, &Qdc::BoardBridge::postUnreadable);
		QSignalSpy later(&posts, &Qdc::BoardBridge::postUnavailable);

		posts.adopt(key, bridge_entry(readable, board, AUTHOR_CID,
		                              static_cast<quint64>(document.size())), first);

		posts.adopt(key, bridge_entry(unreadable, board, AUTHOR_CID,
		                              static_cast<quint64>(junk.size())), second);

		QTRY_COMPARE(ready.count(), 1);
		QTRY_COMPARE(refused.count(), 1);
		QCOMPARE(later.count(), 0);

		/* Both are here from the moment they were adopted, whatever they came to. */
		QVERIFY(posts.isHeld(readable));
		QVERIFY(posts.isHeld(unreadable));
		QVERIFY(!posts.isHeld(ENTRY_TTH));
	}

	/* The store is deleted inside a job, so this is where that job has finished. */
	QVERIFY(Qdc::Testing::runOnCore(m_thread, [] { }));

	Qdc::BoardBridge again(m_thread, nullptr, directory, this);

	/* A bridge that has just been built has heard nothing at all yet. */
	QVERIFY(!again.isHeld(readable));

	QSignalSpy loaded(&again, &Qdc::BoardBridge::entriesLoaded);
	QVERIFY(loaded.isValid());

	again.loadEntries(key, board);

	QTRY_COMPARE(loaded.count(), 1);
	QCOMPARE(loaded.at(0).at(2).value<QVector<Qdc::BoardEntry>>().size(), 3);

	/* What is here is here again, and the entry whose body was never fetched is not. */
	QVERIFY2(again.isHeld(readable),
	         "a document on the disk was forgotten when the board was read back");

	QVERIFY(again.isHeld(unreadable));
	QVERIFY(!again.isHeld(ENTRY_TTH));

	/*
	 * And being held is not a verdict. The document is read again on being opened and
	 * the state comes out of that reading; declaring it verified here would present a
	 * post whose author the hub and the document disagree about as one they agreed on.
	 */
	QCOMPARE(again.postState(readable), Qdc::PostState::Announced);
	QCOMPARE(again.attribution(readable), QString());

	/*
	 * The one that cannot be read is the exception, because that answer does survive:
	 * these bytes hash to the name the entry gave and are not a post, so every copy of
	 * it is these same bytes and there is nothing for a reader to ask anybody for.
	 */
	QCOMPARE(again.postState(unreadable), Qdc::PostState::Unavailable);
	QCOMPARE(again.postState(ENTRY_TTH), Qdc::PostState::Announced);

	/*
	 * And the same again for an entry that arrives rather than one read back, which is
	 * the way it actually happens: a document is held by hash and is the same document
	 * on every hub, while an index belongs to one board on one of them. So the ordinary
	 * way to meet a post whose bytes are already on this machine is as news from a second
	 * hub - and asking only on the way back from the disk left it drawn as unfetched and
	 * then fetched over the network from somebody who would be told, for a file that was
	 * already here.
	 */
	const QString elsewhere = QStringLiteral("adc://192.0.2.55:412");

	Qdc::HubBridge second(m_thread, elsewhere, this);
	const QString otherKey = Qdc::BoardBridge::hubKey(second.url());

	/*
	 * A third bridge, which has never read a board back. Asking the one above would
	 * prove nothing: it already knows what it holds from the load, so the answer would
	 * be right whether an arriving entry was checked or not.
	 */
	Qdc::BoardBridge fresh(m_thread, nullptr, directory, this);
	fresh.follow(&second);

	QVERIFY(!fresh.isHeld(readable));

	QuickDC::HubListener* other = listenerFor(second);
	QVERIFY(other != nullptr);

	QSignalSpy arrived(&fresh, &Qdc::BoardBridge::entriesStored);

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [other, board, readable, unreadable] {
		other->EventBoardEntry(core_entry(readable, board, AUTHOR_CID, 1786440000));
		other->EventBoardEntry(core_entry(unreadable, board, AUTHOR_CID, 1786440001));
		other->EventBoardEntry(core_entry(ENTRY_TTH, board, AUTHOR_CID, 1786440002));
	}));

	QTRY_VERIFY(arrived.count() >= 1);
	QTRY_COMPARE(fresh.unread(otherKey, board), 3);

	QVERIFY2(fresh.isHeld(readable),
	         "a document on the disk was not recognised in an arriving entry");

	QVERIFY(fresh.isHeld(unreadable));
	QVERIFY(!fresh.isHeld(ENTRY_TTH));

	/* And the one that can never be read says so without anybody pressing anything. */
	QTRY_COMPARE(fresh.postState(unreadable), Qdc::PostState::Unavailable);
}

/**
 * A post whose document is here is read off this disk, and nobody is asked anything.
 *
 * The rule that a body is never fetched without a reader asking is a privacy rule: the
 * request goes to whoever holds the document, which at first is only its author, so a
 * fetch nobody asked for is a census of the post's readers. Reading a file that is
 * already here is visible to nobody, so it needs no press - and it must not be able to
 * turn into a fetch, however stale this side's idea of what it holds has become.
 */
void TestBridge::aHeldDocumentIsReadWithoutAskingAnybody()
{
	const QString board = QStringLiteral("general");
	const QString directory = boards(QStringLiteral("read-from-disk"));
	const QString hubUrl = QStringLiteral("adc://192.0.2.55:412");

	Qdc::TransferBridge transfers(m_thread, this);
	Qdc::BoardBridge posts(m_thread, &transfers, directory, this);

	const QByteArray document =
		post_document(AUTHOR_CID, QStringLiteral("Hub upgrade on Saturday"),
		              "The hub will be offline on Saturday.\n");

	const QString tth = tth_of(document);
	const QString file = QDir(directory).filePath(QStringLiteral("composed"));

	QFile bytes(file);
	QVERIFY(bytes.open(QIODevice::WriteOnly));
	bytes.write(document);
	bytes.close();

	const Qdc::BoardEntry entry =
		bridge_entry(tth, board, AUTHOR_CID, static_cast<quint64>(document.size()));

	QSignalSpy ready(&posts, &Qdc::BoardBridge::postReady);
	QSignalSpy refused(&posts, &Qdc::BoardBridge::postUnavailable);
	QVERIFY(ready.isValid());

	posts.adopt(hubUrl, entry, file);

	QTRY_COMPARE(ready.count(), 1);
	QVERIFY(posts.isHeld(tth));

	/*
	 * Opened again, as a reader coming back to a post does. Fetching is momentary and is
	 * said at once rather than when the disk answers: for the moment this takes, the
	 * post must not go on saying that nothing about it has been verified.
	 */
	posts.readHeld(hubUrl, entry);
	QCOMPARE(posts.postState(tth), Qdc::PostState::Fetching);

	QTRY_COMPARE(ready.count(), 2);

	const Qdc::BoardPost post = ready.at(1).at(1).value<Qdc::BoardPost>();

	QCOMPARE(post.state, Qdc::PostState::Verified);
	QCOMPARE(posts.postState(tth), Qdc::PostState::Verified);
	QCOMPARE(post.subject, QStringLiteral("Hub upgrade on Saturday"));
	QVERIFY(post.body.startsWith(QStringLiteral("The hub will be offline")));

	/* And the document is still where it was: reading it is not adopting it again. */
	QVERIFY(QFile::exists(QDir(directory).filePath(QStringLiteral("posts/") + tth)));

	/* Nothing went out for it. This is the check that matters: the queue itself. */
	const QByteArray hash = tth.toUtf8();
	bool queued = true;

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [this, hash, &queued] {
		QuickDC::Core* core = Qdc::Testing::core(m_thread);
		if (!core || !core->queue) return;

		queued = core->queue->getFileByTTH(hash.constData()) != nullptr;
	}));

	QVERIFY2(!queued, "reading a post that is on this disk asked a peer for it");

	/*
	 * And a post whose document is not here is left alone entirely. Not refused and not
	 * fetched: nobody asked for this body, so nothing is said about it and nothing goes
	 * on the network.
	 */
	const Qdc::BoardEntry absent = bridge_entry(OTHER_TTH, board, AUTHOR_CID, 412);

	QVERIFY(!posts.isHeld(OTHER_TTH));

	posts.readHeld(hubUrl, absent);

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [] { }));
	QCoreApplication::processEvents();

	QCOMPARE(posts.postState(OTHER_TTH), Qdc::PostState::Announced);
	QCOMPARE(ready.count(), 2);
	QCOMPARE(refused.count(), 0);

	const QByteArray other = OTHER_TTH.toUtf8();
	queued = true;

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [this, other, &queued] {
		QuickDC::Core* core = Qdc::Testing::core(m_thread);
		if (!core || !core->queue) return;

		queued = core->queue->getFileByTTH(other.constData()) != nullptr;
	}));

	QVERIFY2(!queued, "opening a post nobody holds fetched it without being asked");
}

/**
 * A document that is here and is not the post it claims to be is not offered as
 * something to fetch.
 *
 * These bytes hash to the name the entry gave, so every copy of the post is these same
 * bytes and it is unreadable for ever. There is nobody to ask and nothing to try again,
 * so what a reader gets is the reason - not a button that this bridge would only refuse.
 */
void TestBridge::aDocumentKnownNotToBeThePostIsNotOfferedAsFetchable()
{
	const QString board = QStringLiteral("general");
	const QString directory = boards(QStringLiteral("never-readable"));
	const QString hubUrl = QStringLiteral("adc://192.0.2.56:412");

	Qdc::TransferBridge transfers(m_thread, this);
	Qdc::BoardBridge posts(m_thread, &transfers, directory, this);

	const QByteArray junk = "these bytes are their own name and are not a post\n";
	const QString tth = tth_of(junk);

	const QString file = QDir(directory).filePath(QStringLiteral("arrived"));

	QFile bytes(file);
	QVERIFY(bytes.open(QIODevice::WriteOnly));
	bytes.write(junk);
	bytes.close();

	const Qdc::BoardEntry entry =
		bridge_entry(tth, board, AUTHOR_CID, static_cast<quint64>(junk.size()));

	/*
	 * Reported as unreadable both times: the bytes hash to the name and are not a post,
	 * which is the answer for good rather than for now - so the reading side is told the
	 * one that takes the entry off the board and never the one that keeps it.
	 */
	QSignalSpy refused(&posts, &Qdc::BoardBridge::postUnreadable);
	QSignalSpy later(&posts, &Qdc::BoardBridge::postUnavailable);
	QVERIFY(refused.isValid());

	posts.adopt(hubUrl, entry, file);

	QTRY_COMPARE(refused.count(), 1);

	/* Kept, and remembered as what it is, so that it is not fetched and refused again. */
	QVERIFY(posts.isHeld(tth));
	QCOMPARE(posts.postState(tth), Qdc::PostState::Unavailable);

	posts.readHeld(hubUrl, entry);

	QTRY_COMPARE(refused.count(), 2);

	QVERIFY2(refused.at(1).at(1).toString().contains(
			QStringLiteral("not the post it claims to be")),
	         qPrintable(refused.at(1).at(1).toString()));

	QCOMPARE(later.count(), 0);
	QCOMPARE(posts.postState(tth), Qdc::PostState::Unavailable);

	/* And nothing was asked of anybody for it, which is the point of remembering. */
	const QByteArray hash = tth.toUtf8();
	bool queued = true;

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [this, hash, &queued] {
		QuickDC::Core* core = Qdc::Testing::core(m_thread);
		if (!core || !core->queue) return;

		queued = core->queue->getFileByTTH(hash.constData()) != nullptr;
	}));

	QVERIFY2(!queued, "a document known not to be its post was fetched again");
}

bool TestBridge::isOffered(const QByteArray& tth)
{
	bool free = false;

	Qdc::Testing::runOnCore(m_thread, [this, tth, &free] {
		QuickDC::Core* core = Qdc::Testing::core(m_thread);
		if (!core || !core->shares) return;

		free = core->shares->isSlotFreeByTTH(tth.constData());
	});

	return free;
}

void TestBridge::readAPostInAnEarlierSession(const QString& directory,
                                             const QString& address,
                                             const QString& board,
                                             const QByteArray& document)
{
	const QString key = Qdc::BoardBridge::hubKey(address);
	const QString tth = tth_of(document);
	const QByteArray hash = tth.toUtf8();

	{
		Qdc::BoardBridge posts(m_thread, nullptr, directory);
		posts.setServing(key, true);

		Qdc::HubBridge hub(m_thread, address);
		posts.follow(&hub);

		QuickDC::HubListener* listener = listenerFor(hub);
		QVERIFY(listener != nullptr);

		QSignalSpy stored(&posts, &Qdc::BoardBridge::entriesStored);
		QVERIFY(stored.isValid());

		QVERIFY(Qdc::Testing::runOnCore(m_thread, [listener, tth, board] {
			listener->EventBoardEntry(core_entry(tth, board, AUTHOR_CID, 1786439662));
		}));

		QTRY_COMPARE(stored.count(), 1);

		const QString file = QDir(directory).filePath(QStringLiteral("fetched"));

		QFile bytes(file);
		QVERIFY(bytes.open(QIODevice::WriteOnly));
		bytes.write(document);
		bytes.close();

		QSignalSpy ready(&posts, &Qdc::BoardBridge::postReady);
		QVERIFY(ready.isValid());

		posts.adopt(key, bridge_entry(tth, board, AUTHOR_CID,
		                              static_cast<quint64>(document.size())),
		            file);

		QTRY_COMPARE(ready.count(), 1);
		QCOMPARE(ready.at(0).at(1).value<Qdc::BoardPost>().state,
		         Qdc::PostState::Verified);

		/* Hashing is asynchronous, so the offer stands a moment after serve() did. */
		QTRY_VERIFY(isOffered(hash));
	}

	/* The document is on the disk, and this session's offer of it is withdrawn. */
	QVERIFY(QFile::exists(QDir(directory).filePath(QStringLiteral("posts/") + tth)));

	QVERIFY(Qdc::Testing::runOnCore(m_thread, [this, hash] {
		QuickDC::Core* core = Qdc::Testing::core(m_thread);
		if (!core || !core->shares) return;

		core->shares->detach(hash.constData());
	}));

	QVERIFY2(!isOffered(hash), "the earlier session was still offering the document");
}

/**
 * A post held from an earlier session is answered for again when the board it is on is
 * taken up, and taken up means subscribed to rather than looked at.
 *
 * An offer lasts as long as the process that made it and no longer, which is
 * deliberate: whether a post is served is the user's decision to take again at every
 * start-up rather than something a configuration file remembers on their behalf. The
 * consequence is that somebody has to take it up again, and nobody did - so a client
 * would accumulate a store full of posts, draw every one of them off its own disk, and
 * still leave the next reader to find the only other copy elsewhere. Which is the one
 * thing a board rests on not happening.
 *
 * Subscribing is the moment that matters and a pane being opened is not, because a
 * client is a source for the boards it carries rather than for the ones somebody
 * happened to open today. A client that had read a board for a year and served it only
 * on the afternoons its user clicked on it would be, for every other purpose, not a
 * source at all.
 */
void TestBridge::aPostHeldFromAnEarlierSessionIsOfferedWhenItsBoardIsTakenUp()
{
	const QString directory = boards(QStringLiteral("resumed"));
	const QString board = QStringLiteral("general");
	const QString address = QStringLiteral("adc://192.0.2.44:412");
	const QString key = Qdc::BoardBridge::hubKey(address);

	const QByteArray document =
		post_document(AUTHOR_CID, QStringLiteral("Still here"),
		              "A post outlives the session that read it.\n");

	readAPostInAnEarlierSession(directory, address, board, document);

	/* A client starting up: it subscribes from the cursor it cached, and nothing else. */
	Qdc::BoardBridge posts(m_thread, nullptr, directory, this);
	posts.setServing(key, true);

	Qdc::HubBridge hub(m_thread, address, this);
	posts.follow(&hub);

	QSignalSpy state(&posts, &Qdc::BoardBridge::boardStateReady);
	QVERIFY(state.isValid());

	posts.resume(&hub, board);

	QTRY_COMPARE(state.count(), 1);
	QCOMPARE(state.at(0).at(2).toULongLong(), 1786439662u);

	QTRY_VERIFY2(isOffered(tth_of(document).toUtf8()),
	             "a board was subscribed to and the post held on it offered to nobody");
}

/**
 * And the same when the board is read back to be drawn, which is the other way a client
 * meets what it already holds.
 *
 * Both paths, because they are reached in either order and by different clients: one
 * that starts up and subscribes without anybody opening a board, and one whose user
 * opens a board of a hub they have not joined. Neither is allowed to be the only one
 * that takes the documents up.
 */
void TestBridge::aPostHeldFromAnEarlierSessionIsOfferedWhenItsBoardIsReadBack()
{
	const QString directory = boards(QStringLiteral("reoffered"));
	const QString board = QStringLiteral("general");
	const QString address = QStringLiteral("adc://192.0.2.45:412");
	const QString key = Qdc::BoardBridge::hubKey(address);

	const QByteArray document =
		post_document(AUTHOR_CID, QStringLiteral("Read back"),
		              "The board is drawn from what is cached.\n");

	readAPostInAnEarlierSession(directory, address, board, document);

	Qdc::BoardBridge posts(m_thread, nullptr, directory, this);
	posts.setServing(key, true);

	QSignalSpy loaded(&posts, &Qdc::BoardBridge::entriesLoaded);
	QVERIFY(loaded.isValid());

	posts.loadEntries(key, board);

	QTRY_COMPARE(loaded.count(), 1);
	QCOMPARE(loaded.at(0).at(2).value<QVector<Qdc::BoardEntry>>().size(), 1);

	QTRY_VERIFY2(isOffered(tth_of(document).toUtf8()),
	             "a board was read back and the post held on it offered to nobody");
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

QTEST_GUILESS_MAIN(TestBridge)

#include "tst_bridge.moc"

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/completed.h>
#include <core/configbridge.h>
#include <core/corethread.h>
#include <core/transferbridge.h>
#include <gui/fileactions.h>
#include <gui/transfermodels.h>
#include <gui/transfersview.h>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

/**
 * Transfers: the download queue as the core keeps it, the tables over it, and the
 * history of what has finished.
 *
 * The queue is driven through the bridge against a real core, so the round trip -
 * queue a file, change its priority, take it out again - is exercised rather than
 * mocked. Nothing connects to a peer, so nothing ever actually transfers; the
 * active table is tested against its model directly.
 */
class TestTransfers : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();

	void queueRoundTripsThroughTheCore();
	void queuePriorityIsReadBack();
	void removingAnEntryCanKeepOrDeleteThePartialFile();
	void queueChangesAreAnnounced();

	void queueModelFormatsItsColumns();
	void activeModelUpdatesRowsInPlace();
	void completedModelMarksMissingFiles();

	void completedStoreRoundTrips();
	void completedStoreDropsTheOldestPastItsLimit();
	void completedStoreIgnoresAnImmediateRepeat();

	void viewShowsQueueContents();

private:
	/** Ask the bridge for fresh figures and wait for them; see the comment there. */
	QVector<Qdc::QueueEntry> snapshot();

	QString scratch(const QString& name) const;

	QTemporaryDir m_dir;
	Qdc::CoreThread* m_thread = nullptr;
	Qdc::TransferBridge* m_transfers = nullptr;
};

QString TestTransfers::scratch(const QString& name) const
{
	return QDir(m_dir.path()).filePath(name);
}

void TestTransfers::initTestCase()
{
	QVERIFY2(m_dir.isValid(), "could not create a temporary directory");

	m_thread = new Qdc::CoreThread(this);
	m_thread->setPreferenceFiles(scratch(QStringLiteral("quickdc.conf")),
	                             scratch(QStringLiteral("session.conf")),
	                             scratch(QStringLiteral("queue.xml")));

	QVERIFY2(m_thread->startAndWait(),
	         qPrintable(QStringLiteral("the core did not start: ")
	                    + m_thread->failureReason()));

	m_transfers = new Qdc::TransferBridge(m_thread, this);
}

void TestTransfers::cleanupTestCase()
{
	/* Before the thread stops: the bridge unregisters from the core as it goes. */
	delete m_transfers;
	m_transfers = nullptr;

	if (m_thread) m_thread->shutdown();
}

QVector<Qdc::QueueEntry> TestTransfers::snapshot()
{
	/*
	 * The bridge no longer reads the core when asked - it asks the core thread and
	 * reports when the answer arrives, so that no view ever waits on the reactor. A
	 * test therefore asks and waits the same way a view does.
	 */
	QSignalSpy fresh(m_transfers, &Qdc::TransferBridge::snapshotChanged);

	m_transfers->refresh();
	fresh.wait(5000);

	return m_transfers->queueSnapshot();
}

void TestTransfers::queueRoundTripsThroughTheCore()
{
	QVERIFY(snapshot().isEmpty());

	const QString target = scratch(QStringLiteral("film.mkv"));
	const QString tth = QStringLiteral("MWXFLX5NO4T54FMVTGZ6UXER2QKN6J2IGGL3W7A");

	m_transfers->enqueue(target, tth, 2097152);

	const QVector<Qdc::QueueEntry> entries = snapshot();
	QCOMPARE(entries.size(), 1);
	QCOMPARE(entries.first().filename, target);
	QCOMPARE(entries.first().tth, tth);
	QCOMPARE(entries.first().size, 2097152u);
	QCOMPARE(entries.first().done, 0u);
	QCOMPARE(entries.first().sources, 0u);
	QVERIFY(!entries.first().complete);

	/* The core returns the entry it already has rather than adding a second. */
	m_transfers->enqueue(target, tth, 2097152);
	QCOMPARE(snapshot().size(), 1);

	m_transfers->removeFromQueue(target);
	QVERIFY(snapshot().isEmpty());

	/* Removing something that is not queued is harmless. */
	m_transfers->removeFromQueue(target);
	m_transfers->removeFromQueue(QString());
}

void TestTransfers::queuePriorityIsReadBack()
{
	const QString target = scratch(QStringLiteral("priority.bin"));
	m_transfers->enqueue(target, QStringLiteral("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"),
	                     1024);

	QCOMPARE(snapshot().first().priority, Qdc::Priority::Normal);

	m_transfers->setPriority(target, Qdc::Priority::High);
	QCOMPARE(snapshot().first().priority, Qdc::Priority::High);

	/* Pausing is the bottom of the same scale, not a separate flag. */
	m_transfers->setPriority(target, Qdc::Priority::Paused);
	QCOMPARE(snapshot().first().priority, Qdc::Priority::Paused);

	/* A file that is not queued is ignored rather than added. */
	m_transfers->setPriority(scratch(QStringLiteral("nothing")), Qdc::Priority::High);
	QCOMPARE(snapshot().size(), 1);

	m_transfers->removeFromQueue(target);
}

void TestTransfers::removingAnEntryCanKeepOrDeleteThePartialFile()
{
	const QString target = scratch(QStringLiteral("partial.bin"));
	const QString partial = target + QStringLiteral(".part");

	const auto writePartial = [&partial] {
		QFile file(partial);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("half a file");
		file.close();
	};

	/* Kept by default: the entry goes, the bytes stay, and it can be requeued. */
	m_transfers->enqueue(target, QStringLiteral("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"), 4096);
	writePartial();
	QVERIFY(QFile::exists(partial));

	m_transfers->removeFromQueue(target, false);
	QVERIFY(snapshot().isEmpty());
	QVERIFY2(QFile::exists(partial),
	         "removing an entry must not throw away what was downloaded");

	/* Asked for explicitly, the partial file goes too. */
	m_transfers->enqueue(target, QStringLiteral("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"), 4096);
	m_transfers->removeFromQueue(target, true);

	QVERIFY(snapshot().isEmpty());
	QVERIFY(!QFile::exists(partial));
}

void TestTransfers::queueChangesAreAnnounced()
{
	QSignalSpy changes(m_transfers, &Qdc::TransferBridge::queueChanged);
	QVERIFY(changes.isValid());

	const QString target = scratch(QStringLiteral("announced.bin"));
	m_transfers->enqueue(target, QStringLiteral("CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC"), 512);

	/*
	 * The core reports this from inside its own queue processing, so the event is
	 * marshalled and arrives on this thread rather than immediately.
	 */
	QVERIFY(changes.wait(5000));

	const int afterAdd = changes.count();
	QVERIFY(afterAdd >= 1);

	m_transfers->removeFromQueue(target);
	QTRY_VERIFY(changes.count() > afterAdd);
}

void TestTransfers::queueModelFormatsItsColumns()
{
	Qdc::QueueEntry entry;
	entry.filename = QStringLiteral("/home/user/downloads/film.mkv");
	entry.tth = QStringLiteral("MWXFLX5NO4T54FMVTGZ6UXER2QKN6J2IGGL3W7A");
	entry.size = 1000;
	entry.done = 250;
	entry.sources = 4;
	entry.activeSources = 2;
	entry.priority = Qdc::Priority::High;

	Qdc::QueueModel model;
	model.setEntries({entry});

	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(model.columnCount(), Qdc::QueueModel::ColumnCount);

	/* The name column shows the tail; the whole path is in the tooltip. */
	QCOMPARE(model.data(model.index(0, Qdc::QueueModel::NameColumn), Qt::DisplayRole).toString(),
	         QStringLiteral("film.mkv"));
	QCOMPARE(model.data(model.index(0, Qdc::QueueModel::NameColumn),
	                    Qt::ToolTipRole).toString(), entry.filename);

	/* Both source counts, because they answer different questions. */
	QCOMPARE(model.data(model.index(0, Qdc::QueueModel::SourcesColumn), Qt::DisplayRole).toString(),
	         QStringLiteral("2 / 4"));

	QCOMPARE(model.data(model.index(0, Qdc::QueueModel::PriorityColumn), Qt::DisplayRole).toString(),
	         QStringLiteral("High"));

	/* The delegate's fraction, not a formatted string. */
	QCOMPARE(model.data(model.index(0, Qdc::QueueModel::ProgressColumn),
	                    Qdc::QueueModel::FractionRole).toDouble(), 0.25);

	/* A file of unknown size must not divide by zero. */
	Qdc::QueueEntry unknown;
	unknown.filename = QStringLiteral("mystery.bin");
	model.setEntries({unknown});

	QCOMPARE(model.data(model.index(0, Qdc::QueueModel::ProgressColumn),
	                    Qdc::QueueModel::FractionRole).toDouble(), 0.0);
	QCOMPARE(model.data(model.index(0, Qdc::QueueModel::SizeColumn), Qt::DisplayRole).toString(),
	         QStringLiteral("unknown"));
}

void TestTransfers::activeModelUpdatesRowsInPlace()
{
	Qdc::TransferInfo down;
	down.id = 1;
	down.filename = QStringLiteral("/tmp/incoming.bin");
	down.length = 1000;
	down.transferred = 100;
	down.bytesPerSecond = 2048;

	Qdc::TransferInfo up = down;
	up.id = 2;
	up.upload = true;

	Qdc::ActiveTransfersModel model;
	model.setTransfers({down, up});
	QCOMPARE(model.rowCount(), 2);

	QCOMPARE(model.data(model.index(0, Qdc::ActiveTransfersModel::DirectionColumn),
	                        Qt::DisplayRole).toString(), QStringLiteral("↓"));
	QCOMPARE(model.data(model.index(1, Qdc::ActiveTransfersModel::DirectionColumn),
	                        Qt::DisplayRole).toString(), QStringLiteral("↑"));

	QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
	QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

	/*
	 * Same rows, moved on. This must report a change rather than reset, or the
	 * selection and scroll position are thrown away once a second.
	 */
	down.transferred = 400;
	model.setTransfers({down, up});

	QCOMPARE(resets.count(), 0);
	QCOMPARE(changed.count(), 1);
	QCOMPARE(model.at(0).transferred, 400u);

	/* Nothing moved and no rate change: nothing to report. */
	model.setTransfers({down, up});
	QCOMPARE(changed.count(), 1);

	/* A rate that changed on its own still counts - it is an estimate. */
	down.bytesPerSecond = 4096;
	model.setTransfers({down, up});
	QCOMPARE(changed.count(), 2);

	/* A different number of rows is a reset. */
	model.setTransfers({down});
	QCOMPARE(resets.count(), 1);
	QCOMPARE(model.rowCount(), 1);
}

void TestTransfers::completedModelMarksMissingFiles()
{
	const QString present = scratch(QStringLiteral("kept.bin"));

	QFile file(present);
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write("here");
	file.close();

	Qdc::CompletedDownload there;
	there.filename = present;
	there.size = 4;
	there.finished = QDateTime::currentDateTime();

	Qdc::CompletedDownload gone;
	gone.filename = scratch(QStringLiteral("deleted-since.bin"));
	gone.size = 99;
	gone.finished = QDateTime::currentDateTime();

	Qdc::CompletedModel model;
	model.setEntries({there, gone});

	QVERIFY(model.data(model.index(0, 0), Qdc::CompletedModel::PresentRole).toBool());
	QVERIFY(!model.data(model.index(1, 0), Qdc::CompletedModel::PresentRole).toBool());

	/* A file that has gone says so in its tooltip rather than looking normal. */
	QVERIFY(model.data(model.index(1, 0), Qt::ToolTipRole).toString()
		.contains(QStringLiteral("no longer there")));

	QVERIFY(QFile::remove(present));
}

void TestTransfers::completedStoreRoundTrips()
{
	Qdc::ConfigBridge config(m_thread);

	/* A path containing the separator, which the encoding has to survive. */
	const QString awkward = scratch(QStringLiteral("odd|name.bin"));

	{
		Qdc::Completed completed(&config);
		QVERIFY(completed.entries().isEmpty());

		QSignalSpy changes(&completed, &Qdc::Completed::changed);

		Qdc::CompletedDownload first;
		first.filename = scratch(QStringLiteral("one.bin"));
		first.size = 1234;
		first.tth = QStringLiteral("AAA");
		first.finished = QDateTime::fromSecsSinceEpoch(1700000000);
		completed.add(first);

		Qdc::CompletedDownload second;
		second.filename = awkward;
		second.size = 5678;
		second.finished = QDateTime::fromSecsSinceEpoch(1700000100);
		completed.add(second);

		QCOMPARE(changes.count(), 2);

		/* Newest first. */
		QCOMPARE(completed.entries().first().filename, awkward);
	}

	Qdc::Completed reloaded(&config);
	QCOMPARE(reloaded.count(), 2);
	QCOMPARE(reloaded.entries().first().filename, awkward);
	QCOMPARE(reloaded.entries().first().size, 5678u);
	QCOMPARE(reloaded.entries().last().tth, QStringLiteral("AAA"));
	QCOMPARE(reloaded.entries().last().finished.toSecsSinceEpoch(), 1700000000);

	QVERIFY(reloaded.remove(awkward));
	QVERIFY(!reloaded.remove(awkward));
	QCOMPARE(reloaded.count(), 1);

	reloaded.clear();
	QCOMPARE(reloaded.count(), 0);

	/* And the clearing persisted, rather than leaving a numbered tail behind. */
	Qdc::Completed empty(&config);
	QCOMPARE(empty.count(), 0);
}

void TestTransfers::completedStoreDropsTheOldestPastItsLimit()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::Completed completed(&config);
	completed.clear();

	const int limit = Qdc::Completed::limit();

	for (int n = 0; n < limit + 5; ++n)
	{
		Qdc::CompletedDownload entry;
		entry.filename = scratch(QStringLiteral("bulk-%1.bin").arg(n));
		entry.size = static_cast<quint64>(n);
		entry.finished = QDateTime::fromSecsSinceEpoch(1700000000 + n);
		completed.add(entry);
	}

	QCOMPARE(completed.count(), limit);

	/* The newest is kept and the oldest are the ones dropped. */
	QCOMPARE(completed.entries().first().filename,
	         scratch(QStringLiteral("bulk-%1.bin").arg(limit + 4)));

	completed.clear();
}

void TestTransfers::completedStoreIgnoresAnImmediateRepeat()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::Completed completed(&config);
	completed.clear();

	Qdc::CompletedDownload entry;
	entry.filename = scratch(QStringLiteral("once.bin"));
	entry.size = 10;

	completed.add(entry);
	completed.add(entry);
	QCOMPARE(completed.count(), 1);

	/*
	 * Still one, even with something else in between: what is being suppressed is
	 * the same completion reported twice, and a different file arriving does not
	 * make the repeat any more real.
	 */
	Qdc::CompletedDownload other;
	other.filename = scratch(QStringLiteral("other.bin"));
	other.size = 20;
	completed.add(other);
	completed.add(entry);
	QCOMPARE(completed.count(), 2);

	/*
	 * The same file downloaded again later is a genuine second entry, and both
	 * have to be kept - so the suppression is on the timestamp, not the name.
	 */
	Qdc::CompletedDownload again = entry;
	again.finished = QDateTime::currentDateTime().addSecs(-86400);
	completed.add(again);
	QCOMPARE(completed.count(), 3);

	/* Ordered by the timestamp shown, so yesterday's copy is last. */
	QCOMPARE(completed.entries().last().finished.toSecsSinceEpoch(),
	         again.finished.toSecsSinceEpoch());

	/* An entry with no filename is not a download. */
	completed.add(Qdc::CompletedDownload());
	QCOMPARE(completed.count(), 3);

	completed.clear();
}

void TestTransfers::viewShowsQueueContents()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::Completed completed(&config);
	completed.clear();

	/*
	 * The tests above leave the queue empty, but the bridge reports the figures it was
	 * last handed rather than reading the core when asked - so a fresh reading is waited
	 * for before an empty table means anything.
	 */
	QVERIFY(snapshot().isEmpty());

	Qdc::TransfersView view(m_transfers, &completed);

	QCOMPARE(view.currentTab(), 0);
	QTRY_COMPARE(view.queueRowCount(), 0);
	QCOMPARE(view.activeRowCount(), 0);
	QCOMPARE(view.completedRowCount(), 0);

	const QString target = scratch(QStringLiteral("shown.bin"));
	m_transfers->enqueue(target, QStringLiteral("DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"),
	                     8192);

	/* The queue's own change event reaches the table. */
	QTRY_COMPARE(view.queueRowCount(), 1);

	/* A completion recorded by the store reaches the Completed tab. */
	Qdc::CompletedDownload entry;
	entry.filename = target;
	entry.size = 8192;
	completed.add(entry);

	QCOMPARE(view.completedRowCount(), 1);

	view.setCurrentTab(2);
	QCOMPARE(view.currentTab(), 2);

	m_transfers->removeFromQueue(target);
	QTRY_COMPARE(view.queueRowCount(), 0);

	completed.clear();
}

QTEST_MAIN(TestTransfers)

#include "tst_transfers.moc"

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

	void chunksOfOneFileFoldIntoOneTransfer();
	void aFoldedDownloadTakesItsProgressFromTheQueue();
	void aRowIsHeldWhileTheFileIsBetweenChunks();
	void aRowKeepsItsPlaceWhateverOrderTheChunksAreIn();
	void theRateIsWhatArrivedBetweenTwoReadings();
	void chunksComingAndGoingDoNotResetTheTable();
	void aFileBeingTransferredIsNotAlsoWaiting();
	void aReadingTakenAsAChunkEndsIsNotAnIdleTransfer();

	void completedStoreRoundTrips();
	void completedStoreDropsTheOldestPastItsLimit();
	void completedStoreIgnoresAnImmediateRepeat();

	void viewShowsQueueContents();
	void theBadgeCountsFilesAndNotChunks();
	void aRecordedNameReachesTheFileItNames();

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

void TestTransfers::chunksOfOneFileFoldIntoOneTransfer()
{
	/*
	 * What the core reports while a big file is being fetched: one transfer per
	 * chunk, each writing into the same ".part", each with its own range and rate.
	 */
	Qdc::TransferInfo first;
	first.id = 1;
	first.filename = QStringLiteral("/tmp/big.iso.part");
	first.transferred = 100;
	first.length = 1000;
	first.bytesPerSecond = 500;

	Qdc::TransferInfo second = first;
	second.id = 2;
	second.transferred = 250;
	second.bytesPerSecond = 1500;

	Qdc::TransferInfo third = first;
	third.id = 3;
	third.transferred = 50;
	third.bytesPerSecond = 1000;
	third.finished = true;

	/* The same file going the other way is a different thing being moved. */
	Qdc::TransferInfo out;
	out.id = 4;
	out.filename = QStringLiteral("/tmp/big.iso");
	out.transferred = 7;
	out.length = 1000;
	out.bytesPerSecond = 70;
	out.upload = true;

	/* And so is another file altogether. */
	Qdc::TransferInfo other;
	other.id = 5;
	other.filename = QStringLiteral("/tmp/small.bin.part");
	other.transferred = 5;
	other.length = 10;

	const QVector<Qdc::TransferInfo> folded =
		Qdc::TransferBridge::foldByFile({first, second, third, out, other}, {});

	QCOMPARE(folded.size(), 3);

	/* Rows keep the order the files were first seen in. */
	const Qdc::TransferInfo& download = folded.at(0);

	/* Named as the file it will be, not as the ".part" it is being written to. */
	QCOMPARE(download.filename, QStringLiteral("/tmp/big.iso"));
	QCOMPARE(download.chunks, 3u);
	QCOMPARE(download.transferred, 400u);
	QCOMPARE(download.length, 3000u);

	/* The rate is what the file is arriving at, which is all of them together. */
	QCOMPARE(download.bytesPerSecond, 3000u);

	/* One chunk being done does not make the file done. */
	QVERIFY(!download.finished);
	QVERIFY(!download.upload);

	QCOMPARE(folded.at(1).filename, QStringLiteral("/tmp/big.iso"));
	QVERIFY(folded.at(1).upload);
	QCOMPARE(folded.at(1).chunks, 1u);

	QCOMPARE(folded.at(2).filename, QStringLiteral("/tmp/small.bin"));

	/*
	 * The keys stand for the file, so a row keeps its identity from one reading to
	 * the next even as the chunks underneath it come and go - which is what stops
	 * the table resetting, and with it the selection.
	 */
	const QVector<Qdc::TransferInfo> again =
		Qdc::TransferBridge::foldByFile({third, first}, {});

	QCOMPARE(again.size(), 1);
	QCOMPARE(again.at(0).id, download.id);
	QVERIFY(download.id != folded.at(1).id);
	QVERIFY(download.id != folded.at(2).id);

	/* A transfer with no file to name cannot be folded with another one. */
	Qdc::TransferInfo nameless;
	nameless.id = 6;
	Qdc::TransferInfo alsoNameless;
	alsoNameless.id = 7;

	QCOMPARE(Qdc::TransferBridge::foldByFile({nameless, alsoNameless}, {}).size(), 2);
}

void TestTransfers::aFoldedDownloadTakesItsProgressFromTheQueue()
{
	Qdc::TransferInfo chunk;
	chunk.id = 1;
	chunk.filename = QStringLiteral("/tmp/big.iso.part");
	chunk.transferred = 300;
	chunk.length = 1000;

	Qdc::QueueEntry entry;
	entry.filename = QStringLiteral("/tmp/big.iso");
	entry.size = 100000;
	entry.done = 40000;

	const QVector<Qdc::TransferInfo> folded =
		Qdc::TransferBridge::foldByFile({chunk}, {entry});

	QCOMPARE(folded.size(), 1);

	/*
	 * The chunks in flight cover a kilobyte of a hundred; what a row standing for
	 * the file has to show is how much of the file is there - which is what the
	 * queue has committed, plus what the chunks have pulled in since. The queue
	 * counts a chunk only once it is whole, so on its own it steps a chunk at a
	 * time.
	 */
	QCOMPARE(folded.at(0).transferred, 40000u + 300u);
	QCOMPARE(folded.at(0).length, 100000u);

	/* An upload is not in the queue, so a like-named entry is not its progress. */
	Qdc::TransferInfo upload = chunk;
	upload.filename = QStringLiteral("/tmp/big.iso");
	upload.upload = true;

	const QVector<Qdc::TransferInfo> outgoing =
		Qdc::TransferBridge::foldByFile({upload}, {entry});

	QCOMPARE(outgoing.at(0).transferred, 300u);
	QCOMPARE(outgoing.at(0).length, 1000u);

	/* Neither is a queue entry that says nothing about how big the file is. */
	Qdc::QueueEntry sizeless = entry;
	sizeless.size = 0;
	sizeless.done = 0;

	const QVector<Qdc::TransferInfo> unknown =
		Qdc::TransferBridge::foldByFile({chunk}, {sizeless});

	QCOMPARE(unknown.at(0).transferred, 300u);
	QCOMPARE(unknown.at(0).length, 1000u);
}

void TestTransfers::aRowIsHeldWhileTheFileIsBetweenChunks()
{
	Qdc::TransferInfo chunk;
	chunk.id = 1;
	chunk.filename = QStringLiteral("/tmp/held.iso.part");
	chunk.transferred = 100;
	chunk.length = 1000;
	chunk.bytesPerSecond = 4096;

	Qdc::QueueEntry entry;
	entry.filename = QStringLiteral("/tmp/held.iso");
	entry.size = 100000;
	entry.done = 20000;

	const QVector<Qdc::TransferInfo> moving =
		Qdc::TransferBridge::foldByFile({chunk}, {entry});

	QCOMPARE(moving.size(), 1);
	QCOMPARE(moving.at(0).chunks, 1u);
	QCOMPARE(moving.at(0).idleMs, 0);

	/*
	 * The chunk is answered and the next one is a round trip away, so the core has
	 * nothing in flight for this file. The row has to stay: it vanishing and coming
	 * back a second later is the flicker this exists to stop.
	 */
	const QVector<Qdc::TransferInfo> gap =
		Qdc::TransferBridge::foldByFile({}, {entry}, moving, 1000);

	QCOMPARE(gap.size(), 1);
	QCOMPARE(gap.at(0).id, moving.at(0).id);
	QCOMPARE(gap.at(0).chunks, 0u);
	QCOMPARE(gap.at(0).idleMs, 1000);

	/*
	 * And the rate fades rather than blinking out. Nothing arrived, so the reading
	 * is nothing, but it is averaged with the one before it - an empty rate draws
	 * as an empty cell, and snapping to it would only move the flicker into that
	 * column.
	 */
	QCOMPARE(gap.at(0).bytesPerSecond, 2048u);

	const QVector<Qdc::TransferInfo> stalled =
		Qdc::TransferBridge::foldByFile({}, {entry}, gap, 1000);

	QCOMPARE(stalled.size(), 1);
	QCOMPARE(stalled.at(0).bytesPerSecond, 1024u);
	QCOMPARE(stalled.at(0).idleMs, 2000);

	/* Given up on once it has been quiet for longer than the gap could be. */
	QVERIFY(Qdc::TransferBridge::foldByFile({}, {entry}, stalled, 5000).isEmpty());

	/* The next chunk arriving picks the same row up again. */
	const QVector<Qdc::TransferInfo> again =
		Qdc::TransferBridge::foldByFile({chunk}, {entry}, stalled, 1000);

	QCOMPARE(again.size(), 1);
	QCOMPARE(again.at(0).id, moving.at(0).id);
	QCOMPARE(again.at(0).chunks, 1u);
	QCOMPARE(again.at(0).idleMs, 0);

	/*
	 * Held only while the queue still wants the file. Off the queue means it
	 * finished or was taken out, and either way there is nothing to wait for.
	 */
	QVERIFY(Qdc::TransferBridge::foldByFile({}, {}, moving, 100).isEmpty());

	Qdc::QueueEntry done = entry;
	done.complete = true;
	QVERIFY(Qdc::TransferBridge::foldByFile({}, {done}, moving, 100).isEmpty());

	/*
	 * An upload is not in the queue, so there is nothing to consult and the grace
	 * decides on its own.
	 */
	Qdc::TransferInfo out = chunk;
	out.filename = QStringLiteral("/tmp/held.iso");
	out.upload = true;

	const QVector<Qdc::TransferInfo> sending =
		Qdc::TransferBridge::foldByFile({out}, {});

	QCOMPARE(Qdc::TransferBridge::foldByFile({}, {}, sending, 1000).size(), 1);
	QVERIFY(Qdc::TransferBridge::foldByFile({}, {}, sending, 5000).isEmpty());
}

void TestTransfers::theRateIsWhatArrivedBetweenTwoReadings()
{
	Qdc::QueueEntry entry;
	entry.filename = QStringLiteral("/tmp/measured.iso");
	entry.size = 1000 * 1000 * 1000;
	entry.done = 0;

	Qdc::TransferInfo chunk;
	chunk.id = 1;
	chunk.filename = QStringLiteral("/tmp/measured.iso.part");
	chunk.length = 16 * 1024 * 1024;

	/*
	 * The core estimates a rate per transfer, and a transfer is one chunk, so at
	 * twenty five megabytes a second those estimates are each a fraction of a
	 * second old and read as almost nothing. What arrived between two readings does
	 * not care where the chunk boundaries fell, so that is what is measured.
	 */
	chunk.bytesPerSecond = 3;

	QVector<Qdc::TransferInfo> rows =
		Qdc::TransferBridge::foldByFile({chunk}, {entry});
	QCOMPARE(rows.size(), 1);

	/* A megabyte a second, from a standing start: half of it after one reading. */
	for (int n = 0; n < 8; ++n)
	{
		entry.done += 1000 * 1000;
		rows = Qdc::TransferBridge::foldByFile({chunk}, {entry}, rows, 1000);
	}

	QCOMPARE(rows.size(), 1);
	QVERIFY2(rows.at(0).bytesPerSecond > 900u * 1000
	         && rows.at(0).bytesPerSecond < 1100u * 1000,
	         qPrintable(QStringLiteral("rate came out at %1")
	                    .arg(rows.at(0).bytesPerSecond)));

	/* Two readings in the same instant say nothing; the figure stands. */
	const quint64 settled = rows.at(0).bytesPerSecond;
	rows = Qdc::TransferBridge::foldByFile({chunk}, {entry}, rows, 0);
	QCOMPARE(rows.at(0).bytesPerSecond, settled);

	/*
	 * A chunk that failed has its bytes thrown away, so progress can go backwards.
	 * That is not a negative rate.
	 */
	entry.done -= 1000 * 1000;
	rows = Qdc::TransferBridge::foldByFile({chunk}, {entry}, rows, 1000);
	QCOMPARE(rows.at(0).bytesPerSecond, settled / 2);
}

void TestTransfers::aRowKeepsItsPlaceWhateverOrderTheChunksAreIn()
{
	Qdc::TransferInfo first;
	first.id = 1;
	first.filename = QStringLiteral("/tmp/one.bin.part");
	first.length = 10;

	Qdc::TransferInfo second;
	second.id = 2;
	second.filename = QStringLiteral("/tmp/two.bin.part");
	second.length = 10;

	const QVector<Qdc::TransferInfo> before =
		Qdc::TransferBridge::foldByFile({first, second}, {});

	QCOMPARE(before.size(), 2);
	QCOMPARE(before.at(0).filename, QStringLiteral("/tmp/one.bin"));

	/*
	 * The core reports its transfers in the order of a vector that chunks are
	 * added to and taken out of, so the same two files can arrive either way
	 * round. Rows that swapped places every reading looked like flicker too.
	 */
	const QVector<Qdc::TransferInfo> after =
		Qdc::TransferBridge::foldByFile({second, first}, {}, before, 1000);

	QCOMPARE(after.size(), 2);
	QCOMPARE(after.at(0).filename, QStringLiteral("/tmp/one.bin"));
	QCOMPARE(after.at(1).filename, QStringLiteral("/tmp/two.bin"));

	/* Something genuinely new goes at the end rather than into the middle. */
	Qdc::TransferInfo third;
	third.id = 3;
	third.filename = QStringLiteral("/tmp/three.bin.part");
	third.length = 10;

	const QVector<Qdc::TransferInfo> grown =
		Qdc::TransferBridge::foldByFile({third, second, first}, {}, after, 1000);

	QCOMPARE(grown.size(), 3);
	QCOMPARE(grown.at(0).filename, QStringLiteral("/tmp/one.bin"));
	QCOMPARE(grown.at(1).filename, QStringLiteral("/tmp/two.bin"));
	QCOMPARE(grown.at(2).filename, QStringLiteral("/tmp/three.bin"));
}

void TestTransfers::chunksComingAndGoingDoNotResetTheTable()
{
	Qdc::QueueEntry entry;
	entry.filename = QStringLiteral("/tmp/steady.iso");
	entry.size = 100000;
	entry.done = 1000;

	Qdc::TransferInfo chunk;
	chunk.filename = QStringLiteral("/tmp/steady.iso.part");
	chunk.length = 1000;
	chunk.bytesPerSecond = 2048;

	Qdc::ActiveTransfersModel model;

	QVector<Qdc::TransferInfo> rows =
		Qdc::TransferBridge::foldByFile({chunk}, {entry});
	model.setTransfers(rows);
	QCOMPARE(model.rowCount(), 1);

	QSignalSpy resets(&model, &QAbstractItemModel::modelReset);

	/*
	 * A file arriving in chunks, as the core reports it: one moving, then a gap
	 * while the next is asked for, then another one - with a different address, so
	 * a different id, each time. None of that may reach the table as a reset, or
	 * the selection and the scroll position go with it.
	 */
	for (int round = 0; round < 6; ++round)
	{
		entry.done += 1000;

		/* The gap between one chunk and the next. */
		rows = Qdc::TransferBridge::foldByFile({}, {entry}, rows, 200);
		model.setTransfers(rows);
		QCOMPARE(model.rowCount(), 1);

		chunk.id = static_cast<quintptr>(100 + round);
		rows = Qdc::TransferBridge::foldByFile({chunk}, {entry}, rows, 200);
		model.setTransfers(rows);
		QCOMPARE(model.rowCount(), 1);
	}

	QCOMPARE(resets.count(), 0);

	/* And the row is the file throughout, with the file's own progress on it. */
	QCOMPARE(model.at(0).filename, QStringLiteral("/tmp/steady.iso"));
	QCOMPARE(model.at(0).transferred, entry.done);
	QCOMPARE(model.at(0).length, 100000u);
}

void TestTransfers::aReadingTakenAsAChunkEndsIsNotAnIdleTransfer()
{
	Qdc::QueueEntry entry;
	entry.filename = QStringLiteral("/tmp/steady.iso");
	entry.size = 4000ull * 1024 * 1024;
	entry.done = 0;

	Qdc::TransferInfo chunk;
	chunk.id = 1;
	chunk.filename = QStringLiteral("/tmp/steady.iso.part");
	chunk.length = 1024 * 1024;

	QVector<Qdc::TransferInfo> rows =
		Qdc::TransferBridge::foldByFile({chunk}, {entry});
	QCOMPARE(rows.size(), 1);

	/*
	 * Every reading here is taken in the moment a chunk has just finished - which
	 * is what actually happens, because a transfer ending is one of the things that
	 * asks for a reading. Nothing is in flight at any of them, but a megabyte
	 * arrived each time, so this is a download running perfectly well and the row
	 * has to stay for all of it.
	 *
	 * Counting only what was in flight made the row look idle at every one of these
	 * and gave up on it after the grace, which is a badge and a table row blinking
	 * out in the middle of a download.
	 */
	for (int n = 0; n < 200; ++n)
	{
		entry.done += 1024 * 1024;

		rows = Qdc::TransferBridge::foldByFile({}, {entry}, rows, 40);

		QVERIFY2(rows.size() == 1,
		         qPrintable(QStringLiteral("the row was given up on at reading %1")
		                    .arg(n)));
		QCOMPARE(rows.at(0).idleMs, 0);
	}

	/* Eight seconds of that, and it is still one transfer of one file. */
	QCOMPARE(rows.at(0).filename, QStringLiteral("/tmp/steady.iso"));
	QCOMPARE(rows.at(0).chunks, 0u);

	/* Nothing arriving is a different matter, and still runs out of patience. */
	for (int n = 0; n < 4; ++n)
		rows = Qdc::TransferBridge::foldByFile({}, {entry}, rows, 1000);

	QVERIFY(rows.isEmpty());
}

void TestTransfers::aFileBeingTransferredIsNotAlsoWaiting()
{
	Qdc::QueueEntry moving;
	moving.filename = QStringLiteral("/tmp/moving.iso");
	moving.size = 1000;

	Qdc::QueueEntry waiting;
	waiting.filename = QStringLiteral("/tmp/waiting.iso");
	waiting.size = 1000;

	Qdc::TransferInfo row;
	row.filename = QStringLiteral("/tmp/moving.iso");

	const QVector<Qdc::QueueEntry> marked =
		Qdc::TransferBridge::markTransferring({moving, waiting}, {row});

	QCOMPARE(marked.size(), 2);
	QVERIFY(marked.at(0).transferring);
	QVERIFY(!marked.at(1).transferring);

	/*
	 * Held rows count too. A file between chunks is still being transferred, and an
	 * entry that changed tabs every time a chunk ended would be the same flicker in
	 * another place.
	 */
	Qdc::TransferInfo quiet = row;
	quiet.chunks = 0;
	quiet.idleMs = 1500;

	QVERIFY(Qdc::TransferBridge::markTransferring({moving}, {quiet}).at(0).transferring);

	/* Sending somebody a file says nothing about one being fetched. */
	Qdc::TransferInfo out = row;
	out.upload = true;

	QVERIFY(!Qdc::TransferBridge::markTransferring({moving}, {out}).at(0).transferring);

	/* And a row with no name cannot be matched against anything. */
	Qdc::TransferInfo nameless;

	QVERIFY(!Qdc::TransferBridge::markTransferring({moving}, {nameless})
		.at(0).transferring);

	/* Nothing moving leaves the queue as it was. */
	QVERIFY(!Qdc::TransferBridge::markTransferring({moving}, {}).at(0).transferring);
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

void TestTransfers::theBadgeCountsFilesAndNotChunks()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::Completed completed(&config);
	completed.clear();

	Qdc::TransfersView view(m_transfers, &completed);
	QCOMPARE(view.badgeCount(), 0);

	/*
	 * The core's events are one per chunk. Three of them moving the same file is
	 * one thing being downloaded, and that is what the rail is asked to show.
	 */
	Qdc::TransferInfo chunk;
	chunk.id = 11;
	chunk.filename = QStringLiteral("/tmp/badged.iso.part");

	emit m_transfers->transferStarted(chunk);
	QCOMPARE(view.badgeCount(), 1);

	chunk.id = 12;
	emit m_transfers->transferStarted(chunk);
	chunk.id = 13;
	emit m_transfers->transferStarted(chunk);
	QCOMPARE(view.badgeCount(), 1);

	/* A second file is a second thing. */
	Qdc::TransferInfo elsewhere;
	elsewhere.id = 21;
	elsewhere.filename = QStringLiteral("/tmp/other.bin.part");

	emit m_transfers->transferStarted(elsewhere);
	QCOMPARE(view.badgeCount(), 2);

	/* One chunk of the file ending leaves the file itself still going. */
	chunk.id = 12;
	emit m_transfers->transferFinished(chunk);
	QCOMPARE(view.badgeCount(), 2);

	chunk.id = 11;
	emit m_transfers->transferStopped(chunk);
	chunk.id = 13;
	emit m_transfers->transferError(chunk);
	QCOMPARE(view.badgeCount(), 1);

	emit m_transfers->transferFinished(elsewhere);
	QCOMPARE(view.badgeCount(), 0);

	completed.clear();
}

void TestTransfers::aRecordedNameReachesTheFileItNames()
{
	/*
	 * The core records a download under the name it was configured with, and the default
	 * download directory begins with a tilde. Every action on a finished download - open
	 * it, show it, delete it, and deciding whether a picture in chat can be drawn - goes
	 * through here, and all of them answered no for as long as the tilde was passed to Qt
	 * unexpanded.
	 */
	QCOMPARE(Qdc::FileActions::resolve(QStringLiteral("~")), QDir::homePath());
	QCOMPARE(Qdc::FileActions::resolve(QStringLiteral("~/.quickdc/downloads/film.mkv")),
	         QDir::homePath() + QStringLiteral("/.quickdc/downloads/film.mkv"));

	/* An absolute path is already where it says it is. */
	QCOMPARE(Qdc::FileActions::resolve(QStringLiteral("/tmp/film.mkv")),
	         QStringLiteral("/tmp/film.mkv"));

	/* A tilde inside a name is part of the name, not a home directory. */
	QCOMPARE(Qdc::FileActions::resolve(QStringLiteral("/tmp/~backup")),
	         QStringLiteral("/tmp/~backup"));

	QCOMPARE(Qdc::FileActions::resolve(QString()), QString());

	/* And a real file is found through the tilde form. */
	const QString made = scratch(QStringLiteral("recorded.bin"));
	QFile file(made);
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write("here");
	file.close();

	QVERIFY(Qdc::FileActions::exists(made));
	QVERIFY(!Qdc::FileActions::exists(QStringLiteral("~/.quickdc/nothing-here.bin")));
}

QTEST_MAIN(TestTransfers)

#include "tst_transfers.moc"

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_TRANSFERBRIDGE_H
#define HAVE_QUICKDC_QUI_TRANSFERBRIDGE_H

#include "coretypes.h"

#include <QElapsedTimer>
#include <QObject>
#include <QVector>

namespace Qdc {

class CoreThread;
class QueueReporter;
class TransferReporter;

/**
 * The transfer engine and the download queue, as a view sees it.
 *
 * Lives on the thread that constructed it. The core side half is a
 * TransferReporter registered with the core's transfer manager, added and
 * removed inside jobs on the core thread.
 *
 * The two halves of a transfer view are served differently, because the core
 * serves them differently:
 *
 *   comings and goings   pushed. transferStarted() and the three ways a
 *                        transfer can end are all real events from the core.
 *   rates and progress   pulled. QuickDC::TransferListener declares an
 *                        EventTransferProgress that nothing ever emits, so
 *                        activeTransfers() polls instead. A view wanting a live
 *                        rate column drives it from a timer of its own; there is
 *                        no point going faster than the core's own quarter
 *                        second turn.
 */
class TransferBridge final : public QObject {
	Q_OBJECT

public:
	explicit TransferBridge(CoreThread* thread, QObject* parent = nullptr);
	~TransferBridge() override;

	/**
	 * Every transfer in flight, as of the last snapshot, one row per file.
	 *
	 * Returns immediately: these are the figures the core thread last handed over,
	 * not a fresh reading. Nothing here waits for the core, because the core thread
	 * is busy for as long as its current turn takes - a transfer moving chunks, a
	 * hash being collected, a name being looked up - and an interface that waited
	 * for that stopped drawing for exactly as long.
	 *
	 * Ask for new figures with refresh() and draw again when snapshotChanged() says
	 * they have arrived. Empty until the first snapshot completes.
	 *
	 * The chunks a file is arriving in are folded together here rather than in one
	 * of the views, because every one of them counts these: a table of what is
	 * moving, the badge on the rail, and the summary on the home page would each
	 * have had to know that three rows can mean one download.
	 */
	QVector<TransferInfo> activeTransfers() const;

	/**
	 * Fold the core's transfers into one row per file, and hold that row steady.
	 *
	 * Chunks of the same file going the same way become a single row: their rates
	 * are added, and 'chunks' counts how many there were. A download's progress is
	 * taken from its queue entry rather than from the chunks, because the chunks
	 * only cover the ranges being fetched at this moment and the queue knows how
	 * much of the whole file is there.
	 *
	 * Folding alone is not enough to make a row stand still, which is what the last
	 * two arguments are for. A chunk is a request of its own, so between one
	 * finishing and the next being answered there is nothing moving for the file at
	 * all - and a reading taken in that moment used to find no row for it, so the
	 * row vanished and came back a second later. Every figure the core keeps behaves
	 * that way, the queue's own count of active sources included, because they are
	 * all counting requests. So a row that was there and has nothing moving now is
	 * held for a few seconds before it is given up on, and it keeps the rate it last
	 * had - a rate of nothing draws as an empty cell, so zeroing it moved the flicker
	 * into that column instead of curing it.
	 *
	 * Rows also keep the order they were first seen in, for the same reason: the
	 * core's own order follows a vector that chunks are added to and taken out of,
	 * so rows swapped places from one reading to the next.
	 *
	 * @param chunks what the core reported, one entry per Transfer.
	 * @param queued the queue as of the same snapshot, for those progress figures,
	 *        and to tell a download between chunks from one that has finished or
	 *        been taken out. Uploads are not in it and are held on the grace alone.
	 * @param previous what this returned for the reading before, or nothing.
	 * @param sinceMs how long ago that reading was taken, in milliseconds.
	 *
	 * Static and public because it is worth testing on its own, and because the
	 * rule it implements - what counts as "the same file" - is one that a caller
	 * comparing a row against a live event has to agree with.
	 */
	static QVector<TransferInfo> foldByFile(const QVector<TransferInfo>& chunks,
	                                        const QVector<QueueEntry>& queued,
	                                        const QVector<TransferInfo>& previous = {},
	                                        int sinceMs = 0);

	/**
	 * The download queue, from the same snapshot, whole.
	 *
	 * Includes the files being transferred right now, each with
	 * QueueEntry::transferring set. waitingEntries() is the rest of it.
	 *
	 * Empty both when nothing is queued and when the core was built without a
	 * queue file, which the lighter constructor allows.
	 */
	QVector<QueueEntry> queueSnapshot() const;

	/**
	 * The queue minus whatever is being transferred at the moment.
	 *
	 * What is still waiting, in other words. A file being fetched is in the queue
	 * until it is complete - that is what the queue is for - but showing it in a
	 * list of what is waiting as well as in a list of what is moving says the same
	 * thing twice, so the two are kept disjoint.
	 */
	QVector<QueueEntry> waitingEntries() const;

	/**
	 * Flag the queue entries that the given rows are transferring.
	 *
	 * Matched on the local filename, which is what a folded download row is named
	 * after. Uploads are not consulted: a file being sent to somebody has nothing
	 * to do with one being fetched, even where both are the same file.
	 *
	 * Static and public for the same reasons as foldByFile(), whose output it is
	 * meant to be given.
	 */
	static QVector<QueueEntry> markTransferring(const QVector<QueueEntry>& queued,
	                                            const QVector<TransferInfo>& active);

	/** How many queued files are being fetched from someone right now. */
	quint32 activeTransferCount() const;

	/**
	 * Ask the core thread for fresh figures.
	 *
	 * Returns at once. One reading is in flight at a time, so calling this from a
	 * timer faster than the core can answer costs nothing extra.
	 */
	void refresh();

public slots:
	/**
	 * Change a queued file's priority.
	 *
	 * Pausing is a priority rather than a separate state, which is the core's own
	 * model: the scheduler skips PrioPaused and starts nothing for it.
	 *
	 * @param filename the local filename, which is what identifies a queue entry -
	 *        QueueEntry::filename carries it.
	 */
	void setPriority(const QString& filename, Qdc::Priority priority);

	/**
	 * Take a file out of the queue.
	 *
	 * @param deletePartial also delete the incomplete ".part" file. Off by
	 *        default: removing an entry is easy to do by accident and throwing
	 *        away a half-finished download with it should be asked for.
	 */
	void removeFromQueue(const QString& filename, bool deletePartial = false);

	/**
	 * Queue a file for download.
	 *
	 * Sources are what actually make it downloadable, and this adds none - the
	 * entry sits there until the queue finds somewhere to get it from, which for a
	 * file with a TTH it does by searching. Without a TTH nothing will ever match
	 * it.
	 */
	void enqueue(const QString& filename, const QString& tth, quint64 size);

signals:
	void transferStarted(const Qdc::TransferInfo& transfer);

	/**
	 * The queue gained or lost an entry. Carries nothing: a table wants the whole
	 * snapshot anyway, and the events arrive in bursts as the queue is processed.
	 *
	 * A fresh reading is asked for when this happens, so the figures behind
	 * queueSnapshot() are updated a moment later - see snapshotChanged().
	 */
	void queueChanged();

	/**
	 * New figures have arrived and the accessors above will report them.
	 *
	 * Emitted whether or not anything differs, so a view may simply draw again.
	 */
	void snapshotChanged();

	/**
	 * A download finished, matched its hash, and is now at 'filename'.
	 *
	 * The only notification that a download succeeded - see QuickDC::QueueListener
	 * in the core for why this is separate from the entry leaving the queue.
	 */
	void downloadCompleted(const QString& filename, quint64 size,
	                       const QString& tth);

	/** Every byte asked for arrived. */
	void transferFinished(const Qdc::TransferInfo& transfer);

	/** Ended early, with no error: the connection went away. */
	void transferStopped(const Qdc::TransferInfo& transfer);

	/** Ended on an I/O failure; TransferInfo says how far it got. */
	void transferError(const Qdc::TransferInfo& transfer);

private:
	friend class QueueReporter;

	CoreThread* m_thread;

	/** Owned by the core thread; only touched from a job there. */
	TransferReporter* m_reporter = nullptr;
	QueueReporter* m_queueReporter = nullptr;

	/** The last figures the core thread handed over; see activeTransfers(). */
	QVector<TransferInfo> m_active;

	/**
	 * Since the last reading was folded, which is how long a row with nothing
	 * moving has been quiet. Measured here rather than assumed from the poll
	 * interval, because a view is free to ask whenever it likes - and does, every
	 * time a transfer ends.
	 */
	QElapsedTimer m_taken;
	QVector<QueueEntry> m_queue;
	quint32 m_activeCount = 0;

	/** Whether a reading has been asked for and not yet arrived. */
	bool m_reading = false;
};

}

#endif // HAVE_QUICKDC_QUI_TRANSFERBRIDGE_H

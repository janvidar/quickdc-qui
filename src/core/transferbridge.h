/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_TRANSFERBRIDGE_H
#define HAVE_QUICKDC_QUI_TRANSFERBRIDGE_H

#include "coretypes.h"

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
	 * Every transfer in flight, as of the last snapshot.
	 *
	 * Returns immediately: these are the figures the core thread last handed over,
	 * not a fresh reading. Nothing here waits for the core, because the core thread
	 * is busy for as long as its current turn takes - a transfer moving chunks, a
	 * hash being collected, a name being looked up - and an interface that waited
	 * for that stopped drawing for exactly as long.
	 *
	 * Ask for new figures with refresh() and draw again when snapshotChanged() says
	 * they have arrived. Empty until the first snapshot completes.
	 */
	QVector<TransferInfo> activeTransfers() const;

	/**
	 * The download queue, from the same snapshot.
	 *
	 * Empty both when nothing is queued and when the core was built without a
	 * queue file, which the lighter constructor allows.
	 */
	QVector<QueueEntry> queueSnapshot() const;

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
	QVector<QueueEntry> m_queue;
	quint32 m_activeCount = 0;

	/** Whether a reading has been asked for and not yet arrived. */
	bool m_reading = false;
};

}

#endif // HAVE_QUICKDC_QUI_TRANSFERBRIDGE_H

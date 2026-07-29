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
	 * Every transfer in flight, taken in one pass on the core thread.
	 *
	 * Blocks until that pass completes, which is a job's turn away rather than a
	 * reactor turn away, so it is fine to call from a repeating timer. Empty if
	 * the core is not running.
	 */
	QVector<TransferInfo> activeTransfers() const;

	/**
	 * The download queue, likewise.
	 *
	 * Empty both when nothing is queued and when the core was built without a
	 * queue file, which the lighter constructor allows.
	 */
	QVector<QueueEntry> queueSnapshot() const;

	/** How many queued files are being fetched from someone right now. */
	quint32 activeTransferCount() const;

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
	 */
	void queueChanged();

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
};

}

#endif // HAVE_QUICKDC_QUI_TRANSFERBRIDGE_H

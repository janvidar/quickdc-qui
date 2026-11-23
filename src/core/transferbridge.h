/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_TRANSFERBRIDGE_H
#define HAVE_QUICKDC_QUI_TRANSFERBRIDGE_H

#include "coretypes.h"

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QVector>


namespace QuickDC { namespace Api { struct Event; namespace Events { struct TransfersSnapshot; } } }

namespace Qdc {

class CoreThread;

/**
 * The transfer engine and the download queue, as a view sees it.
 *
 * Lives on the thread that constructed it. It hears the core's transfer and
 * queue events and asks for everything else with commands, so it holds nothing
 * on the core thread.
 *
 * The two halves of a transfer view are served differently, because the core
 * serves them differently:
 *
 *   comings and goings   pushed. transferStarted() and the three ways a
 *                        transfer can end are all real events from the core.
 *   rates and progress   pulled. Nothing in the core announces progress, so
 *                        refresh() asks for a TransfersSnapshot instead. A view wanting a live
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
	 *
	 * @param hidden path prefixes whose files are left out entirely; see hidePath().
	 */
	static QVector<TransferInfo> foldByFile(const QVector<TransferInfo>& chunks,
	                                        const QVector<QueueEntry>& queued,
	                                        const QVector<TransferInfo>& previous = {},
	                                        int sinceMs = 0,
	                                        const QStringList& hidden = {});

	/**
	 * Keep the files under 'prefix' out of what a transfer view is shown.
	 *
	 * A transfer list is a list of what the user asked for. Not everything this
	 * client fetches is that: the body of a bulletin somebody is reading is fetched
	 * by hash the same way as any other file, and opening a thread of ten replies
	 * would put ten rows in a list whose whole purpose is the film that was asked
	 * for last night. So a caller that fetches on its own account names the directory
	 * it fetches into, and its files are absent from activeTransfers(),
	 * queueSnapshot() and the count of what is moving.
	 *
	 * Hiding a row is not hiding the transfer. The file is queued, is fetched, and
	 * is reported to whoever asked for it by its own signals; what is hidden is one
	 * view of it. Anything the user asked for is somewhere under a directory they
	 * chose, so nothing of theirs can fall under one of these.
	 */
	void hidePath(const QString& prefix);

	/** The prefixes hidePath() was given, in the order they were given. */
	QStringList hiddenPaths() const;

	/**
	 * Whether 'filename' lies under one of 'prefixes': the rule hiding uses.
	 *
	 * A path component at a time rather than a bare string prefix, so that a
	 * directory called "postscripts" is not hidden by one called "posts".
	 */
	static bool isUnder(const QString& filename, const QStringList& prefixes);

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

	/**
	 * Queue what a search turned up, with the peer that answered it as a source.
	 *
	 * The difference from the above is the source, and it is the whole point: a
	 * search reply says who has the file, so there is no need to go looking for
	 * somewhere to get it from. It is also the only way to download a reply that
	 * carried no TTH, since without a hash there is nothing for the queue's own
	 * searching to match on.
	 *
	 * @param filename the local file to download to, path and all.
	 */
	void enqueue(const QString& filename, const Qdc::SearchHit& hit);

	/**
	 * Queue a file somebody is waiting to read, and go looking for it at once.
	 *
	 * The difference from the plain enqueue() is when the search happens. The
	 * queue's own rotation sends one search per two minutes across everything in it,
	 * which is right for a background queue that nobody is watching and useless for
	 * a document somebody has just clicked on: ten of them would be twenty minutes
	 * before the last was even looked for, and every one of those turns is one the
	 * user's own downloads did not get. So a search for this file goes out now,
	 * without disturbing the rotation, which continues on exactly the schedule it
	 * would have had.
	 *
	 * No source is named. That is deliberate and is the whole reason this is not
	 * addFallbackSource() as well: a document's readers are visible to whoever
	 * serves it, so where a search turns up somebody other than the author they are
	 * preferred, and the author is named only if nobody else answers.
	 *
	 * @param size what to bound the file at, which for a post is the caller's own cap
	 *        rather than any size a hub quoted.
	 * @param hubUrl the hub the file was learned from, which is the only one asked
	 *        about it. A post's hash is on one board and its readers are that hub's
	 *        subscribers, so a search sent anywhere else reaches nobody who can answer
	 *        and tells that hub's operator which boards this client reads. Empty asks
	 *        every hub, for a file that was not learned from any one of them.
	 */
	void enqueueForReading(const QString& filename, const QString& tth, quint64 size,
	                       const QString& hubUrl = QString());

	/**
	 * Name a source for a queued file, where nothing else has answered for it.
	 *
	 * Does nothing where the file already has a source, which is what makes this a
	 * fallback rather than a first choice: the search sent by enqueueForReading()
	 * is given its chance, and this is where a file nobody else answered for gets
	 * the one source it is certain of.
	 *
	 * @param cid how the queue names a user: the client ID on ADC. A BBS0 index
	 *        entry carries the CID of the session the hub accepted the post from, so
	 *        the author of a post is a known source for its body.
	 */
	void addFallbackSource(const QString& tth, const QString& hubUrl,
	                       const QString& cid);

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
	void onCoreEvent(const QuickDC::Api::Event& event);
	void absorb(const QuickDC::Api::Events::TransfersSnapshot& taken);

	CoreThread* m_thread;

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

	/** Whose rows are left out of every reading; see hidePath(). */
	QStringList m_hidden;

	/** Whether a reading has been asked for and not yet arrived. */
	bool m_reading = false;

	/** Which snapshot is this bridge's own; see SnapshotTransfers. */
	quint64 m_request = 0;
};

}

#endif // HAVE_QUICKDC_QUI_TRANSFERBRIDGE_H

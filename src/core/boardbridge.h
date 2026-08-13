/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_BOARDBRIDGE_H
#define HAVE_QUICKDC_QUI_BOARDBRIDGE_H

#include "coretypes.h"
#include "relay.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

#include <memory>
#include <utility>

namespace Qdc {

class BoardStore;
class CoreThread;
class HubBridge;
class TransferBridge;

/**
 * What this client holds of the bulletin boards it has been told about, as a view
 * sees it.
 *
 * A HubBridge carries what one hub says: the boards it has, the entries on them,
 * and the refusals. None of that is kept by a hub connection, because a board has
 * to be there before the hub is - an interface drawn at start-up has no session to
 * ask - so this is where the entries are written down, and it is what answers the
 * cursor a subscription resumes from.
 *
 * The two halves it owns live on the core thread and are reached only from a job
 * there. A BoardIndex is one hub's assertions about one board, kept per hub because
 * merging two hubs' assertions produces a board that exists nowhere. A PostStore is
 * the documents, keyed by hash alone and shared across every hub, because a post
 * carries no board and the same bytes are the same post wherever they are indexed.
 *
 * Three policies live here rather than in a view, because each of them is a
 * decision about what this client does on the network and not about what it draws:
 *
 *   fetching     never on the arrival of an entry. A subscription is cheap and a
 *                body is not, and fetching one says "I have read this" to whoever
 *                answers - to the author first of all, who is the only source until
 *                somebody else has read it. So a body is fetched when a user asks
 *                for it, and prefetching a thread is off unless a hub has been
 *                told otherwise.
 *   capping      independently of what the entry declares. BoardEntry::size is the
 *                author's own claim copied by the hub and bounds nothing at all;
 *                see fetchCap() and abandonOverCap().
 *   discarding   never as a side effect. Throwing a document away withdraws this
 *                client as a source for a post that may have no other copy left,
 *                so it is chosen in one place and carried out in another, and the
 *                carrying out waits until nothing is being sent the file - see
 *                releasePending().
 *
 * Lives on the thread that constructed it. Nothing here waits for the core: every
 * answer arrives as a signal, including the ones a view would rather have had as a
 * return value.
 */
class BoardBridge final : public QObject {
	Q_OBJECT

public:
	/**
	 * Whether what is held for a board can still be caught up with; mirrors
	 * QuickDC::BoardIndex::View.
	 *
	 * Three states and not a flag, because a board no descriptor has arrived for
	 * is not a board known to be whole. A hub replays no further back than the OT
	 * of its descriptor and says so in advance precisely so that a client can tell
	 * the difference, and an interface that showed a board with a hole in it as
	 * the whole of that board would be presenting an absence as a fact.
	 */
	enum class View {
		/** No descriptor has been seen, so the question cannot be answered. */
		Unknown,

		/** Everything before what is held will still be replayed. */
		Complete,

		/** The entries between the cursor and the hub's oldest are not coming. */
		Gap
	};
	Q_ENUM(View)

	/**
	 * @param transfers how a document is fetched and what says whether one is
	 *        being sent to somebody. May be null, which leaves a bridge that
	 *        reads and writes the index and never fetches a body - a usable
	 *        client, since reading a board does not require answering for it.
	 * @param directory where the indexes and the documents live; "~/.quickdc" by
	 *        default. Three directories are made under it: "boards" for what the
	 *        hubs said, "posts" for the documents, and "incoming" for a fetch that
	 *        has not finished. Named after the hash and never after anything a hub
	 *        chose - '..' is a legal board name.
	 */
	explicit BoardBridge(CoreThread* thread, TransferBridge* transfers = nullptr,
	                     const QString& directory = QString(),
	                     QObject* parent = nullptr);
	~BoardBridge() override;

	/**
	 * The name a hub's boards are filed under: its "{scheme}://{host}:{port}".
	 *
	 * The same form the core files a hub under and the same one a download source
	 * records, which is what lets the CID out of an index entry be handed to the
	 * queue as a source on that hub. Keyed on the address as typed instead, one
	 * hub reachable by two addresses would have an index each.
	 */
	static QString hubKey(const QString& url);

	/**
	 * Keep what this hub says about its boards.
	 *
	 * Every entry it reports is written down and every descriptor is recorded, so
	 * that a cursor and a gap can be answered later. Nothing is fetched: an entry
	 * arriving is not a reason to ask anybody for anything.
	 *
	 * Dropped by itself if the hub is destroyed; unfollow() is for a caller that
	 * wants to stop before then.
	 */
	void follow(HubBridge* hub);
	void unfollow(HubBridge* hub);

	/** Whether follow() has been called for this hub and not undone. */
	bool follows(HubBridge* hub) const;

	/**
	 * Subscribe to a board, resuming from the highest timestamp already held.
	 *
	 * The cursor is on disk and the disk is the core thread's, so this reads it
	 * there and sends the subscription when the answer arrives - which is a moment
	 * later, and is why this is not a question a caller can ask and act on itself.
	 *
	 * Resuming means from the highest seen and never from one past it. Timestamps
	 * are seconds and are not unique, so a post the hub accepted in the same second
	 * as the last entry received would fall behind a cursor advanced past it and
	 * would be delivered to nobody; the cost is that the entries of that second
	 * arrive twice, and the index absorbs them.
	 *
	 * boardStateReady() is emitted with the cursor and the view either way, so a
	 * caller learns of a gap it cannot close at the moment it asks to catch up.
	 */
	void resume(HubBridge* hub, const QString& board);

	/** What this bridge last decided about a document; Announced if it knows none. */
	PostState postState(const QString& tth) const;

	/**
	 * Who a post is attributed to: the CID of the index entry it was verified
	 * against.
	 *
	 * Always the hub's claim and never the document's, and that is the whole of the
	 * rule. The document says who wrote the post and the hub says who submitted it,
	 * which are claims about different events; where they disagree the honest case
	 * - a post relayed to a second hub by somebody who did not write it - is
	 * indistinguishable from a forgery, so the disagreement is shown and the
	 * document's claim is never presented as the author. See PostState::Disputed.
	 *
	 * Answered from memory, so a pane may ask while it is laying a post out.
	 */
	QString attribution(const QString& tth) const;

	/**
	 * Whether the bodies of a thread the user opens may be fetched ahead of them,
	 * per hub.
	 *
	 * Off until somebody says otherwise, and per hub because that is the scope the
	 * disclosure has: a prompt fetch identifies a reader who was online and
	 * subscribed at that moment, and on a quiet board the author can count them.
	 */
	void setPrefetch(const QString& hubUrl, bool prefetch);
	bool prefetches(const QString& hubUrl) const;

	/**
	 * Whether a verified document is offered to peers by hash, per hub.
	 *
	 * This is what makes a board durable - a popular post ends up held by everyone
	 * who has read it and survives the departure of its author - and it is also a
	 * statement to anybody who asks: *I have read this*. So it is off until a hub
	 * the user has actually joined is named, and turning it off withholds what is
	 * already offered rather than throwing it away.
	 *
	 * It reaches the documents this hub's posts were read from, and no others. The
	 * store is keyed by hash alone, because the same bytes are the same post on every
	 * hub that indexes them, and it deliberately does not remember what was offered:
	 * an offer lasts no longer than the process that made it. So after a restart a
	 * document is offered again from the moment it is next read, which errs towards
	 * saying less about what this client has read rather than more.
	 */
	void setServing(const QString& hubUrl, bool serving);
	bool serves(const QString& hubUrl) const;

	/**
	 * The most this will fetch for one post, whatever the entry declared.
	 *
	 * Nothing verifies a declared size before the transfer: the author states it
	 * and the hub copies it, so an entry claiming four hundred bytes may be
	 * answered by a peer that sends for as long as it is allowed to. A fetch is
	 * therefore bounded here, an entry declaring more than this is not asked for at
	 * all, and a transfer that passes it is abandoned.
	 */
	void setFetchCap(quint64 bytes);
	quint64 fetchCap() const;

	/**
	 * How many post bodies may be in flight at once.
	 *
	 * Bounded separately from the download queue on purpose: opening a thread of
	 * fifty replies must not displace what the user asked for. The rest wait here
	 * rather than in the queue, which is also why a thread that is closed again
	 * costs nothing.
	 */
	void setConcurrentFetches(int fetches);
	int concurrentFetches() const;

	/**
	 * The most this will fetch from one board in a period, in bytes.
	 *
	 * A board is written to by strangers and read by whoever is curious, and a
	 * thread of large posts is the cheapest thing on the network to make. This
	 * bounds what a single board can talk this client into moving however many
	 * times a user clicks. Zero for either argument means no bound.
	 */
	void setBoardAllowance(quint64 bytes, int seconds);

	/**
	 * What this client is willing to hold: documents older than 'ageSeconds', and
	 * no more than 'totalBytes' of them. Zero for either means no bound.
	 *
	 * Applied to documents and never to index entries. An entry is a few hundred
	 * bytes and a board without its entries is not a board, while a document is as
	 * large as its author made it.
	 */
	void setCacheBounds(quint64 ageSeconds, quint64 totalBytes);

	/**
	 * Which of the documents waiting to be discarded may go: the ones no upload
	 * names.
	 *
	 * Static and public because it is the assurance nothing below this layer can
	 * make, and because getting it wrong is a use-after-free rather than a wrong
	 * pixel. An upload holds the interior handle the share manager returns for a
	 * hash - a member of the object that withdrawing it destroys - and reads
	 * through it until the transfer ends. The share manager hears nothing about
	 * transfers and the store knows about neither, so this layer is the only one
	 * that can say; a document is named by its hash and a document being sent is a
	 * file whose name is that hash, which is the whole of the matching rule.
	 *
	 * A document nobody may release yet stays marked and is released later. Nothing
	 * is lost by waiting: a marked document is still readable and still correct.
	 */
	static QVector<QString> releasable(const QVector<QString>& pending,
	                                   const QVector<Qdc::TransferInfo>& transfers);

	/**
	 * Whether a transfer that has moved 'received' bytes has to be abandoned.
	 *
	 * Both bounds, in one place, because they are asked about together and neither
	 * is the other's excuse: 'declared' is the author's claim and a transfer passing
	 * it is not the post it says it is, while 'cap' is this client's own and is the
	 * only one of the two that a hostile peer cannot choose.
	 */
	static bool passesCap(quint64 received, quint64 declared, quint64 cap);

public slots:
	/**
	 * Read a board back from disk. Answered by entriesLoaded().
	 *
	 * The whole board, oldest first, one entry per hash and a withdrawn post
	 * present as its tombstone. There is no useful tail of a board: it is read to
	 * be displayed as a board.
	 */
	void loadEntries(const QString& hubUrl, const QString& board);

	/** Ask what is held for a board. Answered by boardStateReady(). */
	void requestState(const QString& hubUrl, const QString& board);

	/**
	 * Fetch the body of one post, because somebody asked to read it.
	 *
	 * The entry is passed rather than looked up because it is what the caller is
	 * looking at, and because everything the fetch needs is in it: the hash to ask
	 * for, the size to disbelieve, and the CID to fall back on when a search finds
	 * nobody else.
	 *
	 * Answered by postReady() or postUnavailable(), and by postStateChanged() on
	 * the way. A document already held and already verified is answered from disk
	 * without asking anybody for anything.
	 */
	void fetch(const QString& hubUrl, const Qdc::BoardEntry& entry);

	/**
	 * Fetch the bodies of a thread the user has opened, where the hub allows it.
	 *
	 * Does nothing at all unless setPrefetch() has been called for the hub. This is
	 * the one place a body is fetched without somebody having asked for that body,
	 * and it is a hub-by-hub decision rather than a global one.
	 */
	void prefetchThread(const QString& hubUrl, const QVector<Qdc::BoardEntry>& thread);

	/**
	 * Decide what a document that has arrived actually is, and keep it if it is the
	 * post it claims to be.
	 *
	 * The whole of verification, in the order the extension requires it:
	 *
	 *   1. the bytes hash to the TTH the entry named, or they are discarded
	 *      entirely. Everything else rests on this one. A copy that fails it says
	 *      nothing about the post - the next peer may have the real document - so
	 *      the bytes go and the entry stays askable.
	 *   2. the header parses, and is refused rather than repaired. A document that
	 *      hashes to its name and does not parse is unreadable for ever, because
	 *      every copy of it is these same bytes; that one is remembered as failed
	 *      so it is not fetched and refused over and over.
	 *   3. the parent and the subject are taken from the document where they
	 *      disagree with the entry. The document is inside the hash and the entry
	 *      is not.
	 *   4. the author the document claims is compared against the one the hub
	 *      claims. A disagreement is PostState::Disputed, attributed to the hub's
	 *      claim with the document's shown as a claim - never the other way about.
	 *
	 * Only a document that passed all four is offered to peers, and only where the
	 * hub it came from is one the user said to serve on. Answering for bytes that
	 * are not the post they claim to be would spread a forgery under a name other
	 * clients have checked.
	 *
	 * Public because fetching is not the only way a document can arrive, and
	 * because this is the decision worth testing without a network.
	 */
	void adopt(const QString& hubUrl, const Qdc::BoardEntry& entry,
	           const QString& path);

	/**
	 * Abandon any fetch in flight that has passed its cap.
	 *
	 * Handed the rows rather than reading them, so that a caller already polling
	 * the transfers pays for one reading and so that the decision can be made
	 * against rows that no peer had to be persuaded to produce.
	 */
	void abandonOverCap(const QVector<Qdc::TransferInfo>& transfers);

	/** Choose what the cache bounds say should go, and then release what may. */
	void evictNow();

	/**
	 * Carry out the discards that have been chosen, for the documents nothing is
	 * reading.
	 *
	 * The other half of every withdrawal here: a tombstone, an eviction and the
	 * user turning serving off all mark and none of them deletes. Run whenever the
	 * transfers change as well as from here, so a document that could not go a
	 * moment ago goes as soon as it can.
	 */
	void releasePending();

signals:
	/**
	 * A post has been fetched, verified and read.
	 *
	 * BoardPost::cid is the author the *document* claims, which is what that field
	 * means and is never the attribution. Where BoardPost::state is
	 * PostState::Disputed the two claims disagree and the post is attributed to the
	 * index entry's CID, which attribution() will answer for.
	 */
	void postReady(const QString& tth, const Qdc::BoardPost& post);

	/**
	 * The body is not here, and this is why, in words fit to show somebody.
	 *
	 * Never a reason to drop the entry. An index entry can outlive every copy of
	 * the document it names, and the copy that is missing today may be on a client
	 * that is offline; the entry keeps its subject, its author and its date, and
	 * says that the content could not be retrieved.
	 */
	void postUnavailable(const QString& tth, const QString& reason);

	/** How much of a post is known has changed; see Qdc::PostState. */
	void postStateChanged(const QString& tth, Qdc::PostState state);

	/** What loadEntries() read back, oldest first. */
	void entriesLoaded(const QString& hubUrl, const QString& board,
	                   const QVector<Qdc::BoardEntry>& entries);

	/**
	 * The entries of a live board that were kept, in the order they arrived.
	 *
	 * What a board being watched should show. The ones not here were duplicates of
	 * something already held - a resumed subscription replays the second it resumed
	 * from, so that is ordinary rather than an error - or named no document that
	 * could ever be fetched.
	 *
	 * A tombstone is here like any other entry, carrying its hash, its timestamp and
	 * the flag. It removes the post from the board and not its replies, which are
	 * separate posts.
	 */
	void entriesStored(const QString& hubUrl, const QString& board,
	                   const QVector<Qdc::BoardEntry>& entries);

	/**
	 * Where a board stands: the cursor a subscription would resume from, and
	 * whether what is held has a hole in it that no subscription will close.
	 */
	void boardStateReady(const QString& hubUrl, const QString& board,
	                     quint64 cursor, Qdc::BoardBridge::View view);

private:
	/** One body on its way, and everything deciding when to give up on it. */
	struct Fetch {
		QString hubUrl;
		BoardEntry entry;

		/** The local file the queue is writing, which is what names it here. */
		QString path;

		/** The lower of what the entry declared and what this client allows. */
		quint64 cap = 0;

		/**
		 * How long there has been nothing arriving, which is the one clock this
		 * needs and answers two questions.
		 *
		 * The author is a source from the moment the entry arrives and is the last
		 * party to ask, because an author who records who fetches their post has a
		 * complete census of its early readers - so the search goes out first and
		 * this is where it is given time to answer. And a fetch that nothing has
		 * answered at all is eventually given up on, because an entry can outlive
		 * every copy of the document it names and somebody has to say so.
		 *
		 * Restarted whenever bytes arrive: a fetch that is moving has a source, so
		 * there is neither anything to gain by naming the author nor anything to
		 * give up on.
		 */
		QElapsedTimer since;

		/** How much had arrived when 'since' was last restarted. */
		quint64 moved = 0;

		bool authorNamed = false;
	};

	/** What one board has been allowed to move lately; see setBoardAllowance(). */
	struct Allowance {
		quint64 spent = 0;
		QElapsedTimer since;
	};

	/** Persist a batch of entries as they arrive, and report what was kept. */
	void store(const QString& hubUrl, const QVector<Qdc::BoardEntry>& entries);

	/** Record what a hub says a board is, and re-report the board's state. */
	void describe(const QString& hubUrl, const Qdc::BoardInfo& board);

	/** Enqueue one body, ask for sources at once, and mind the bounds. */
	void startFetch(const QString& hubUrl, const Qdc::BoardEntry& entry);

	/** Start whatever is waiting on the concurrency bound. */
	void startWaiting();

	/** Give up on one fetch, saying why, and let the next one in. */
	void giveUp(const QString& tth, const QString& reason);

	void setState(const QString& tth, PostState state);

	/** Whether this board may move another 'bytes'; charges them if it may. */
	bool withinAllowance(const QString& hubUrl, const QString& board, quint64 bytes);

	/** The author of a post whose search has had its chance, named as a source. */
	void nameAuthors();

	/** Note the bytes that have arrived, which is what says a fetch is alive. */
	void noteProgress(const QVector<Qdc::TransferInfo>& transfers);

	/** Say so about a post nobody who has it could be reached for. */
	void giveUpOnTheUnanswered();

	CoreThread* m_thread = nullptr;
	TransferBridge* m_transfers = nullptr;

	QString m_directory;

	/**
	 * The index and the documents, in a box that outlives this object.
	 *
	 * Owned by the core thread, including the job that deletes them - which runs
	 * after this bridge is gone, so it cannot be reached through it. Every job
	 * holds a reference, so the box lasts exactly as long as something still has
	 * use for what is in it.
	 */
	struct StoreBox {
		BoardStore* store = nullptr;
	};

	std::shared_ptr<StoreBox> m_stores;

	/** Where an answer from the core lands; see Qdc::Relay. */
	std::shared_ptr<Relay> m_relay;

	/** The hubs being followed, and the key each one's boards are filed under. */
	QHash<HubBridge*, QString> m_followed;

	/** Bodies in flight, by the local file the queue is writing to. */
	QHash<QString, Fetch> m_fetching;

	/** The same, by hash, since that is what a caller and the store name. */
	QHash<QString, QString> m_byTth;

	/** Waiting on the concurrency bound, in the order they were asked for. */
	QVector<std::pair<QString, BoardEntry>> m_waiting;

	/**
	 * Posts the store is being asked about, which is a moment long and matters.
	 *
	 * Whether a post has to be fetched at all is a question for the core thread, so
	 * there is a gap between a caller asking for one and this knowing that it is on
	 * its way. Two clicks inside that gap are one fetch and not two.
	 */
	QSet<QString> m_asking;

	QHash<QString, PostState> m_state;

	/** The CID each verified document was attributed to; see attribution(). */
	QHash<QString, QString> m_attributed;

	/**
	 * Which hub each document was fetched from, so that turning serving off for a
	 * hub withholds that hub's posts and not everything held.
	 *
	 * The store is keyed by hash alone, because a post is the same post on every hub
	 * that indexes it - but answering for one is a decision the user makes per hub,
	 * and this is the only place the two can be reconciled.
	 */
	QHash<QString, QString> m_fetchedFrom;

	QSet<QString> m_prefetch;
	QSet<QString> m_serving;

	/** What each board has been allowed to move, by hub and board. */
	QHash<QString, Allowance> m_allowances;

	quint64 m_cap;
	int m_concurrent;
	quint64 m_allowanceBytes;
	int m_allowanceSeconds;
	quint64 m_ageBound = 0;
	quint64 m_totalBound = 0;

	/**
	 * Drives the cap while something is in flight, and names an author whose
	 * search found nobody.
	 *
	 * Its own timer rather than whatever a view happens to poll, because both of
	 * those have to happen whether or not anybody is watching a transfer list: a
	 * peer that keeps sending is bounded by nothing else, and a reader waiting on a
	 * post nobody answered for is waiting on this.
	 */
	QTimer* m_watch = nullptr;
};

}

#endif // HAVE_QUICKDC_QUI_BOARDBRIDGE_H

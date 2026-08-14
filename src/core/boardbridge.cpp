/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "boardbridge.h"

#include "corethread.h"
#include "hubbridge.h"
#include "transferbridge.h"

#include <api/core.h>
#include <api/transfers.h>
#include <bbs/boardindex.h>
#include <bbs/poststore.h>
#include <hash/hashjob.h>
#include <network/adc/adcbbs.h>
#include <share/sharemanager.h>

#include <samurai/crypto/digest/merkletree.h>
#include <samurai/crypto/digest/tiger.h>
#include <samurai/io/file.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <map>
#include <string>
#include <vector>

namespace {

/** A tiger root in base32, which is how long a post's name is. */
constexpr int TTH_LENGTH = 39;

/**
 * Whether 'tth' is a tiger tree root: exactly TTH_LENGTH characters of unpadded
 * base32, which is upper case A to Z and the digits 2 to 7.
 *
 * Checked here rather than assumed, because this is the string a cache filename is
 * built out of. The protocol code checks it too before an entry is ever dispatched,
 * and that is the point: a hash reaching this layer from anywhere else - a magnet
 * URI, a document's parent link, a caller's own idea of what to fetch - has to meet
 * the same bar before it names a file. Base32 has no padding at this length and no
 * lower case, so a lower case root and one containing '0', '1', '8' or '9' are
 * refused rather than folded.
 */
bool is_tiger_root(const QString& tth)
{
	if (tth.size() != TTH_LENGTH) return false;

	for (const QChar at : tth)
	{
		const char16_t code = at.unicode();

		const bool letter = code >= u'A' && code <= u'Z';
		const bool digit = code >= u'2' && code <= u'7';

		if (!letter && !digit) return false;
	}

	return true;
}

/**
 * The most this client will fetch for one post unless it is told otherwise.
 *
 * A post document is a header and a body of text, so this is enormous for what it
 * bounds - and that is the point. It is not a guess at how large a post is; it is
 * the point past which a peer that keeps sending is answering something other than
 * the question that was asked. What a fetch is normally bounded by is the size the
 * entry declared, and this is what bounds a declaration nothing verified.
 */
constexpr quint64 FETCH_CAP = 4u * 1024 * 1024;

/**
 * How many bodies are fetched at once.
 *
 * Small, and bounded apart from the download queue's own budget: a thread of fifty
 * replies is fifty files somebody is waiting on, and the queue would treat every
 * one of them exactly as it treats the film the user asked for last night. Two
 * moving with the rest waiting here is a thread that fills in from the top while
 * the user's own downloads keep their slots.
 */
constexpr int CONCURRENT_FETCHES = 2;

/** What one board may talk this client into moving, and over how long. */
constexpr quint64 ALLOWANCE_BYTES = 64u * 1024 * 1024;
constexpr int ALLOWANCE_SECONDS = 3600;

/**
 * How long a source search has to answer before the author is named as a source.
 *
 * The author of a post is a source for its body from the moment the entry arrives,
 * and is the last party to ask: an author who records who fetches their post has a
 * complete census of everybody who read it early. That is unavoidable in the end
 * and need not be volunteered at the start, so the search goes out first and this
 * is how long it is given - long enough for the hubs to answer and for a peer
 * connection to be brokered, short enough that a reader waiting on a post nobody
 * else holds is not left waiting on principle.
 */
constexpr int AUTHOR_GRACE_MS = 20000;

/** How often the fetches in flight are looked at; see BoardBridge::m_watch. */
constexpr int WATCH_INTERVAL_MS = 2000;

/**
 * How long a fetch with nothing arriving is waited for.
 *
 * An index entry can outlive every copy of the document it names, which is a third
 * thing and not an error: the entry keeps its subject, its author and its date, and
 * says that the content could not be retrieved. Something has to decide when to say
 * so, and this is it - measured from the last byte that arrived rather than from the
 * start, so a large post over a slow link is not given up on for being slow.
 */
constexpr int FETCH_TIMEOUT_MS = 300000;

/** Where one board stands, carried back in one piece. */
struct BoardState {
	quint64 cursor = 0;
	Qdc::BoardBridge::View view = Qdc::BoardBridge::View::Unknown;
};

/**
 * What taking a board up again came to: where to resume from, and what this client has
 * gone back to answering for.
 *
 * The two belong to the same job because they are the same walk. Subscribing is the
 * moment a client says it is carrying this board, and carrying it means being a source
 * for the posts of it that are on this disk - so the documents are offered then, rather
 * than whenever somebody happens to open the board to look at it. A client that only
 * answered for boards a user had opened would be no source at all for most of what it
 * holds.
 */
struct Resumed {
	BoardState state;

	/** The documents taken up again; see Stored::offered. */
	QVector<QString> offered;
};

/**
 * What a board reads as, as the index answered it.
 *
 * Carried back beside whatever the job was really asked to do, because each of those
 * jobs is already in the index for that board: asking for the count separately would
 * be another job, another walk over the entries, and a moment in which a badge and the
 * list it sits beside disagree.
 *
 * 'board' is filled in only where one job passed over several of them - see store() -
 * since everywhere else the caller asked about one board and still holds its name.
 */
struct ReadState {
	QString board;
	int unread = 0;

	/** Which of the hashes the job looked at are read; see BoardIndex::isRead(). */
	QVector<QString> read;
};

/** What storing a batch came to: what was kept, and where each board now stands. */
struct Stored {
	/** The entries that were written down, by their position in what was handed over. */
	QVector<int> kept;

	QVector<ReadState> boards;

	/**
	 * Those of them whose documents are already on this disk, and those already known
	 * not to be the post they claim to be.
	 *
	 * Asked of an arriving entry for the same reason it is asked of one read back from
	 * the cache: a document is held by hash and is the same document on every hub, while
	 * an index belongs to one board on one. The post relayed to a second hub arrives as
	 * news with its bytes already here.
	 */
	QVector<QString> held;
	QVector<QString> failed;

	/**
	 * Those of them this client has taken up answering for.
	 *
	 * Reported back because the hub a document is answered for is remembered on the
	 * other side, and it is that record which a later change to the setting reads to
	 * know what to stop offering.
	 */
	QVector<QString> offered;
};

/**
 * What a board read back as: its entries, what has been read of them, and which of
 * their documents are here.
 *
 * The documents come back with the entries because it is one walk over the same board
 * and because the two answers belong together: a state derived from what is held has
 * to be derived from the entries it will be shown beside.
 */
struct Loaded {
	QVector<Qdc::BoardEntry> entries;

	int unread = 0;
	QVector<QString> read;

	/** The documents on this disk, whatever verifying them came to. */
	QVector<QString> held;

	/** Those of them already known not to be the post they claim to be. */
	QVector<QString> failed;

	/** Those of them this client has taken up answering for; see Stored::offered. */
	QVector<QString> offered;
};

/** What the store already knows about a post somebody has asked to read. */
struct Known {
	/** Where the document is; empty when none is held. */
	QString path;

	/** Held, and already known not to be the post it claims to be. */
	bool failed = false;
};

/** What verifying one document came to, ready to be emitted. */
struct Verdict {
	Qdc::BoardPost post;

	/** Empty where the post can be read, and a sentence for a person otherwise. */
	QString reason;

	/**
	 * Whether the store holds the document now.
	 *
	 * Separate from the state, because the two part company in both directions: bytes
	 * that are not the post their hash names are thrown away and leave nothing held,
	 * while a document that hashes to its name and does not parse is kept for ever
	 * precisely because it can never be read.
	 */
	bool held = false;
};

QString from_std(const std::string& text)
{
	return QString::fromUtf8(text.c_str(), static_cast<qsizetype>(text.size()));
}

std::string to_std(const QString& text)
{
	const QByteArray utf8 = text.toUtf8();
	return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

/**
 * What the store already knows about one post.
 *
 * The same question whether somebody asked for a body or merely opened the post: is the
 * document here, and is it already known not to be the post it claims to be. Where the
 * two callers part company is in what they do when the answer is no - one asks a peer
 * for the bytes and the other asks nobody at all.
 */
Known what_is_known(QuickDC::PostStore& posts, const std::string& tth)
{
	Known known;

	known.failed = posts.verification(tth) == QuickDC::PostStore::Verification::Failed;

	if (!known.failed && posts.has(tth)) known.path = from_std(posts.pathFor(tth));

	return known;
}

/**
 * The tiger tree root of a document, base32, computed here and now.
 *
 * The download queue has already checked the hash of anything it reported complete,
 * and this checks it again, because the check is what makes every other rule in the
 * extension safe: a document reaches the store under a name, that name is what this
 * client will answer searches for afterwards, and a board is kept and re-served for
 * years rather than consumed once. Doing it twice costs one pass over a few
 * kilobytes that have just been read into memory anyway - and a document that
 * arrived by some route other than the queue has not been checked at all.
 */
QString tiger_tree_root(const QByteArray& document)
{
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree tree(
		&tiger, Samurai::Crypto::Digest::DEFAULT_MERKLE_BLOCK_SIZE,
		QUICKDC_TTH_TREE_LEVELS);

	tree.update(document.constData(), static_cast<size_t>(document.size()));

	char printed[128];
	if (!tree.digest().getFormattedString(
			Samurai::Crypto::Digest::HashValue::Format::Base32, printed,
			sizeof(printed)))
	{
		return QString();
	}

	return QString::fromLatin1(printed);
}

QuickDC::BoardEntry to_core(const Qdc::BoardEntry& entry)
{
	QuickDC::BoardEntry converted;

	converted.tth        = to_std(entry.tth);
	converted.size       = entry.size;
	converted.board      = to_std(entry.board);
	converted.cid        = to_std(entry.cid);
	converted.nick       = to_std(entry.nick);
	converted.parent     = to_std(entry.parent);
	converted.thread     = to_std(entry.thread);
	converted.subject    = to_std(entry.subject);
	converted.timestamp  = entry.timestamp;
	converted.removed    = entry.removed;

	return converted;
}

Qdc::BoardEntry from_core(const QuickDC::BoardEntry& entry)
{
	Qdc::BoardEntry converted;

	converted.tth        = from_std(entry.tth);
	converted.size       = entry.size;
	converted.board      = from_std(entry.board);
	converted.cid        = from_std(entry.cid);
	converted.nick       = from_std(entry.nick);
	converted.parent     = from_std(entry.parent);
	converted.thread     = from_std(entry.thread);
	converted.subject    = from_std(entry.subject);
	converted.timestamp  = entry.timestamp;
	converted.removed    = entry.removed;

	return converted;
}

QuickDC::Board to_core(const Qdc::BoardInfo& board)
{
	QuickDC::Board converted;

	converted.name         = to_std(board.name);
	converted.title        = to_std(board.title);
	converted.description  = to_std(board.description);
	converted.permissions  = board.permissions;
	converted.maxSize      = board.maxSize;
	converted.newest       = board.newest;
	converted.oldest       = board.oldest;
	converted.posts        = board.posts;
	converted.removed      = board.removed;

	return converted;
}

Qdc::BoardBridge::View from_core(QuickDC::BoardIndex::View view)
{
	switch (view)
	{
		case QuickDC::BoardIndex::View::Complete:
			return Qdc::BoardBridge::View::Complete;

		case QuickDC::BoardIndex::View::Gap:
			return Qdc::BoardBridge::View::Gap;

		case QuickDC::BoardIndex::View::Unknown:
			break;
	}

	return Qdc::BoardBridge::View::Unknown;
}

}

namespace Qdc {

/**
 * The core side half of a BoardBridge: what the hubs said, and every document.
 *
 * Built and destroyed inside jobs on the core thread, and only ever called from
 * there. Neither half is thread safe and both belong to the thread that drives the
 * core, which is also the thread the share manager reports a hash on.
 *
 * The two are held together because most questions need both of them - where does
 * this board resume from, and is this post held - and are separate objects in the
 * core because they are scoped differently: an index is per hub, since a hub's
 * assertions about a board are its own and merging two hubs' produces a board that
 * exists nowhere, while the documents are global, since a post is named by the hash
 * of its own bytes and is the same post wherever it is indexed.
 */
class BoardStore final {

public:
	BoardStore(const std::string& boards, const std::string& documents,
	           QuickDC::Share::Manager* shares)
		: index(boards)
		, posts(documents, shares)
	{
	}

	BoardStore(const BoardStore&) = delete;
	BoardStore& operator=(const BoardStore&) = delete;

	QuickDC::BoardIndex index;
	QuickDC::PostStore posts;
};

}

Qdc::BoardBridge::BoardBridge(CoreThread* thread, TransferBridge* transfers,
                              const QString& directory, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
	, m_transfers(transfers)
	, m_directory(directory.isEmpty()
		? QDir(QDir::homePath()).filePath(QStringLiteral(".quickdc"))
		: directory)
	, m_stores(std::make_shared<StoreBox>())
	, m_relay(Relay::create())
	, m_cap(FETCH_CAP)
	, m_concurrent(CONCURRENT_FETCHES)
	, m_allowanceBytes(ALLOWANCE_BYTES)
	, m_allowanceSeconds(ALLOWANCE_SECONDS)
{
	/*
	 * A body on its way is written here and moved into the store once it has been
	 * checked, which is why it is a directory of its own: the store holds what has
	 * verified, and a file that is still arriving has been verified by nobody.
	 *
	 * Every name under all three is a tiger root this client has checked. Nothing a
	 * hub said reaches a path - not a board name, of which ".." is a legal one, and
	 * not the display name of an attachment.
	 */
	const QString incoming = QDir(m_directory).filePath(QStringLiteral("incoming"));
	const QString boards = QDir(m_directory).filePath(QStringLiteral("boards"));
	const QString documents = QDir(m_directory).filePath(QStringLiteral("posts"));

	m_watch = new QTimer(this);
	m_watch->setInterval(WATCH_INTERVAL_MS);

	connect(m_watch, &QTimer::timeout, this, [this] {
		if (m_fetching.isEmpty())
		{
			m_watch->stop();
			return;
		}

		nameAuthors();
		giveUpOnTheUnanswered();

		/*
		 * Asked for rather than waited for. The answer arrives as
		 * snapshotChanged(), where the cap is applied - so a peer that keeps
		 * sending is stopped whether or not anything is drawing a transfer list.
		 */
		if (m_transfers) m_transfers->refresh();
	});

	if (m_transfers)
	{
		/*
		 * A body somebody is reading is not a download somebody chose, so it stays
		 * out of the transfer list and out of the queue view. A thread of ten
		 * replies would otherwise be ten rows nobody asked for, in a list whose
		 * whole purpose is what the user did ask for.
		 */
		m_transfers->hidePath(incoming);

		connect(m_transfers, &TransferBridge::downloadCompleted, this,
		        [this](const QString& filename, quint64, const QString& tth) {
			const auto at = m_fetching.constFind(filename);
			if (at == m_fetching.constEnd()) return;

			const Fetch fetch = *at;

			/*
			 * The queue reports the hash it verified the file against, and it has
			 * to be the one that was asked for. Verification below would say so
			 * anyway; this only saves reading the file to find out.
			 */
			if (!tth.isEmpty() && tth != fetch.entry.tth)
			{
				giveUp(fetch.entry.tth, tr("What arrived is not the post that was "
				                           "asked for."));
				return;
			}

			adopt(fetch.hubUrl, fetch.entry, filename);
		});

		connect(m_transfers, &TransferBridge::snapshotChanged, this, [this] {
			const QVector<TransferInfo> moving = m_transfers->activeTransfers();

			noteProgress(moving);
			abandonOverCap(moving);

			/* A document that could not be discarded a moment ago may go now. */
			releasePending();
		});
	}

	if (!m_thread) return;

	/*
	 * Posted rather than waited on. Both halves read a directory as they are built,
	 * and a window that waited for that stopped drawing for as long as the core's
	 * current turn took. Everything this bridge can be asked to do afterwards is
	 * posted too and finds the store through the same box, so an order given before
	 * it exists simply queues up behind it.
	 */
	m_thread->post([holder = m_stores, thread = m_thread,
	                incoming = to_std(incoming), boards = to_std(boards),
	                documents = to_std(documents)] {
		QuickDC::Core* core = thread->core();

		QDir().mkpath(from_std(incoming));

		holder->store = new BoardStore(boards, documents,
		                               core ? core->shares : nullptr);

		/*
		 * Whatever was marked to go before the last shutdown goes now, and this is
		 * the one moment when nothing has to be vouched for: an offer to the share
		 * manager lasts no longer than the process that made it, so a store that
		 * has just been built is serving nothing and no upload can be holding a
		 * handle into one of these files.
		 */
		for (const std::string& tth : holder->store->posts.pending())
			holder->store->posts.release(tth);
	});
}

Qdc::BoardBridge::~BoardBridge()
{
	/* Nothing in flight has anywhere to report to from here on. */
	m_relay->detach();

	if (!m_thread) return;

	/*
	 * Posted, and it does not have to block. Nothing in the core holds a pointer to
	 * either half: neither is a listener registered anywhere, and the store listens
	 * to the share manager only while a hash job it started is outstanding, which
	 * its own destructor is what sees to.
	 *
	 * Nothing is released here, which is deliberate. A document marked to go stays
	 * marked; marks survive a restart, and the next start-up releases them at the
	 * one moment when nothing can be reading the file.
	 */
	m_thread->post([holder = m_stores] {
		delete holder->store;
		holder->store = nullptr;
	});
}

QString Qdc::BoardBridge::hubKey(const QString& url)
{
	return HubBridge::settingsUrl(url);
}

void Qdc::BoardBridge::follow(HubBridge* hub)
{
	if (!hub || m_followed.contains(hub)) return;

	const QString key = hubKey(hub->url());
	m_followed.insert(hub, key);

	connect(hub, &HubBridge::boardEntriesReceived, this,
	        [this, key](const QVector<BoardEntry>& entries) {
		store(key, entries);
	});

	connect(hub, &HubBridge::boardDescribed, this,
	        [this, key](const BoardInfo& board) {
		describe(key, board);
	});

	/*
	 * The lambdas above are bound to both objects, so either one going takes them
	 * with it. This is only so that follows() stops saying yes about a hub that has
	 * gone, and so that a resume() answered afterwards has something to test.
	 */
	connect(hub, &QObject::destroyed, this, [this, hub] {
		m_followed.remove(hub);
	});
}

void Qdc::BoardBridge::unfollow(HubBridge* hub)
{
	if (!hub || !m_followed.contains(hub)) return;

	m_followed.remove(hub);

	disconnect(hub, &HubBridge::boardEntriesReceived, this, nullptr);
	disconnect(hub, &HubBridge::boardDescribed, this, nullptr);
}

bool Qdc::BoardBridge::follows(HubBridge* hub) const
{
	return m_followed.contains(hub);
}

void Qdc::BoardBridge::store(const QString& hubUrl, const QVector<BoardEntry>& entries)
{
	if (!m_thread || entries.isEmpty()) return;

	/*
	 * Converted to the core's own value type before the closure takes them: a
	 * QString shares its buffer with its copies, and these are read on the core's
	 * thread while this one goes on holding the originals.
	 *
	 * What comes back is which of them were kept, by position, rather than the
	 * entries themselves - the caller's copies are right here, and converting them
	 * back would be the same strings built twice.
	 */
	std::vector<QuickDC::BoardEntry> converted;
	converted.reserve(entries.size());

	for (const BoardEntry& entry : entries)
		converted.push_back(to_core(entry));

	m_thread->call(m_relay,
		[holder = m_stores, converted = std::move(converted), hub = to_std(hubUrl),
		 serving = serves(hubUrl)] {
			Stored stored;

			if (!holder->store) return stored;

			/* Which boards the batch touched, and what was written down on each. */
			std::map<std::string, std::vector<std::string>> touched;

			for (size_t n = 0; n < converted.size(); ++n)
			{
				const QuickDC::BoardEntry& entry = converted.at(n);

				if (entry.board.empty()) continue;

				/*
				 * True for an entry that was written down, and true as well where
				 * nothing could be written at all - an index with nowhere to write
				 * holds nothing, so nothing it is told can be a repeat of anything,
				 * and a live entry must not be dropped from a view because the
				 * cache is broken. False is a duplicate from the resumption window
				 * or a hash that is not a tiger root.
				 */
				if (!holder->store->index.append(hub, entry.board, entry))
					continue;

				stored.kept.append(static_cast<int>(n));
				touched[entry.board].push_back(entry.tth);

				/*
				 * And whether the document is already here, which an arriving entry is
				 * no less likely to have than one read back off the disk. Documents are
				 * held by hash and are the same document everywhere; an index belongs to
				 * one board on one hub. So the ordinary way to meet a post whose bytes
				 * are already on this machine is exactly this - the same post indexed on
				 * a second hub, arriving as news. Asked here, or it would be drawn as
				 * unfetched and then fetched over the network from somebody who would be
				 * told, for a file already on the disk.
				 */
				const Known known = what_is_known(holder->store->posts, entry.tth);

				if (known.failed) stored.failed.append(from_std(entry.tth));
				else if (!known.path.isEmpty()) stored.held.append(from_std(entry.tth));

				/*
				 * A tombstone is a request to stop holding the document, honoured
				 * by the clients that receive it and meaningless to those that do
				 * not - so it marks, and does not delete. The release is the other
				 * half and waits for somebody who can say nothing is reading it.
				 */
				if (entry.removed)
				{
					holder->store->posts.forget(entry.tth);
					continue;
				}

				/*
				 * And answered for, where the user said to answer for this hub's posts.
				 * An offer lasts only as long as the process that made it, so a document
				 * held from an earlier session is offered again the first time the board
				 * it is on says it is there - which for a board being read live is here.
				 * Without it a client would hold a post, draw it, and still leave the
				 * next reader to find the one copy elsewhere.
				 */
				if (serving && holder->store->posts.serve(entry.tth))
					stored.offered.append(from_std(entry.tth));
			}

			/*
			 * Where each of those boards stands now, in the same job. An entry that has
			 * only just arrived is not always an unread one: a board somebody dismissed
			 * carries a watermark, and an entry landing under it arrives read - so the
			 * index is asked about each of them rather than assumed to say no.
			 */
			for (const auto& [board, written] : touched)
			{
				ReadState where;
				where.board = from_std(board);
				where.unread =
					static_cast<int>(holder->store->index.unread(hub, board));

				for (const std::string& tth : written)
				{
					if (holder->store->index.isRead(hub, board, tth))
						where.read.append(from_std(tth));
				}

				stored.boards.append(where);
			}

			return stored;
		},
		[this, hubUrl, entries](const Stored& stored) {
			/*
			 * The counts before the entries, so that anything drawing a board on the
			 * strength of entriesStored() draws it with the count that goes with them.
			 */
			for (const ReadState& where : stored.boards)
				noteRead(hubUrl, where.board, where.unread, where.read, false);

			/*
			 * Before the entries as well, so that a view told about one can ask whether
			 * its document is here and get the answer that belongs with it. No state is
			 * derived from being held - what verifying came to was not written down, and
			 * calling a held document verified would draw a post the hub and the document
			 * disagree about as one they agreed on. Opening it reads it, off this disk.
			 */
			for (const QString& tth : stored.held) m_held.insert(tth);
			for (const QString& tth : stored.failed) m_held.insert(tth);

			/*
			 * Which hub each document is answered for, so that the user turning the
			 * setting off for this one stops offering what it took up.
			 */
			for (const QString& tth : stored.offered) m_fetchedFrom.insert(tth, hubUrl);

			QHash<QString, QVector<BoardEntry>> byBoard;
			bool withdrawn = false;

			for (int n : stored.kept)
			{
				const BoardEntry& entry = entries.at(n);
				byBoard[entry.board].append(entry);

				if (!entry.removed) continue;

				withdrawn = true;
				setState(entry.tth, PostState::Withdrawn);
			}

			for (auto at = byBoard.constBegin(); at != byBoard.constEnd(); ++at)
				emit entriesStored(hubUrl, at.key(), at.value());

			/*
			 * After them, because this is a state and a state is about a post the view has
			 * been told about. A document that is here and is not the post it claims to be
			 * is not an unfetched one: every copy of it is these same bytes, so there is
			 * nothing to fetch and nothing a reader could do by asking.
			 */
			for (const QString& tth : stored.failed)
				setState(tth, PostState::Unavailable);

			/*
			 * Nothing is fetched here, and that is a rule rather than an omission.
			 * A subscription is cheap and a body is not; the stream delivers an
			 * entry within a second of the hub accepting it, so a prompt fetch names
			 * a reader who was online and subscribed at that moment - to the author
			 * first of all, who is the only source until somebody else has read it.
			 */
			if (withdrawn) releasePending();
		});
}

void Qdc::BoardBridge::describe(const QString& hubUrl, const BoardInfo& board)
{
	if (!m_thread || board.name.isEmpty()) return;

	const QString name = board.name;

	/*
	 * A board that has ceased to exist is described by a descriptor carrying its name
	 * and the flag, and nothing else - so recording it would record an oldest
	 * timestamp of zero and turn a view with a hole in it into a complete one. The
	 * cached entries are still there and are still worth reading, so what is held is
	 * reported and the record is left as it was.
	 */
	if (board.removed)
	{
		requestState(hubUrl, name);
		return;
	}

	m_thread->call(m_relay,
		[holder = m_stores, hub = to_std(hubUrl), described = to_core(board)] {
			BoardState state;

			if (!holder->store) return state;

			holder->store->index.describe(hub, described);

			state.cursor = holder->store->index.cursor(hub, described.name);
			state.view = from_core(holder->store->index.view(hub, described.name));

			return state;
		},
		[this, hubUrl, name](const BoardState& state) {
			/*
			 * A descriptor is the only thing that can turn a complete view of a
			 * board into one with a hole in it: the hub raises the oldest timestamp
			 * it will replay, and what is between that and the cursor is not coming.
			 * So the state is re-reported whenever one arrives, rather than waiting
			 * to be asked.
			 */
			emit boardStateReady(hubUrl, name, state.cursor, state.view);
		});
}

void Qdc::BoardBridge::loadEntries(const QString& hubUrl, const QString& board)
{
	if (!m_thread || board.isEmpty()) return;

	m_thread->call(m_relay,
		[holder = m_stores, hub = to_std(hubUrl), name = to_std(board),
		 serving = serves(hubUrl)] {
			Loaded loaded;

			if (!holder->store) return loaded;

			const std::vector<QuickDC::BoardEntry> held =
				holder->store->index.entries(hub, name);

			loaded.entries.reserve(static_cast<qsizetype>(held.size()));

			/*
			 * What has been read of the board, taken while its entries are being walked
			 * anyway. This is where the copy on the other side is filled: everything
			 * after it is a change to a board that has been read back once.
			 */
			QuickDC::PostStore& posts = holder->store->posts;

			for (const QuickDC::BoardEntry& entry : held)
			{
				loaded.entries.append(from_core(entry));

				if (holder->store->index.isRead(hub, name, entry.tth))
					loaded.read.append(from_std(entry.tth));

				/*
				 * And whether the document is here, in the same walk. Nothing else on
				 * this side ever asks, so without this a post whose document is on the
				 * disk reads as unfetched from the moment a board is reopened - which
				 * it does for one written on this machine as readily as for any other.
				 */
				if (posts.verification(entry.tth)
				    == QuickDC::PostStore::Verification::Failed)
					loaded.failed.append(from_std(entry.tth));
				else if (posts.has(entry.tth))
					loaded.held.append(from_std(entry.tth));

				/*
				 * And answered for again, where the user said to answer for this hub's
				 * posts. An offer is made to the share manager for the session only, so
				 * every document read in an earlier one is held and offered to nobody
				 * until something takes it up again - and this walk, which the board
				 * already does to know what is here, is where that happens. A store full
				 * of posts that no peer can be told about is a client that reads a board
				 * and never carries it.
				 */
				if (serving && posts.serve(entry.tth))
					loaded.offered.append(from_std(entry.tth));
			}

			loaded.unread = static_cast<int>(holder->store->index.unread(hub, name));

			return loaded;
		},
		[this, hubUrl, board](const Loaded& loaded) {
			noteRead(hubUrl, board, loaded.unread, loaded.read, true);

			/*
			 * Before the entries, so that a view told about an entry can ask whether
			 * its document is here and get the answer that goes with it. No state is
			 * derived from being held: what is recorded of a document is the outcome of
			 * verification, and the author the document claims was compared against the
			 * entry's when it was read and written down nowhere - so calling a held
			 * document verified would draw a post the hub and the document disagree
			 * about as one they agreed on. What happens instead is that opening the post
			 * reads it, off this disk, and the state comes back out of the document.
			 */
			for (const QString& tth : loaded.held) m_held.insert(tth);
			for (const QString& tth : loaded.failed) m_held.insert(tth);

			/* Which hub each is answered for; see the same line in store(). */
			for (const QString& tth : loaded.offered) m_fetchedFrom.insert(tth, hubUrl);

			emit entriesLoaded(hubUrl, board, loaded.entries);

			/*
			 * After them, because this is a state and a state is about a post the view
			 * has been told about. A document that is here and is not the post it claims
			 * to be is not an unfetched one: every copy of it is these same bytes, so
			 * there is nothing to fetch and nothing a reader could do by asking. Said
			 * now rather than left to a press this bridge would only refuse.
			 */
			for (const QString& tth : loaded.failed)
				setState(tth, PostState::Unavailable);
		});
}

void Qdc::BoardBridge::requestState(const QString& hubUrl, const QString& board)
{
	if (!m_thread || board.isEmpty()) return;

	m_thread->call(m_relay,
		[holder = m_stores, hub = to_std(hubUrl), name = to_std(board)] {
			BoardState state;

			if (!holder->store) return state;

			state.cursor = holder->store->index.cursor(hub, name);
			state.view = from_core(holder->store->index.view(hub, name));

			return state;
		},
		[this, hubUrl, board](const BoardState& state) {
			emit boardStateReady(hubUrl, board, state.cursor, state.view);
		});
}

void Qdc::BoardBridge::resume(HubBridge* hub, const QString& board)
{
	if (!hub || !m_thread || board.isEmpty()) return;

	const QString key = hubKey(hub->url());

	m_thread->call(m_relay,
		[holder = m_stores, url = to_std(key), name = to_std(board),
		 serving = serves(key)] {
			Resumed resumed;

			if (!holder->store) return resumed;

			resumed.state.cursor = holder->store->index.cursor(url, name);
			resumed.state.view = from_core(holder->store->index.view(url, name));

			if (!serving) return resumed;

			/*
			 * And answered for from here on, which is what subscribing to a board
			 * commits this client to. An offer lasts as long as the process that made
			 * it, so everything read in an earlier session is held and offered to
			 * nobody until something takes it up - and the entries are walked here
			 * rather than when a pane is opened, because a client is a source for the
			 * boards it carries and not for the ones somebody looked at today.
			 */
			for (const QuickDC::BoardEntry& entry :
			     holder->store->index.entries(url, name))
			{
				if (entry.removed) continue;

				if (holder->store->posts.serve(entry.tth))
					resumed.offered.append(from_std(entry.tth));
			}

			return resumed;
		},
		[this, hub, key, board](const Resumed& resumed) {
			/* Which hub each is answered for; see the same line in store(). */
			for (const QString& tth : resumed.offered) m_fetchedFrom.insert(tth, key);

			/*
			 * The hub may be gone by now: this is the answer to a question asked of
			 * another thread. What says so is whether it is still followed, which
			 * its own destruction sees to.
			 */
			if (m_followed.contains(hub))
				hub->subscribeBoard(board, resumed.state.cursor);

			emit boardStateReady(key, board, resumed.state.cursor, resumed.state.view);
		});
}

Qdc::PostState Qdc::BoardBridge::postState(const QString& tth) const
{
	return m_state.value(tth, PostState::Announced);
}

bool Qdc::BoardBridge::isHeld(const QString& tth) const
{
	return m_held.contains(tth);
}

QString Qdc::BoardBridge::attribution(const QString& tth) const
{
	return m_attributed.value(tth);
}

QString Qdc::BoardBridge::boardKey(const QString& hubUrl, const QString& board)
{
	return hubUrl + QLatin1Char('/') + board;
}

bool Qdc::BoardBridge::isRead(const QString& hubUrl, const QString& board,
                              const QString& tth) const
{
	const auto at = m_read.constFind(boardKey(hubUrl, board));

	return at != m_read.constEnd() && at->read.contains(tth);
}

int Qdc::BoardBridge::unread(const QString& hubUrl, const QString& board) const
{
	const auto at = m_read.constFind(boardKey(hubUrl, board));

	return at == m_read.constEnd() ? 0 : at->unread;
}

void Qdc::BoardBridge::noteRead(const QString& hubUrl, const QString& board,
                                int unread, const QVector<QString>& read, bool whole)
{
	if (board.isEmpty()) return;

	Read& held = m_read[boardKey(hubUrl, board)];

	/*
	 * A job that walked the whole board answered for every entry on it, so what it
	 * says stands on its own. One that passed over a few of them says nothing about
	 * the rest, and dropping what is held on the strength of it would forget a post
	 * somebody read a moment ago.
	 */
	QSet<QString> marked = whole ? QSet<QString>() : held.read;

	for (const QString& tth : read) marked.insert(tth);

	if (held.unread == unread && held.read == marked) return;

	held.unread = unread;
	held.read = std::move(marked);

	emit readStateChanged(hubUrl, board);
}

void Qdc::BoardBridge::markRead(const QString& hubUrl, const QString& board,
                                const QString& tth)
{
	if (!m_thread || board.isEmpty() || tth.isEmpty()) return;

	m_thread->call(m_relay,
		[holder = m_stores, hub = to_std(hubUrl), name = to_std(board),
		 post = to_std(tth)] {
			ReadState where;

			if (!holder->store) return where;

			QuickDC::BoardIndex& index = holder->store->index;

			index.markRead(hub, name, post);

			/*
			 * What the index says afterwards, rather than what the marking returned.
			 * markRead() answers false for a post that was already read and false again
			 * where the record could not be written, and those are not the same news;
			 * asking makes the second of them leave the post unread and leave the badge
			 * counting it, which is the failure worth having.
			 */
			if (index.isRead(hub, name, post)) where.read.append(from_std(post));

			where.unread = static_cast<int>(index.unread(hub, name));

			return where;
		},
		[this, hubUrl, board](const ReadState& where) {
			noteRead(hubUrl, board, where.unread, where.read, false);
		});
}

void Qdc::BoardBridge::markAllRead(const QString& hubUrl, const QString& board)
{
	if (!m_thread || board.isEmpty()) return;

	m_thread->call(m_relay,
		[holder = m_stores, hub = to_std(hubUrl), name = to_std(board)] {
			ReadState where;

			if (!holder->store) return where;

			QuickDC::BoardIndex& index = holder->store->index;

			index.markAllRead(hub, name);

			where.unread = static_cast<int>(index.unread(hub, name));

			/*
			 * The whole of what the board reads as, and not what moved. A watermark is
			 * one record standing for every entry below it, so there is no list of what
			 * it marked - and the entries it stands for are right here to be asked
			 * about one at a time. Dismissing a board is one action by one person, so
			 * this walk happens once rather than per entry that arrives.
			 */
			for (const QuickDC::BoardEntry& entry : index.entries(hub, name))
			{
				if (index.isRead(hub, name, entry.tth))
					where.read.append(from_std(entry.tth));
			}

			return where;
		},
		[this, hubUrl, board](const ReadState& where) {
			noteRead(hubUrl, board, where.unread, where.read, true);
		});
}

void Qdc::BoardBridge::setPrefetch(const QString& hubUrl, bool prefetch)
{
	if (prefetch) m_prefetch.insert(hubUrl);
	else m_prefetch.remove(hubUrl);
}

bool Qdc::BoardBridge::prefetches(const QString& hubUrl) const
{
	return m_prefetch.contains(hubUrl);
}

void Qdc::BoardBridge::setServing(const QString& hubUrl, bool serving)
{
	if (serving == m_serving.contains(hubUrl)) return;

	if (serving) m_serving.insert(hubUrl);
	else m_serving.remove(hubUrl);

	if (!m_thread) return;

	/*
	 * The documents this hub's boards were read from, and only those. A document is
	 * named by its own bytes and is the same post on every hub that indexes it, so
	 * the store is keyed globally - but whether it is answered for is a per-hub
	 * setting, and the hub a document was fetched from is the one whose setting
	 * governs it. A post fetched from two hubs follows the first of them, which is
	 * one hub's worth of imprecision in a decision the user makes per hub.
	 */
	std::vector<std::string> documents;

	for (auto at = m_fetchedFrom.constBegin(); at != m_fetchedFrom.constEnd(); ++at)
	{
		if (at.value() != hubUrl) continue;

		documents.push_back(to_std(at.key()));
	}

	if (documents.empty()) return;

	/*
	 * Withheld rather than detached, for the same reason as every other withdrawal
	 * here: marking is safe at any moment, and stopping a peer's transfer dead is
	 * not this layer's decision to take from inside a setting being changed.
	 */
	m_thread->post([holder = m_stores, documents = std::move(documents), serving] {
		if (!holder->store) return;

		for (const std::string& tth : documents)
		{
			if (serving) holder->store->posts.serve(tth);
			else holder->store->posts.withhold(tth);
		}
	});

	if (!serving) releasePending();
}

bool Qdc::BoardBridge::serves(const QString& hubUrl) const
{
	return m_serving.contains(hubUrl);
}

void Qdc::BoardBridge::setFetchCap(quint64 bytes)
{
	m_cap = bytes;
}

quint64 Qdc::BoardBridge::fetchCap() const
{
	return m_cap;
}

void Qdc::BoardBridge::setConcurrentFetches(int fetches)
{
	m_concurrent = qMax(1, fetches);
	startWaiting();
}

int Qdc::BoardBridge::concurrentFetches() const
{
	return m_concurrent;
}

void Qdc::BoardBridge::setBoardAllowance(quint64 bytes, int seconds)
{
	m_allowanceBytes = bytes;
	m_allowanceSeconds = seconds;
}

void Qdc::BoardBridge::setCacheBounds(quint64 ageSeconds, quint64 totalBytes)
{
	m_ageBound = ageSeconds;
	m_totalBound = totalBytes;
}

bool Qdc::BoardBridge::passesCap(quint64 received, quint64 declared, quint64 cap)
{
	if (cap > 0 && received > cap) return true;

	return declared > 0 && received > declared;
}

void Qdc::BoardBridge::fetch(const QString& hubUrl, const BoardEntry& entry)
{
	if (!is_tiger_root(entry.tth))
	{
		/*
		 * A hash that is not a tiger root names no document, could never be
		 * fetched, and must not reach a filename. Refused here rather than passed
		 * on, because everything below this takes the hash as a name.
		 */
		giveUp(entry.tth, tr("The hub named this post with something that is not a "
		                     "hash, so there is nothing to fetch."));
		return;
	}

	if (entry.removed)
	{
		/*
		 * A tombstone is the more particular thing to know about a post than "the
		 * body is not here", so it is what is recorded. The replies to it are
		 * separate posts and are unaffected.
		 */
		setState(entry.tth, PostState::Withdrawn);
		giveUp(entry.tth, tr("This post has been withdrawn."));
		return;
	}

	/*
	 * Already on its way, or already being asked about. Two clicks are not two
	 * reasons to ask a peer, and the store is asked on the core thread first - so
	 * without the last of these a second click would arrive while the first was
	 * still in the air and charge the fetch twice.
	 */
	if (m_byTth.contains(entry.tth) || m_asking.contains(entry.tth)) return;

	for (const auto& waiting : m_waiting)
	{
		if (waiting.second.tth == entry.tth) return;
	}

	if (!m_thread)
	{
		giveUp(entry.tth, tr("The core is not running."));
		return;
	}

	m_asking.insert(entry.tth);

	/*
	 * Asked of the store first, on the core thread. A document already held and
	 * already verified is a post that can be read without asking anybody for
	 * anything, and a document already known not to be the post it claims to be
	 * must not be fetched, checked and refused over and over.
	 */
	m_thread->call(m_relay,
		[holder = m_stores, tth = to_std(entry.tth)] {
			if (!holder->store) return Known();

			return what_is_known(holder->store->posts, tth);
		},
		[this, hubUrl, entry](const Known& known) {
			m_asking.remove(entry.tth);

			if (known.failed)
			{
				m_held.insert(entry.tth);

				giveUp(entry.tth,
				       tr("The document behind this post is not the post it claims "
				          "to be, and every copy of it is these same bytes."),
				       true);
				return;
			}

			/*
			 * Held already, so it is read and checked again rather than taken on
			 * trust. What is cached is the outcome of verification and not the
			 * reading of it, and this is the one path that turns a document into
			 * something on screen.
			 */
			if (!known.path.isEmpty())
			{
				m_held.insert(entry.tth);

				adopt(hubUrl, entry, known.path);
				return;
			}

			m_held.remove(entry.tth);

			startFetch(hubUrl, entry);
		});
}

void Qdc::BoardBridge::readHeld(const QString& hubUrl, const BoardEntry& entry)
{
	if (!m_thread || !is_tiger_root(entry.tth)) return;

	/*
	 * A tombstone says more about a post than any document could, and it is what a
	 * withdrawn post is drawn as. Nothing is read for one, and nothing is said either:
	 * this was nobody asking for anything.
	 */
	if (entry.removed) return;

	/* On its way already, or already being asked about: two selections are one read. */
	if (m_byTth.contains(entry.tth) || m_asking.contains(entry.tth)) return;

	/*
	 * What this side believes, which is what a view acted on in deciding to call this,
	 * and what the post said before - because if the document has gone since, nothing
	 * has happened and the post has to be left saying what it said.
	 */
	const bool believed = isHeld(entry.tth);
	const PostState before = postState(entry.tth);

	m_asking.insert(entry.tth);

	/*
	 * Said before the store is asked rather than after it answers. Reading a document
	 * off this disk still crosses to the core thread and back, and for that moment the
	 * post must not go on telling the reader that nothing about it has been verified,
	 * with a button offering to fetch a document that is on the disk in front of them.
	 */
	if (believed) setState(entry.tth, PostState::Fetching);

	m_thread->call(m_relay,
		[holder = m_stores, tth = to_std(entry.tth)] {
			if (!holder->store) return Known();

			return what_is_known(holder->store->posts, tth);
		},
		[this, hubUrl, entry, believed, before](const Known& known) {
			m_asking.remove(entry.tth);

			if (known.failed)
			{
				m_held.insert(entry.tth);

				/*
				 * Here, and unreadable for ever: these bytes hash to the name the entry
				 * gave and are not a post document, so every copy of it is these same
				 * bytes. Saying so is the whole answer - there is nothing to ask anybody
				 * for.
				 */
				giveUp(entry.tth,
				       tr("The document behind this post is not the post it claims to "
				          "be, and every copy of it is these same bytes."),
				       true);
				return;
			}

			if (known.path.isEmpty())
			{
				/*
				 * Gone after all - evicted, or released since this side last heard of
				 * it - so nothing is asked of anybody. Falling through to a fetch here
				 * would put a request on the network that no reader made, which is the
				 * one thing this path exists to avoid.
				 */
				m_held.remove(entry.tth);

				if (believed && postState(entry.tth) == PostState::Fetching)
					setState(entry.tth, before);

				return;
			}

			m_held.insert(entry.tth);

			/*
			 * Read and checked again rather than taken on trust, exactly as a fetched
			 * document is: what the store remembers is the outcome of verification and
			 * not the reading of it, and the author the document claims has to be
			 * compared against the entry's again before anything is drawn.
			 */
			adopt(hubUrl, entry, known.path);
		});
}

void Qdc::BoardBridge::prefetchThread(const QString& hubUrl,
                                      const QVector<BoardEntry>& thread)
{
	/*
	 * Off unless this hub has been named, and that is the whole of the check. The
	 * extension asks that nothing be prefetched by default and that whatever is be
	 * a per-hub decision, because reading a post is visible to whoever wrote it and
	 * a client that quietly fetched a thread would be answering for its user.
	 */
	if (!prefetches(hubUrl)) return;

	for (const BoardEntry& entry : thread)
	{
		if (entry.removed) continue;
		if (postState(entry.tth) == PostState::Verified) continue;

		fetch(hubUrl, entry);
	}
}

void Qdc::BoardBridge::startFetch(const QString& hubUrl, const BoardEntry& entry)
{
	/*
	 * The declared size is the author's claim and the cap is this client's own. An
	 * entry declaring more than the cap is not asked for at all - and a peer cannot
	 * be trusted to stop where it said it would, so the bound that means anything is
	 * the one applied below to what has actually arrived.
	 */
	if (m_cap > 0 && entry.size > m_cap)
	{
		giveUp(entry.tth, tr("This post is said to be %1 bytes, which is more than "
		                     "this client will fetch for one post.").arg(entry.size));
		return;
	}

	if (m_fetching.size() >= m_concurrent)
	{
		/*
		 * Waiting here rather than in the download queue, which is what keeps a
		 * thread somebody opened from taking the slots of the film they asked for
		 * last night - and what makes closing that thread again free.
		 */
		m_waiting.append({hubUrl, entry});
		return;
	}

	if (!m_transfers)
	{
		giveUp(entry.tth, tr("There is nothing here to fetch a post with."));
		return;
	}

	Fetch fetch;
	fetch.hubUrl = hubUrl;
	fetch.entry = entry;
	fetch.path = QDir(QDir(m_directory).filePath(QStringLiteral("incoming")))
		.filePath(entry.tth);
	fetch.cap = entry.size > 0 && (m_cap == 0 || entry.size < m_cap) ? entry.size
	                                                                : m_cap;

	/*
	 * Nothing bounds this fetch: the entry declared no size and the cap has been
	 * turned off. Refused rather than asked for, since what would be asked of a peer
	 * is "send until you stop", and no answer to that can be checked.
	 */
	if (fetch.cap == 0)
	{
		giveUp(entry.tth, tr("Nothing says how large this post is, and no limit is "
		                     "set on what may be fetched for one."));
		return;
	}

	/*
	 * Charged where the fetch actually goes out, so that one waiting behind the
	 * concurrency bound is charged when it starts rather than when it was asked for.
	 */
	if (!withinAllowance(hubUrl, entry.board, fetch.cap))
	{
		giveUp(entry.tth, tr("This board has been asked for as much as it is allowed "
		                     "for the moment. Try again shortly."));
		return;
	}

	fetch.since.start();

	m_fetching.insert(fetch.path, fetch);
	m_byTth.insert(entry.tth, fetch.path);

	setState(entry.tth, PostState::Fetching);

	/*
	 * The size handed to the queue is the cap rather than the declaration, so that a
	 * peer answering an entry which declared nothing is bounded too. What goes on
	 * the wire is "TTH/{root}", built from the hash rather than from the name, and
	 * the hash is what the queue checks the result against.
	 */
	m_transfers->enqueueForReading(fetch.path, entry.tth, fetch.cap, hubUrl);

	if (!m_watch->isActive()) m_watch->start();
}

void Qdc::BoardBridge::startWaiting()
{
	while (!m_waiting.isEmpty() && m_fetching.size() < m_concurrent)
	{
		const std::pair<QString, BoardEntry> next = m_waiting.takeFirst();
		startFetch(next.first, next.second);
	}
}

void Qdc::BoardBridge::giveUp(const QString& tth, const QString& reason, bool unreadable)
{
	m_asking.remove(tth);

	const QString path = m_byTth.take(tth);

	if (!path.isEmpty())
	{
		m_fetching.remove(path);

		/*
		 * The partial file goes with it. They are bytes nothing describes - named
		 * after a hash they did not turn out to have - and worth nothing to the next
		 * attempt, which starts again from whoever answers next.
		 */
		if (m_transfers) m_transfers->removeFromQueue(path, true);
	}

	/*
	 * A withdrawn post is not an unavailable one. The tombstone is what is known
	 * about it and it is the more particular of the two, so it stands.
	 */
	if (postState(tth) != PostState::Withdrawn)
		setState(tth, PostState::Unavailable);

	if (unreadable) emit postUnreadable(tth, reason);
	else emit postUnavailable(tth, reason);

	startWaiting();
}

void Qdc::BoardBridge::setState(const QString& tth, PostState state)
{
	if (tth.isEmpty()) return;

	const auto at = m_state.constFind(tth);
	if (at != m_state.constEnd() && *at == state) return;

	m_state.insert(tth, state);
	emit postStateChanged(tth, state);
}

bool Qdc::BoardBridge::withinAllowance(const QString& hubUrl, const QString& board,
                                       quint64 bytes)
{
	if (m_allowanceBytes == 0 || m_allowanceSeconds <= 0) return true;

	Allowance& allowance = m_allowances[boardKey(hubUrl, board)];

	if (!allowance.since.isValid()
	    || allowance.since.elapsed() > m_allowanceSeconds * 1000LL)
	{
		allowance.since.start();
		allowance.spent = 0;
	}

	if (allowance.spent + bytes > m_allowanceBytes) return false;

	allowance.spent += bytes;
	return true;
}

void Qdc::BoardBridge::nameAuthors()
{
	if (!m_transfers) return;

	for (Fetch& fetch : m_fetching)
	{
		if (fetch.authorNamed) continue;
		if (fetch.since.elapsed() < AUTHOR_GRACE_MS) continue;

		fetch.authorNamed = true;

		/*
		 * The search has had its chance, so the author is named at last. A source
		 * other than the entry's own CID is preferred wherever a search turns one
		 * up, because an author who records who fetches their post has a census of
		 * its readers - but a post nobody else holds is unreadable without them, and
		 * unreadable is worse than seen to be read.
		 */
		m_transfers->addFallbackSource(fetch.entry.tth, fetch.hubUrl,
		                               fetch.entry.cid);
	}
}

void Qdc::BoardBridge::noteProgress(const QVector<TransferInfo>& transfers)
{
	for (const TransferInfo& row : transfers)
	{
		if (row.upload) continue;

		const auto at = m_fetching.find(row.filename);
		if (at == m_fetching.end()) continue;
		if (row.transferred <= at->moved) continue;

		/*
		 * Bytes are arriving, so the clock starts again. One clock does for both
		 * questions this answers: a fetch that is moving has a source, so there is
		 * nothing to be gained by naming the author, and nothing to give up on.
		 */
		at->moved = row.transferred;
		at->since.start();
	}
}

void Qdc::BoardBridge::giveUpOnTheUnanswered()
{
	QVector<QString> lost;

	for (const Fetch& fetch : m_fetching)
	{
		if (fetch.since.elapsed() < FETCH_TIMEOUT_MS) continue;

		lost.append(fetch.entry.tth);
	}

	/*
	 * Collected first, because giving up takes the fetch out of what is being walked.
	 * The entry is untouched and is still worth showing: this says that nobody who
	 * has the document could be reached, which is neither an empty post nor an error,
	 * and asking again later is what a reader does next.
	 */
	for (const QString& tth : lost)
	{
		giveUp(tth, tr("Nobody who has this post could be reached. It may be on a "
		               "client that is offline; try again later."));
	}
}

void Qdc::BoardBridge::abandonOverCap(const QVector<TransferInfo>& transfers)
{
	for (const TransferInfo& row : transfers)
	{
		if (row.upload) continue;

		const auto at = m_fetching.constFind(row.filename);
		if (at == m_fetching.constEnd()) continue;

		if (!passesCap(row.transferred, at->entry.size, at->cap)) continue;

		giveUp(at->entry.tth,
		       tr("The peer sending this post sent more than the post was said to "
		          "be, so the transfer was abandoned."));
	}
}

void Qdc::BoardBridge::adopt(const QString& hubUrl, const BoardEntry& entry,
                             const QString& path)
{
	if (!m_thread || path.isEmpty()) return;

	const bool serving = serves(hubUrl);
	const quint64 cap = m_cap;

	m_thread->call(m_relay,
		[holder = m_stores, file = to_std(path), wanted = to_core(entry), serving,
		 cap] {
			Verdict verdict;
			verdict.post.tth = from_std(wanted.tth);
			verdict.post.state = PostState::Unavailable;

			if (!holder->store)
			{
				verdict.reason = BoardBridge::tr("There is nowhere to keep posts.");
				return verdict;
			}

			QuickDC::PostStore& posts = holder->store->posts;

			/*
			 * Whether this is the store's own copy, which decides whether it is
			 * adopted: adopting a document already held removes the file it was
			 * handed, and handing over the store's own path would be the store
			 * deleting the document it holds.
			 */
			const bool held = posts.has(wanted.tth)
				&& !file.empty() && posts.pathFor(wanted.tth) == file;

			/*
			 * What is here before anything is decided, so that a document already in the
			 * store is still reported as held when it cannot even be opened.
			 */
			verdict.held = held;

			QFile document(from_std(file));

			if (!document.open(QIODevice::ReadOnly))
			{
				verdict.reason = BoardBridge::tr("The document could not be read.");
				return verdict;
			}

			/*
			 * One byte past the cap, so that a file over it is recognised as over it
			 * rather than read in full and then measured. The declared size is not
			 * consulted here at all: it is the author's claim, and this is the check
			 * that does not rest on one.
			 */
			const QByteArray bytes = cap > 0
				? document.read(static_cast<qint64>(cap) + 1) : document.readAll();

			document.close();

			if (cap > 0 && static_cast<quint64>(bytes.size()) > cap)
			{
				verdict.reason = BoardBridge::tr(
					"The document is larger than this client will read for one "
					"post.");
				return verdict;
			}

			/*
			 * Step 1, and everything else rests on it. Until this has passed an
			 * index entry is a stranger's description of a file that may not exist.
			 * A copy that fails it is discarded entirely rather than remembered:
			 * these bytes say nothing about the post, and the next peer to answer
			 * may have the document itself.
			 */
			if (tiger_tree_root(bytes) != from_std(wanted.tth))
			{
				if (held) posts.record(wanted.tth,
				                       QuickDC::PostStore::Verification::Failed);
				else QFile::remove(from_std(file));

				verdict.reason = BoardBridge::tr(
					"What arrived is not the post that hash names, so it was thrown "
					"away.");
				return verdict;
			}

			/*
			 * Step 2. Refused and never repaired: the document was asked for by
			 * hash, so a reader that repaired one would hold bytes that no longer
			 * hash to the name it requested - and would serve the repair on to the
			 * next reader under the original name.
			 */
			QuickDC::Post parsed;

			if (!ADC::parsePost(bytes.constData(),
			                    static_cast<size_t>(bytes.size()), parsed))
			{
				/*
				 * These bytes are the post - they hash to its name - and they are
				 * not a post document. Every copy of it is these same bytes, so it
				 * is unreadable for ever: kept and remembered as failed, so that it
				 * is not fetched and refused again, and offered to nobody.
				 */
				if (!held) verdict.held = posts.adopt(wanted.tth, file);

				posts.record(wanted.tth, QuickDC::PostStore::Verification::Failed);

				verdict.reason = BoardBridge::tr(
					"The document is not a post: its header does not parse.");
				return verdict;
			}

			/* Step 3. The document is inside the hash and the entry is not. */
			verdict.post.cid = from_std(parsed.cid);
			verdict.post.parent = from_std(parsed.parent);
			verdict.post.subject = from_std(parsed.subject);

			/*
			 * Read as UTF-8, which is what the extension says a header is; a body is
			 * raw bytes, and one that cannot be decoded is replaced rather than
			 * refused. These bytes hashed to the name they were asked for, so this
			 * is the post, and a post is not unreadable for a byte its author got
			 * wrong.
			 */
			verdict.post.body = from_std(parsed.body);

			verdict.post.dated = parsed.date.has_value();
			verdict.post.date = parsed.date.value_or(0);
			verdict.post.richText = parsed.isRichText();

			/*
			 * Step 4, which has no automatic resolution. The document says who wrote
			 * the post and the hub says who submitted it: two claims about two
			 * events. Where they agree the post is that CID's; where they disagree
			 * it is attributed to the hub's claim with the document's shown as a
			 * claim, because the honest case - a post relayed to a second hub by
			 * somebody who did not write it - is indistinguishable from a forgery.
			 */
			verdict.post.state = parsed.cid == wanted.cid ? PostState::Verified
			                                              : PostState::Disputed;

			if (!held) verdict.held = posts.adopt(wanted.tth, file);

			posts.record(wanted.tth, QuickDC::PostStore::Verification::Verified);

			/*
			 * Offered only now, and only where the user said to answer for this
			 * hub's posts. A disputed post is still the post its hash names - the
			 * disagreement is about who wrote it, not about what it says - so it is
			 * served like any other document that verified.
			 */
			if (serving) posts.serve(wanted.tth);

			return verdict;
		},
		[this, hubUrl, entry](const Verdict& verdict) {
			const QString tth = entry.tth;

			m_asking.remove(tth);

			const QString path = m_byTth.take(tth);

			if (!path.isEmpty()) m_fetching.remove(path);

			/*
			 * Whether the document is here now, which is not the same news as whether
			 * the post can be read: a document that hashes to its name and does not
			 * parse is kept for ever precisely because every copy of it is these same
			 * bytes, and it is answered from the disk rather than fetched again.
			 */
			if (verdict.held) m_held.insert(tth);
			else m_held.remove(tth);

			if (verdict.post.state == PostState::Unavailable)
			{
				setState(tth, PostState::Unavailable);

				/*
				 * Which of the two failures this was, said here rather than worked out
				 * from what the store happens to hold afterwards. The document being
				 * here is the mark of one of them and not of the other, but it is also
				 * the mark of a copy that is here and merely could not be opened - and
				 * that is a bad afternoon, not a post that can never be read.
				 */
				if (verdict.held) emit postUnreadable(tth, verdict.reason);
				else emit postUnavailable(tth, verdict.reason);

				startWaiting();
				return;
			}

			/*
			 * Attributed to the hub's claim, always, and remembered so that a pane
			 * holding only the post can say who it is from without going near the
			 * index.
			 */
			m_attributed.insert(tth, entry.cid);
			m_fetchedFrom.insert(tth, hubUrl);

			setState(tth, verdict.post.state);
			emit postReady(tth, verdict.post);

			startWaiting();
		});
}

void Qdc::BoardBridge::evictNow()
{
	if (!m_thread) return;

	const quint64 age = m_ageBound;
	const quint64 total = m_totalBound;

	if (age == 0 && total == 0)
	{
		/* Nothing is bounded, so nothing is chosen - but marks may still be due. */
		releasePending();
		return;
	}

	const quint64 now = static_cast<quint64>(QDateTime::currentSecsSinceEpoch());

	m_thread->call(m_relay,
		[holder = m_stores, age, total, now] {
			if (!holder->store) return 0;

			QuickDC::PostStore::Bounds bounds;
			bounds.age = age;
			bounds.total = total;

			/*
			 * Marks and does not delete, which is what makes eviction safe to run
			 * from a timer at all: choosing what should go is safe at any moment,
			 * and saying when it may go is somebody else's business.
			 */
			return static_cast<int>(holder->store->posts.evict(bounds, now).size());
		},
		[this](int marked) {
			if (marked > 0) releasePending();
		});
}

void Qdc::BoardBridge::releasePending()
{
	if (!m_thread) return;

	/*
	 * What is being sent to somebody, as this thread last saw it. An upload holds an
	 * interior pointer into the object that withdrawing a hash destroys, so a
	 * document named by one of these is left marked and released later - and nothing
	 * is lost by waiting, since a marked document is still readable and still
	 * correct.
	 */
	QVector<TransferInfo> uploading;

	if (m_transfers)
	{
		for (const TransferInfo& row : m_transfers->activeTransfers())
		{
			if (!row.upload) continue;

			uploading.append(row);
		}
	}

	std::vector<std::string> named;
	named.reserve(uploading.size());

	for (const TransferInfo& row : uploading)
		named.push_back(to_std(QFileInfo(row.filename).fileName()));

	m_thread->post([holder = m_stores, thread = m_thread, named = std::move(named)] {
		if (!holder->store) return;

		/*
		 * Asked of the transfer manager as well as of the snapshot above, because a
		 * snapshot is as old as the last reading and an upload may have started
		 * since. This is the reading that cannot be stale: it is taken in the same
		 * job as the release, between turns of the reactor, so nothing can begin
		 * serving a file between the two.
		 */
		QVector<TransferInfo> live;

		for (const std::string& name : named)
		{
			TransferInfo row;
			row.upload = true;
			row.filename = from_std(name);

			live.append(row);
		}

		QuickDC::Core* core = thread->core();

		if (core && core->transfers)
		{
			for (QuickDC::Transfer* transfer = core->transfers->first(); transfer;
			     transfer = core->transfers->next())
			{
				if (!transfer->isUpload()) continue;

				TransferInfo row;
				row.upload = true;

				if (Samurai::IO::File* file = transfer->getFile())
					row.filename = QString::fromStdString(file->getName());

				live.append(row);
			}
		}

		QVector<QString> pending;

		for (const std::string& tth : holder->store->posts.pending())
			pending.append(from_std(tth));

		for (const QString& tth : BoardBridge::releasable(pending, live))
			holder->store->posts.release(to_std(tth));
	});
}

QVector<QString> Qdc::BoardBridge::releasable(const QVector<QString>& pending,
                                              const QVector<TransferInfo>& transfers)
{
	if (pending.isEmpty()) return pending;

	QSet<QString> reading;

	for (const TransferInfo& row : transfers)
	{
		if (!row.upload) continue;
		if (row.filename.isEmpty()) continue;

		/*
		 * A document is filed under its hash and under nothing else, so a file being
		 * sent names the document it holds by its last path component. Anything else
		 * being uploaded is a shared file and is not this store's to withdraw.
		 */
		reading.insert(QFileInfo(row.filename).fileName());
	}

	QVector<QString> allowed;
	allowed.reserve(pending.size());

	for (const QString& tth : pending)
	{
		if (reading.contains(tth)) continue;

		allowed.append(tth);
	}

	return allowed;
}

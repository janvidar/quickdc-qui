/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "boardbridge.h"

#include "corechannel.h"
#include "corethread.h"
#include "hubbridge.h"
#include "transferbridge.h"



#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QPointer>

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

Qdc::BoardBridge::View from_core(QuickDC::Api::BoardView view)
{
	switch (view)
	{
		case QuickDC::Api::BoardView::Complete: return Qdc::BoardBridge::View::Complete;
		case QuickDC::Api::BoardView::Gap:      return Qdc::BoardBridge::View::Gap;
		case QuickDC::Api::BoardView::Unknown:  break;
	}
	return Qdc::BoardBridge::View::Unknown;
}

QVector<QString> from_std(const std::vector<std::string>& list)
{
	QVector<QString> out;
	out.reserve(static_cast<qsizetype>(list.size()));
	for (const std::string& text : list) out.append(from_std(text));
	return out;
}

}

template<typename Reply, typename Command, typename Then>
void Qdc::BoardBridge::ask(Command command, Then then)
{
	if (!m_thread) return;

	command.store = m_store;
	command.request = ++m_nextRequest;

	if (!m_thread->submit(command)) return;

	m_replies.emplace(command.request, [then = std::move(then)](const QuickDC::Api::Event& event) {
		if (const Reply* reply = event.as<Reply>()) then(*reply);
	});
}

void Qdc::BoardBridge::onCoreEvent(const QuickDC::Api::Event& event)
{
	std::visit([this](const auto& e) {
		if constexpr (requires { e.store; e.request; })
		{
			if (e.store != m_store) return;

			const auto found = m_replies.find(e.request);
			if (found == m_replies.end()) return;

			const auto reply = std::move(found->second);
			m_replies.erase(found);
			reply(QuickDC::Api::Event(e));
		}
	}, event.value);
}

Qdc::BoardBridge::BoardBridge(CoreThread* thread, TransferBridge* transfers,
                              const QString& directory, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
	, m_transfers(transfers)
	, m_directory(directory.isEmpty()
		? QDir(QDir::homePath()).filePath(QStringLiteral(".quickdc"))
		: directory)
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

	static QuickDC::Api::BoardStoreHandle stores = 0;
	m_store = ++stores;

	connect(m_thread->channel(), &CoreChannel::event, this, &BoardBridge::onCoreEvent);

	m_thread->submit(QuickDC::Api::Commands::OpenBoardStore{
		m_store, to_std(boards), to_std(documents), to_std(incoming)});
}

Qdc::BoardBridge::~BoardBridge()
{
	/* Marks to release are kept; the next opening of the store releases them. */
	if (m_thread && m_store)
		m_thread->submit(QuickDC::Api::Commands::CloseBoardStore{m_store});
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

	QuickDC::Api::Commands::StoreBoardEntries command;
	command.hubUrl = to_std(hubUrl);
	command.serving = serves(hubUrl);
	command.entries.reserve(static_cast<size_t>(entries.size()));
	for (const BoardEntry& entry : entries) command.entries.push_back(to_core(entry));

	ask<QuickDC::Api::Events::BoardEntriesStored>(std::move(command),
		[this, hubUrl, entries](const QuickDC::Api::Events::BoardEntriesStored& stored) {
			for (const QuickDC::Api::BoardReadState& where : stored.boards)
				noteRead(hubUrl, from_std(where.board), where.unread, from_std(where.read), false);

			for (const std::string& tth : stored.held) m_held.insert(from_std(tth));
			for (const std::string& tth : stored.failed) m_held.insert(from_std(tth));
			for (const std::string& tth : stored.offered) m_fetchedFrom.insert(from_std(tth), hubUrl);

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

			for (const std::string& tth : stored.failed)
				setState(from_std(tth), PostState::Unavailable);

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

	ask<QuickDC::Api::Events::BoardStateReported>(
		QuickDC::Api::Commands::DescribeBoard{0, 0, to_std(hubUrl), to_core(board)},
		[this, hubUrl, name](const QuickDC::Api::Events::BoardStateReported& state) {
			emit boardStateReady(hubUrl, name, state.cursor, from_core(state.view));
		});
}

void Qdc::BoardBridge::loadEntries(const QString& hubUrl, const QString& board)
{
	if (!m_thread || board.isEmpty()) return;

	ask<QuickDC::Api::Events::BoardLoaded>(
		QuickDC::Api::Commands::LoadBoard{0, 0, to_std(hubUrl), to_std(board), serves(hubUrl)},
		[this, hubUrl, board](const QuickDC::Api::Events::BoardLoaded& loaded) {
			noteRead(hubUrl, board, loaded.unread, from_std(loaded.read), true);

			for (const std::string& tth : loaded.held) m_held.insert(from_std(tth));
			for (const std::string& tth : loaded.failed) m_held.insert(from_std(tth));
			for (const std::string& tth : loaded.offered) m_fetchedFrom.insert(from_std(tth), hubUrl);

			QVector<BoardEntry> entries;
			entries.reserve(static_cast<qsizetype>(loaded.entries.size()));
			for (const QuickDC::BoardEntry& entry : loaded.entries) entries.append(from_core(entry));

			emit entriesLoaded(hubUrl, board, entries);

			for (const std::string& tth : loaded.failed)
				setState(from_std(tth), PostState::Unavailable);
		});
}

void Qdc::BoardBridge::requestState(const QString& hubUrl, const QString& board)
{
	if (!m_thread || board.isEmpty()) return;

	ask<QuickDC::Api::Events::BoardStateReported>(
		QuickDC::Api::Commands::QueryBoardState{0, 0, to_std(hubUrl), to_std(board)},
		[this, hubUrl, board](const QuickDC::Api::Events::BoardStateReported& state) {
			emit boardStateReady(hubUrl, board, state.cursor, from_core(state.view));
		});
}

void Qdc::BoardBridge::resume(HubBridge* hub, const QString& board)
{
	if (!hub || !m_thread || board.isEmpty()) return;

	const QString key = hubKey(hub->url());
	const QPointer<HubBridge> following(hub);

	ask<QuickDC::Api::Events::BoardResumed>(
		QuickDC::Api::Commands::ResumeBoard{0, 0, to_std(key), to_std(board), serves(key)},
		[this, following, key, board](const QuickDC::Api::Events::BoardResumed& resumed) {
			for (const std::string& tth : resumed.offered) m_fetchedFrom.insert(from_std(tth), key);

			/* Still followed: a hub left while the answer was on its way is not taken up again. */
			if (following && m_followed.contains(following.data()))
				following->subscribeBoard(board, resumed.cursor);

			emit boardStateReady(key, board, resumed.cursor, from_core(resumed.view));
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
	 * A request that walked the whole board answered for every entry on it, so what it
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

	ask<QuickDC::Api::Events::BoardReadChanged>(
		QuickDC::Api::Commands::MarkRead{0, 0, to_std(hubUrl), to_std(board), to_std(tth)},
		[this, hubUrl, board](const QuickDC::Api::Events::BoardReadChanged& changed) {
			noteRead(hubUrl, board, changed.state.unread, from_std(changed.state.read), false);
		});
}

void Qdc::BoardBridge::markAllRead(const QString& hubUrl, const QString& board)
{
	if (!m_thread || board.isEmpty()) return;

	ask<QuickDC::Api::Events::BoardReadChanged>(
		QuickDC::Api::Commands::MarkRead{0, 0, to_std(hubUrl), to_std(board), std::string()},
		[this, hubUrl, board](const QuickDC::Api::Events::BoardReadChanged& changed) {
			noteRead(hubUrl, board, changed.state.unread, from_std(changed.state.read), true);
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
	m_thread->submit(QuickDC::Api::Commands::ServePosts{m_store, std::move(documents), serving});

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
	ask<QuickDC::Api::Events::PostKnown>(
		QuickDC::Api::Commands::QueryPost{0, 0, to_std(entry.tth)},
		[this, hubUrl, entry](const QuickDC::Api::Events::PostKnown& known) {
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

			if (!known.path.empty())
			{
				m_held.insert(entry.tth);
				adopt(hubUrl, entry, from_std(known.path));
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

	ask<QuickDC::Api::Events::PostKnown>(
		QuickDC::Api::Commands::QueryPost{0, 0, to_std(entry.tth)},
		[this, hubUrl, entry, believed, before](const QuickDC::Api::Events::PostKnown& known) {
			m_asking.remove(entry.tth);

			if (known.failed)
			{
				m_held.insert(entry.tth);
				giveUp(entry.tth,
				       tr("The document behind this post is not the post it claims to "
				          "be, and every copy of it is these same bytes."),
				       true);
				return;
			}

			if (known.path.empty())
			{
				m_held.remove(entry.tth);
				if (believed && postState(entry.tth) == PostState::Fetching)
					setState(entry.tth, before);
				return;
			}

			m_held.insert(entry.tth);
			adopt(hubUrl, entry, from_std(known.path));
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

		/*
		 * Copied out before giving up, because giving up is what takes this entry out of
		 * the map: the hash would otherwise be a reference into a node that had been
		 * destroyed by the time the state was set and the signal built from it.
		 */
		const QString tth = at->entry.tth;

		giveUp(tth, tr("The peer sending this post sent more than the post was said to "
		               "be, so the transfer was abandoned."));
	}
}

void Qdc::BoardBridge::adopt(const QString& hubUrl, const BoardEntry& entry,
                             const QString& path)
{
	if (!m_thread || path.isEmpty()) return;

	using Adopted = QuickDC::Api::Events::PostAdopted;

	ask<Adopted>(
		QuickDC::Api::Commands::AdoptPost{0, 0, to_std(path), to_core(entry), serves(hubUrl), m_cap},
		[this, hubUrl, entry](const Adopted& verdict) {
			const QString tth = entry.tth;
			m_asking.remove(tth);

			const QString fetched = m_byTth.take(tth);
			if (!fetched.isEmpty()) m_fetching.remove(fetched);

			if (verdict.held) m_held.insert(tth);
			else m_held.remove(tth);

			QString reason;
			switch (verdict.outcome)
			{
				case Adopted::Outcome::Verified:
				case Adopted::Outcome::Disputed:
					break;
				case Adopted::Outcome::NoStore:
					reason = tr("There is nowhere to keep posts.");
					break;
				case Adopted::Outcome::Unreadable:
					reason = tr("The document could not be read.");
					break;
				case Adopted::Outcome::TooLarge:
					reason = tr("The document is larger than this client will read for one post.");
					break;
				case Adopted::Outcome::WrongHash:
					reason = tr("What arrived is not the post that hash names, so it was thrown away.");
					break;
				case Adopted::Outcome::NotAPost:
					reason = tr("The document is not a post: its header does not parse.");
					break;
			}

			if (!reason.isEmpty())
			{
				setState(tth, PostState::Unavailable);
				if (verdict.held) emit postUnreadable(tth, reason);
				else emit postUnavailable(tth, reason);
				startWaiting();
				return;
			}

			BoardPost post;
			post.tth = tth;
			post.cid = from_std(verdict.post.cid);
			post.parent = from_std(verdict.post.parent);
			post.subject = from_std(verdict.post.subject);
			post.body = from_std(verdict.post.body);
			post.dated = verdict.post.dated;
			post.date = verdict.post.date;
			post.richText = verdict.post.richText;
			post.state = verdict.outcome == Adopted::Outcome::Verified ? PostState::Verified
			                                                         : PostState::Disputed;

			m_attributed.insert(tth, entry.cid);
			m_fetchedFrom.insert(tth, hubUrl);
			setState(tth, post.state);
			emit postReady(tth, post);
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

	ask<QuickDC::Api::Events::PostsEvicted>(
		QuickDC::Api::Commands::EvictPosts{0, 0, age, total, now},
		[this](const QuickDC::Api::Events::PostsEvicted& evicted) {
			if (evicted.marked > 0) releasePending();
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

	m_thread->submit(QuickDC::Api::Commands::ReleasePosts{m_store, std::move(named)});
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

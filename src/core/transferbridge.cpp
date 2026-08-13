/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "transferbridge.h"

#include "corethread.h"

#include <api/core.h>
#include <api/transfers.h>
#include <network/downloadqueue.h>
#include <network/filesource.h>
#include <samurai/io/file.h>

#include <QByteArray>
#include <QSet>

#include <QMetaObject>

#include <vector>

namespace {

/**
 * How long a file with nothing moving is still counted as being transferred.
 *
 * Long enough to cover the gap between one chunk being answered and the next -
 * which is a round trip to the peer, and is not even asked for until the finished
 * one has been reported - and short enough that something which really has stopped
 * does not sit there. A few readings' worth either way.
 */
constexpr int IDLE_GRACE_MS = 3000;

/**
 * The shortest interval a rate is measured over.
 *
 * Readings are asked for whenever something happens as well as on a timer, and
 * two of them a few milliseconds apart say nothing about a rate - the figure
 * would be however much happened to have arrived in that instant, multiplied up.
 */
constexpr int RATE_INTERVAL_MIN_MS = 250;

Qdc::TransferInfo convert(QuickDC::Transfer* transfer)
{
	Qdc::TransferInfo info;
	if (!transfer) return info;

	/*
	 * The address, as an opaque row key. Never dereferenced again - by the time
	 * a view reads this the transfer may be gone - which is also why it is
	 * everything else here that has to be copied out now.
	 */
	info.id = reinterpret_cast<quintptr>(transfer);

	if (Samurai::IO::File* file = transfer->getFile())
		info.filename = QString::fromStdString(file->getName());

	info.transferred     = transfer->getTransferred();
	info.length          = transfer->getLength();
	info.bytesPerSecond  = transfer->getBps();
	info.ratio           = transfer->getRatio();
	info.upload          = transfer->isUpload();
	info.finished        = transfer->isFinished();

	return info;
}

QuickDC::QueueFile::Priority convert(Qdc::Priority priority)
{
	switch (priority)
	{
		case Qdc::Priority::Paused: return QuickDC::QueueFile::PrioPaused;
		case Qdc::Priority::Low:    return QuickDC::QueueFile::PrioLow;
		case Qdc::Priority::Normal: return QuickDC::QueueFile::PrioNormal;
		case Qdc::Priority::High:   return QuickDC::QueueFile::PrioHigh;
	}

	return QuickDC::QueueFile::PrioNormal;
}

Qdc::Priority convert(QuickDC::QueueFile::Priority priority)
{
	switch (priority)
	{
		case QuickDC::QueueFile::PrioPaused: return Qdc::Priority::Paused;
		case QuickDC::QueueFile::PrioLow:    return Qdc::Priority::Low;
		case QuickDC::QueueFile::PrioNormal: return Qdc::Priority::Normal;
		case QuickDC::QueueFile::PrioHigh:   return Qdc::Priority::High;
	}

	return Qdc::Priority::Normal;
}

/**
 * The name a download will have when it is finished.
 *
 * A download writes into the queue's temporary file, so that is the name its
 * transfers carry - and the name the queue itself keeps is the final one, which
 * is what a view shows and what the two are matched on.
 */
QString final_name(const QString& path)
{
	const QString suffix = QStringLiteral(QUICKDC_PART_SUFFIX);
	return path.endsWith(suffix) ? path.chopped(suffix.size()) : path;
}

Qdc::QueueEntry convert(const QuickDC::QueueStatus& status)
{
	Qdc::QueueEntry entry;

	/* Both strings point into the QueueFile, so neither may outlive this call. */
	entry.filename       = status.filename ? QString::fromUtf8(status.filename) : QString();
	entry.tth            = status.tth ? QString::fromUtf8(status.tth) : QString();
	entry.size           = status.size;
	entry.done           = status.done;
	entry.sources        = static_cast<quint32>(status.sources);
	entry.activeSources  = static_cast<quint32>(status.activeSources);
	entry.priority       = convert(status.priority);
	entry.complete       = status.complete;

	return entry;
}

}

namespace Qdc {

/** Everything one pass over the core gathers, carried back in one piece. */
struct Snapshot {
	QVector<TransferInfo> active;
	QVector<QueueEntry> queued;
	quint32 count = 0;
};

/**
 * The core side half of a TransferBridge.
 *
 * Registered with the core's transfer manager, which calls these from inside
 * Transfer::exec() - so the transfer is still whole and its byte counts and rate
 * are still readable, but it may be deleted moments later. Hence the copy before
 * anything is handed across.
 */
class TransferReporter final : public QuickDC::TransferListener {

public:
	TransferReporter(TransferBridge* bridge, std::shared_ptr<Relay> relay)
		: m_bridge(bridge)
		, m_relay(std::move(relay))
	{
	}

	void EventTransferStarted(QuickDC::Transfer* transfer) override
	{
		report(transfer, &TransferBridge::transferStarted);
	}

	void EventTransferFinished(QuickDC::Transfer* transfer) override
	{
		report(transfer, &TransferBridge::transferFinished);
	}

	void EventTransferStopped(QuickDC::Transfer* transfer) override
	{
		report(transfer, &TransferBridge::transferStopped);
	}

	void EventTransferError(QuickDC::Transfer* transfer) override
	{
		report(transfer, &TransferBridge::transferError);
	}

private:
	using Signal = void (TransferBridge::*)(const TransferInfo&);

	/**
	 * Queued explicitly, for the same reason as in HubBridge: this only ever
	 * runs on the core thread, where an inline call would mean emitting straight
	 * into a view. Addressed to the relay, which a bridge detaches on its way out,
	 * so an event in flight when that happens becomes nothing.
	 */
	void report(QuickDC::Transfer* transfer, Signal signal)
	{
		const TransferInfo info = convert(transfer);
		TransferBridge* bridge = m_bridge;

		std::shared_ptr<Relay> relay = m_relay;
		Relay* target = relay.get();

		QMetaObject::invokeMethod(target,
			[relay = std::move(relay), bridge, signal, info] {
				if (!relay->detached()) (bridge->*signal)(info);
			}, Qt::QueuedConnection);
	}

	TransferBridge* m_bridge;
	std::shared_ptr<Relay> m_relay;
};

/**
 * The queue half.
 *
 * The core calls these from inside QueueManager::process(), and hands over a
 * QueueFile it deletes immediately afterwards - so the completion event copies
 * what it needs on the spot, and the change events carry nothing at all.
 */
class QueueReporter final : public QuickDC::QueueListener {

public:
	QueueReporter(TransferBridge* bridge, std::shared_ptr<Relay> relay)
		: m_bridge(bridge)
		, m_relay(std::move(relay))
	{
	}

	void EventQueueFileAdded(const QuickDC::QueueFile*) override
	{
		announce();
	}

	void EventQueueFileRemoved(const QuickDC::QueueFile*) override
	{
		announce();
	}

	void EventQueueFileVerified(const QuickDC::QueueFile* file) override
	{
		if (!file) return;

		const QString filename = file->getLocalFilename()
			? QString::fromUtf8(file->getLocalFilename()) : QString();
		const QString tth = file->getTTH() ? QString::fromUtf8(file->getTTH())
		                                   : QString();
		const quint64 size = file->getSize();

		TransferBridge* bridge = m_bridge;

		std::shared_ptr<Relay> relay = m_relay;
		Relay* target = relay.get();

		QMetaObject::invokeMethod(target,
			[relay = std::move(relay), bridge, filename, size, tth] {
				if (!relay->detached())
					emit bridge->downloadCompleted(filename, size, tth);
			}, Qt::QueuedConnection);
	}

private:
	void announce()
	{
		TransferBridge* bridge = m_bridge;

		std::shared_ptr<Relay> relay = m_relay;
		Relay* target = relay.get();

		QMetaObject::invokeMethod(target, [relay = std::move(relay), bridge] {
			if (relay->detached()) return;

			emit bridge->queueChanged();
			bridge->refresh();
		}, Qt::QueuedConnection);
	}

	TransferBridge* m_bridge;
	std::shared_ptr<Relay> m_relay;
};

}

Qdc::TransferBridge::TransferBridge(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
	, m_reporters(std::make_shared<ReporterBox>())
	, m_relay(Relay::create())
{
	if (!m_thread) return;

	/*
	 * Posted rather than waited on. Registering a listener happens between turns of
	 * the reactor, and the window is built before anything has been transferred, so
	 * there is nothing to miss by not waiting - and a great deal to lose by waiting,
	 * since the wait is a whole core turn at whatever the core is busy with.
	 */
	m_thread->post([holder = m_reporters, relay = m_relay, thread = m_thread,
	                bridge = this] {
		QuickDC::Core* core = thread->core();
		if (!core || !core->transfers) return;

		holder->transfers = new TransferReporter(bridge, relay);
		core->transfers->addListener(holder->transfers);

		/* A core built without a queue file has no queue to listen to. */
		if (!core->queue) return;

		holder->queue = new QueueReporter(bridge, relay);
		core->queue->addListener(holder->queue);
	});
}

Qdc::TransferBridge::~TransferBridge()
{
	/* Nothing in flight has anywhere to report to from here on. */
	m_relay->detach();

	if (!m_thread) return;

	/*
	 * Posted, and no longer blocking. The manager holds a raw pointer to each
	 * reporter and would call it on the next transfer event, but the call goes to
	 * the relay rather than to this object and the relay is detached above - so an
	 * event landing between here and the job running has nowhere to go.
	 */
	m_thread->post([holder = m_reporters, thread = m_thread] {
		QuickDC::Core* core = thread->core();

		if (core && core->transfers && holder->transfers)
			core->transfers->removeListener(holder->transfers);

		if (core && core->queue && holder->queue)
			core->queue->removeListener(holder->queue);

		delete holder->transfers;
		holder->transfers = nullptr;

		delete holder->queue;
		holder->queue = nullptr;
	});
}

QVector<Qdc::TransferInfo> Qdc::TransferBridge::activeTransfers() const
{
	return m_active;
}

void Qdc::TransferBridge::hidePath(const QString& prefix)
{
	if (prefix.isEmpty() || m_hidden.contains(prefix)) return;

	m_hidden.append(prefix);

	/*
	 * The rows already gathered are dropped here rather than left to age out, so
	 * that a caller which hides a directory before fetching into it and one which
	 * hides it afterwards see the same list.
	 */
	m_active = foldByFile({}, m_queue, m_active, 0, m_hidden);
	m_queue = markTransferring(m_queue, m_active);
}

QStringList Qdc::TransferBridge::hiddenPaths() const
{
	return m_hidden;
}

bool Qdc::TransferBridge::isUnder(const QString& filename,
                                  const QStringList& prefixes)
{
	if (filename.isEmpty() || prefixes.isEmpty()) return false;

	for (const QString& prefix : prefixes)
	{
		if (prefix.isEmpty()) continue;
		if (filename == prefix) return true;

		/*
		 * The separator is what makes this a directory rather than a string: without
		 * it "…/posts" would hide "…/postscript.pdf" as well, which is a file
		 * somebody chose and must not vanish out of their transfer list.
		 */
		const QString within = prefix.endsWith(QLatin1Char('/'))
			? prefix : prefix + QLatin1Char('/');

		if (filename.startsWith(within)) return true;
	}

	return false;
}

QVector<Qdc::TransferInfo> Qdc::TransferBridge::foldByFile(
	const QVector<TransferInfo>& chunks, const QVector<QueueEntry>& queued,
	const QVector<TransferInfo>& previous, int sinceMs, const QStringList& hidden)
{
	QVector<TransferInfo> folded;
	folded.reserve(chunks.size());

	/* Which row each file landed in, and the wire bytes behind that row's ratio. */
	QHash<QString, int> rows;
	QVector<quint64> wire;

	for (const TransferInfo& chunk : chunks)
	{
		/* Only a download has a ".part" to drop; a shared file may be called that. */
		const QString name =
			chunk.upload ? chunk.filename : final_name(chunk.filename);

		/* Somebody else's business, and not this list's; see hidePath(). */
		if (isUnder(name, hidden)) continue;

		/*
		 * A transfer with no file to name cannot be told from another one, so it
		 * keeps a row to itself rather than joining a heap of them.
		 */
		const QString key = name.isEmpty()
			? QStringLiteral("?") + QString::number(chunk.id)
			: (chunk.upload ? QStringLiteral("up:") : QStringLiteral("down:")) + name;

		const quint64 onTheWire =
			static_cast<quint64>(static_cast<double>(chunk.transferred) * chunk.ratio);

		const auto at = rows.constFind(key);

		if (at == rows.constEnd())
		{
			TransferInfo row = chunk;
			row.filename = name;
			row.chunks = 1;

			/*
			 * Stands for the file rather than for one of its transfers, so that a
			 * row keeps its identity as chunks come and go. Two files colliding
			 * here would cost a repaint and nothing more.
			 */
			row.id = static_cast<quintptr>(qHash(key));

			rows.insert(key, static_cast<int>(folded.size()));
			folded.append(row);
			wire.append(onTheWire);
			continue;
		}

		TransferInfo& row = folded[*at];

		row.transferred    += chunk.transferred;
		row.length         += chunk.length;
		row.bytesPerSecond += chunk.bytesPerSecond;
		row.chunks         += 1;

		/* Over only when every part of it is. */
		row.finished = row.finished && chunk.finished;

		wire[*at] += onTheWire;
	}

	for (int n = 0; n < folded.size(); ++n)
	{
		TransferInfo& row = folded[n];

		row.ratio = row.transferred > 0
			? static_cast<double>(wire.at(n)) / static_cast<double>(row.transferred)
			: 1.0;
	}

	QHash<QString, int> entries;
	for (int n = 0; n < queued.size(); ++n)
		entries.insert(queued.at(n).filename, n);

	/*
	 * Held on where there is nothing moving for the moment, and in the order the
	 * rows were last in. What decides whether a quiet row is still a transfer is
	 * the queue: an entry that is still there and not complete means the file is
	 * between chunks, and anything else means it is over.
	 */
	QHash<quintptr, int> freshById;
	for (int n = 0; n < folded.size(); ++n)
		freshById.insert(folded.at(n).id, n);

	QVector<TransferInfo> merged;
	merged.reserve(folded.size() + previous.size());

	/* What the chunks of each merged row have pulled in but not yet committed. */
	QVector<quint64> inFlight;
	inFlight.reserve(folded.size() + previous.size());

	QSet<quintptr> placed;

	for (const TransferInfo& before : previous)
	{
		if (placed.contains(before.id)) continue;

		/* A row that has only just become somebody else's business goes now. */
		if (isUnder(before.filename, hidden)) continue;

		const auto moving = freshById.constFind(before.id);

		if (moving != freshById.constEnd())
		{
			merged.append(folded.at(*moving));
			inFlight.append(folded.at(*moving).transferred);
			placed.insert(before.id);
			continue;
		}

		TransferInfo quiet = before;
		quiet.chunks = 0;
		quiet.idleMs = before.idleMs + qMax(0, sinceMs);

		/*
		 * The rate is carried for as long as the row is, rather than zeroed as soon
		 * as nothing is moving. A rate of nothing is drawn as an empty cell, so
		 * zeroing it made the column blink between a speed and nothing at all -
		 * which is the flicker itself, only in one column instead of the whole row.
		 * The row is given up on a few seconds later anyway, so the oldest figure
		 * this can show is a few seconds old.
		 */
		if (quiet.idleMs > IDLE_GRACE_MS) continue;

		if (!quiet.upload)
		{
			const auto at = entries.constFind(quiet.filename);
			if (at == entries.constEnd()) continue;
			if (queued.at(*at).complete) continue;
		}

		merged.append(quiet);
		inFlight.append(0);
		placed.insert(quiet.id);
	}

	/* Then whatever has appeared since, at the end. */
	for (const TransferInfo& row : folded)
	{
		if (placed.contains(row.id)) continue;

		merged.append(row);
		inFlight.append(row.transferred);
		placed.insert(row.id);
	}

	QHash<quintptr, int> before;
	for (int n = 0; n < previous.size(); ++n)
		before.insert(previous.at(n).id, n);

	for (int n = 0; n < merged.size(); ++n)
	{
		TransferInfo& row = merged[n];

		/* Nothing uploaded is in the queue; it is what is wanted, not what is held. */
		const auto at = row.upload ? entries.constEnd()
		                           : entries.constFind(row.filename);

		if (at != entries.constEnd() && queued.at(*at).size > 0)
		{
			const QueueEntry& entry = queued.at(*at);

			/*
			 * The file's own figures, plus what the chunks in flight have pulled in
			 * and not yet committed. The queue only counts a chunk once it is whole,
			 * so on its own it steps a chunk at a time; adding what is on its way
			 * makes a bar that follows the bytes.
			 */
			row.transferred = qMin(entry.done + inFlight.at(n), entry.size);
			row.length = entry.size;
		}

		if (row.upload) continue;

		/*
		 * A download's rate is measured here rather than taken from the core, which
		 * estimates per transfer - and a transfer is one chunk, so every chunk starts
		 * a fresh estimate that reads as nothing until it has some seconds behind it.
		 * That is the yoyo: not the transfer changing speed, but the thing measuring
		 * it starting again several times a second. What actually arrived between two
		 * readings does not care where the chunk boundaries fell.
		 */
		const auto was = before.constFind(row.id);
		if (was == before.constEnd()) continue;

		const TransferInfo& last = previous.at(*was);

		/* A chunk that failed has its bytes thrown away, so this can go backwards. */
		const quint64 arrived = row.transferred > last.transferred
			? row.transferred - last.transferred : 0;

		/*
		 * Bytes arriving means the file is moving, whether or not a chunk happened to
		 * be in flight at the instant the reading was taken. Readings are asked for
		 * as transfers end as well as on a timer - and a transfer ending is precisely
		 * the moment when nothing is in flight - so without this a download looked
		 * more idle the more chunks it got through, and was given up on in the middle
		 * of arriving perfectly well.
		 */
		if (arrived > 0) row.idleMs = 0;

		/* Too short an interval to divide by; the figure we had still stands. */
		if (sinceMs < RATE_INTERVAL_MIN_MS)
		{
			row.bytesPerSecond = last.bytesPerSecond;
			continue;
		}

		const quint64 sample = arrived * 1000 / static_cast<quint64>(sinceMs);

		/*
		 * Halfway towards the new figure, so that a reading taken a little early or
		 * late shows as a wobble rather than as a jump - and so that a transfer going
		 * quiet fades out over a couple of readings instead of blinking to nothing.
		 */
		row.bytesPerSecond = (last.bytesPerSecond + sample) / 2;
	}

	return merged;
}

QVector<Qdc::QueueEntry> Qdc::TransferBridge::queueSnapshot() const
{
	return m_queue;
}

QVector<Qdc::QueueEntry> Qdc::TransferBridge::waitingEntries() const
{
	QVector<QueueEntry> waiting;
	waiting.reserve(m_queue.size());

	for (const QueueEntry& entry : m_queue)
	{
		if (!entry.transferring) waiting.append(entry);
	}

	return waiting;
}

QVector<Qdc::QueueEntry> Qdc::TransferBridge::markTransferring(
	const QVector<QueueEntry>& queued, const QVector<TransferInfo>& active)
{
	QSet<QString> moving;

	for (const TransferInfo& row : active)
	{
		if (row.upload) continue;
		if (row.filename.isEmpty()) continue;

		moving.insert(row.filename);
	}

	if (moving.isEmpty()) return queued;

	QVector<QueueEntry> marked = queued;

	for (QueueEntry& entry : marked)
		entry.transferring = moving.contains(entry.filename);

	return marked;
}

quint32 Qdc::TransferBridge::activeTransferCount() const
{
	return m_activeCount;
}

void Qdc::TransferBridge::refresh()
{
	if (!m_thread || m_reading) return;

	m_reading = true;

	/*
	 * Posted rather than waited on. Everything the core is asked for is gathered in
	 * one pass on its own thread and carried back as plain values, which is the same
	 * rule every event here follows: copy what the core lent us, then hand the copy
	 * to the thread that will draw it.
	 */
	m_thread->call(m_relay, [thread = m_thread] {
		QuickDC::Core* core = thread->core();

		Snapshot taken;
		QVector<TransferInfo>& active = taken.active;
		QVector<QueueEntry>& queued = taken.queued;
		quint32& count = taken.count;

		if (core && core->transfers)
		{
			for (QuickDC::Transfer* transfer = core->transfers->first(); transfer;
			     transfer = core->transfers->next())
			{
				active.append(convert(transfer));
			}
		}

		if (core && core->queue)
		{
			std::vector<QuickDC::QueueStatus> status;
			core->queue->getStatus(status);

			queued.reserve(static_cast<qsizetype>(status.size()));
			for (const QuickDC::QueueStatus& entry : status)
				queued.append(convert(entry));

			count = static_cast<quint32>(core->queue->countActiveTransfers());
		}

		return taken;
	}, [this](const Snapshot& taken) {
		/*
		 * Folded here rather than in each view, so that all of them are handed
		 * the same rows and none has to know that one file arrives in several
		 * pieces. On this side of the hand-over rather than the core's, because
		 * holding a row steady means reading the one before it - and the rows
		 * belong to this thread.
		 */
		const int since = m_taken.isValid()
			? static_cast<int>(m_taken.restart()) : 0;

		if (!m_taken.isValid()) m_taken.start();

		m_active = foldByFile(taken.active, taken.queued, m_active, since, m_hidden);

		QVector<QueueEntry> visible;
		visible.reserve(taken.queued.size());

		quint32 count = taken.count;

		for (const QueueEntry& entry : taken.queued)
		{
			if (!isUnder(entry.filename, m_hidden))
			{
				visible.append(entry);
				continue;
			}

			/*
			 * Taken out of the count as well as out of the list. The count is what a
			 * badge is drawn from, and a badge saying ten downloads is the same
			 * disclosure as ten rows saying it - the core counts requests, so this
			 * subtracts them the same way.
			 */
			count -= qMin(count, entry.activeSources);
		}

		m_queue = markTransferring(visible, m_active);
		m_activeCount = count;
		m_reading = false;

		emit snapshotChanged();
	});
}

void Qdc::TransferBridge::setPriority(const QString& filename, Priority priority)
{
	if (!m_thread || filename.isEmpty()) return;

	const QByteArray name = filename.toUtf8();

	/*
	 * Posted, not waited for. Nothing here reads an answer, and a click that waited
	 * would freeze the window for as long as the core's current turn takes.
	 *
	 * The captures are by value because the job outlives this call.
	 */
	m_thread->post([this, name, priority] {
		QuickDC::Core* core = m_thread->core();
		if (!core || !core->queue) return;

		if (QuickDC::QueueFile* file = core->queue->getFileByName(name.constData()))
			file->setPriority(convert(priority));
	});

	/*
	 * Priority is not part of the queue's own change events - it does not add or
	 * remove an entry - so the table is told from here.
	 */
	emit queueChanged();
}

void Qdc::TransferBridge::removeFromQueue(const QString& filename, bool deletePartial)
{
	if (!m_thread || filename.isEmpty()) return;

	const QByteArray name = filename.toUtf8();

	m_thread->post([this, name, deletePartial] {
		QuickDC::Core* core = m_thread->core();
		if (!core || !core->queue) return;

		QuickDC::QueueFile* file = core->queue->getFileByName(name.constData());
		if (!file) return;

		/*
		 * The temporary name has to be read before the entry goes: remove()
		 * deletes the QueueFile that knows it.
		 */
		std::string partial;
		if (deletePartial && file->getTempFilename())
			partial = file->getTempFilename();

		core->queue->remove(file);

		if (!partial.empty()) Samurai::IO::File(partial).remove();
	});
}

void Qdc::TransferBridge::enqueue(const QString& filename, const QString& tth,
                                  quint64 size)
{
	if (!m_thread || filename.isEmpty()) return;

	const QByteArray name = filename.toUtf8();
	const QByteArray hash = tth.toUtf8();

	/*
	 * Posted for the same reason, and this one matters most: a picture arriving in chat
	 * is queued from the message path, so waiting here stopped the interface once per
	 * message on a hub where people send pictures.
	 */
	m_thread->post([this, name, hash, size] {
		QuickDC::Core* core = m_thread->core();
		if (!core || !core->queue) return;

		core->queue->add(name.constData(),
		                 hash.isEmpty() ? nullptr : hash.constData(), size);

		/* The queue's own event reports the new entry, which refreshes the figures. */
	});
}

void Qdc::TransferBridge::enqueueForReading(const QString& filename,
                                            const QString& tth, quint64 size)
{
	if (!m_thread || filename.isEmpty() || tth.isEmpty()) return;

	const QByteArray name = filename.toUtf8();
	const QByteArray hash = tth.toUtf8();

	m_thread->post([this, name, hash, size] {
		QuickDC::Core* core = m_thread->core();
		if (!core || !core->queue) return;

		core->queue->add(name.constData(), hash.constData(), size);

		/*
		 * Asked for at once rather than left to the rotation, and the outcome is not
		 * consulted: a search that was refused for asking too often, or that could
		 * not go out because no hub is logged in, is one the rotation will make
		 * eventually. What matters is that the file somebody is waiting on is not at
		 * the back of a two-minute queue.
		 */
		core->queue->requestSourceSearch(hash.constData());
	});
}

void Qdc::TransferBridge::addFallbackSource(const QString& tth,
                                            const QString& hubUrl,
                                            const QString& cid)
{
	if (!m_thread || tth.isEmpty() || hubUrl.isEmpty() || cid.isEmpty()) return;

	const QByteArray hash = tth.toUtf8();
	const QByteArray hub = hubUrl.toUtf8();
	const QByteArray user = cid.toUtf8();

	m_thread->post([this, hash, hub, user] {
		QuickDC::Core* core = m_thread->core();
		if (!core || !core->queue) return;

		QuickDC::QueueFile* file = core->queue->getFileByTTH(hash.constData());
		if (!file) return;

		/*
		 * Somebody else has answered, so this source is not needed - and the one
		 * being held back is the party who would learn most from being asked. The
		 * question is settled here rather than by the caller because only the queue
		 * knows whether its search found anybody.
		 */
		if (file->countSources() > 0) return;

		core->queue->addKnownSource(hash.constData(), hub.constData(),
		                            user.constData());
	});
}

void Qdc::TransferBridge::enqueue(const QString& filename, const Qdc::SearchHit& hit)
{
	if (!m_thread || filename.isEmpty()) return;

	/* Nowhere to download from, which is the one thing this overload is for. */
	if (hit.hubUrl.isEmpty() || hit.user.sourceId.isEmpty()) return;

	const QByteArray name = filename.toUtf8();
	const QByteArray hash = hit.tth.toUtf8();
	const QByteArray hub = hit.hubUrl.toUtf8();
	const QByteArray source = hit.user.sourceId.toUtf8();
	const QByteArray remote = hit.name.toUtf8();
	const quint64 size = hit.size;

	m_thread->post([this, name, hash, hub, source, remote, size] {
		QuickDC::Core* core = m_thread->core();
		if (!core || !core->queue) return;

		QuickDC::QueueFile* file = core->queue->add(
			name.constData(), hash.isEmpty() ? nullptr : hash.constData(), size);

		if (!file) return;

		/*
		 * The same shape the sessions use when a reply matches something already
		 * queued, in ADC::HubSession::onSearchResult(): a source names the hub, the
		 * user, and what the file is called on their side, and one we already have
		 * is left alone rather than added twice.
		 */
		if (file->findSource(hub.constData(), source.constData())) return;

		file->addSource(new QuickDC::FileSource(
			hub.constData(), source.constData(),
			remote.isEmpty() ? nullptr : remote.constData()));
	});
}

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "transferbridge.h"

#include "corechannel.h"
#include "corethread.h"


#include <QSet>


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

Qdc::TransferInfo convert(const QuickDC::Api::Transfer& transfer)
{
	Qdc::TransferInfo info;
	info.id              = static_cast<quintptr>(transfer.id);
	info.filename        = QString::fromStdString(transfer.filename);
	info.transferred     = transfer.transferred;
	info.length          = transfer.length;
	info.bytesPerSecond  = transfer.bytesPerSecond;
	info.ratio           = transfer.ratio;
	info.upload          = transfer.upload;
	info.finished        = transfer.finished;
	return info;
}

QuickDC::Api::Priority convert(Qdc::Priority priority)
{
	switch (priority)
	{
		case Qdc::Priority::Paused: return QuickDC::Api::Priority::Paused;
		case Qdc::Priority::Low:    return QuickDC::Api::Priority::Low;
		case Qdc::Priority::Normal: return QuickDC::Api::Priority::Normal;
		case Qdc::Priority::High:   return QuickDC::Api::Priority::High;
	}
	return QuickDC::Api::Priority::Normal;
}

Qdc::Priority convert(QuickDC::Api::Priority priority)
{
	switch (priority)
	{
		case QuickDC::Api::Priority::Paused: return Qdc::Priority::Paused;
		case QuickDC::Api::Priority::Low:    return Qdc::Priority::Low;
		case QuickDC::Api::Priority::Normal: return Qdc::Priority::Normal;
		case QuickDC::Api::Priority::High:   return Qdc::Priority::High;
	}
	return Qdc::Priority::Normal;
}

QString final_name(const QString& path)
{
	const QString suffix = QString::fromLatin1(QuickDC::Api::PART_SUFFIX);
	return path.endsWith(suffix) ? path.chopped(suffix.size()) : path;
}

Qdc::QueueEntry convert(const QuickDC::Api::QueueItem& item)
{
	Qdc::QueueEntry entry;
	entry.filename       = QString::fromStdString(item.filename);
	entry.tth            = QString::fromStdString(item.tth);
	entry.size           = item.size;
	entry.done           = item.done;
	entry.sources        = item.sources;
	entry.activeSources  = item.activeSources;
	entry.priority       = convert(item.priority);
	entry.complete       = item.complete;
	return entry;
}

}

Qdc::TransferBridge::TransferBridge(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
{
	if (m_thread)
		connect(m_thread->channel(), &CoreChannel::event, this, &TransferBridge::onCoreEvent);
}

Qdc::TransferBridge::~TransferBridge() = default;

void Qdc::TransferBridge::onCoreEvent(const QuickDC::Api::Event& event)
{
	using namespace QuickDC::Api::Events;

	if (const auto* e = event.as<TransferStarted>())
		emit transferStarted(convert(e->transfer));
	else if (const auto* e = event.as<TransferFinished>())
		emit transferFinished(convert(e->transfer));
	else if (const auto* e = event.as<TransferStopped>())
		emit transferStopped(convert(e->transfer));
	else if (const auto* e = event.as<TransferError>())
		emit transferError(convert(e->transfer));
	else if (event.as<QueueChanged>())
	{
		emit queueChanged();
		refresh();
	}
	else if (const auto* e = event.as<DownloadCompleted>())
	{
		emit downloadCompleted(QString::fromStdString(e->filename), e->size,
		                       QString::fromStdString(e->tth));
	}
	else if (const auto* e = event.as<TransfersSnapshot>())
	{
		if (m_reading && e->request == m_request) absorb(*e);
	}
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
	 * What has already been gathered is dropped here rather than left to age out, so
	 * that a caller which hides a directory before fetching into it and one which
	 * hides it afterwards are shown the same list.
	 */
	QVector<TransferInfo> rows;
	rows.reserve(m_active.size());

	for (const TransferInfo& row : m_active)
	{
		if (!isUnder(row.filename, m_hidden)) rows.append(row);
	}

	QVector<QueueEntry> visible;
	visible.reserve(m_queue.size());

	for (const QueueEntry& entry : m_queue)
	{
		if (!isUnder(entry.filename, m_hidden)) visible.append(entry);
	}

	m_active = rows;
	m_queue = visible;
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

	static quint64 requests = 0;
	const quint64 request = ++requests;

	if (m_thread->submit(QuickDC::Api::Commands::SnapshotTransfers{request}))
	{
		m_reading = true;
		m_request = request;
	}
}

void Qdc::TransferBridge::absorb(const QuickDC::Api::Events::TransfersSnapshot& taken)
{
	QVector<TransferInfo> active;
	active.reserve(static_cast<qsizetype>(taken.active.size()));
	for (const QuickDC::Api::Transfer& transfer : taken.active) active.append(convert(transfer));

	QVector<QueueEntry> queued;
	queued.reserve(static_cast<qsizetype>(taken.queued.size()));
	for (const QuickDC::Api::QueueItem& item : taken.queued) queued.append(convert(item));

	const int since = m_taken.isValid() ? static_cast<int>(m_taken.restart()) : 0;
	if (!m_taken.isValid()) m_taken.start();

	m_active = foldByFile(active, queued, m_active, since, m_hidden);

	QVector<QueueEntry> visible;
	visible.reserve(queued.size());
	quint32 count = taken.activeDownloads;

	for (const QueueEntry& entry : queued)
	{
		if (!isUnder(entry.filename, m_hidden))
		{
			visible.append(entry);
			continue;
		}

		/* A hidden download's connections are not this view's to count either. */
		count -= qMin(count, entry.activeSources);
	}

	m_queue = markTransferring(visible, m_active);
	m_activeCount = count;
	m_reading = false;

	emit snapshotChanged();
}

void Qdc::TransferBridge::setPriority(const QString& filename, Priority priority)
{
	if (!m_thread || filename.isEmpty()) return;
	m_thread->submit(QuickDC::Api::Commands::SetPriority{filename.toStdString(), convert(priority)});
}

void Qdc::TransferBridge::removeFromQueue(const QString& filename, bool deletePartial)
{
	if (!m_thread || filename.isEmpty()) return;
	m_thread->submit(QuickDC::Api::Commands::RemoveFromQueue{filename.toStdString(), deletePartial});
}

void Qdc::TransferBridge::enqueue(const QString& filename, const QString& tth, quint64 size)
{
	if (!m_thread || filename.isEmpty()) return;
	m_thread->submit(QuickDC::Api::Commands::Enqueue{filename.toStdString(), tth.toStdString(), size});
}

void Qdc::TransferBridge::enqueueForReading(const QString& filename, const QString& tth,
                                            quint64 size, const QString& hubUrl)
{
	if (!m_thread || filename.isEmpty() || tth.isEmpty()) return;
	m_thread->submit(QuickDC::Api::Commands::EnqueueForReading{
		filename.toStdString(), tth.toStdString(), size, hubUrl.toStdString()});
}

void Qdc::TransferBridge::addFallbackSource(const QString& tth, const QString& hubUrl,
                                            const QString& cid)
{
	if (!m_thread || tth.isEmpty() || hubUrl.isEmpty() || cid.isEmpty()) return;
	m_thread->submit(QuickDC::Api::Commands::AddFallbackSource{
		tth.toStdString(), hubUrl.toStdString(), cid.toStdString()});
}

void Qdc::TransferBridge::enqueue(const QString& filename, const Qdc::SearchHit& hit)
{
	if (!m_thread || filename.isEmpty()) return;
	if (hit.hubUrl.isEmpty() || hit.user.sourceId.isEmpty()) return;

	m_thread->submit(QuickDC::Api::Commands::EnqueueFromSearch{
		filename.toStdString(), hit.tth.toStdString(), hit.size,
		hit.hubUrl.toStdString(), hit.user.sourceId.toStdString(), hit.name.toStdString()});
}

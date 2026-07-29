/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "transferbridge.h"

#include "corethread.h"

#include <api/core.h>
#include <api/transfers.h>
#include <samurai/io/file.h>

#include <QByteArray>

#include <QMetaObject>

#include <vector>

namespace {

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
	explicit TransferReporter(TransferBridge* bridge)
		: m_bridge(bridge)
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
	 * into a view. The bridge is the context object, so a bridge that has gone
	 * away takes its pending events with it.
	 */
	void report(QuickDC::Transfer* transfer, Signal signal)
	{
		const TransferInfo info = convert(transfer);
		TransferBridge* bridge = m_bridge;

		QMetaObject::invokeMethod(bridge, [bridge, signal, info] {
			(bridge->*signal)(info);
		}, Qt::QueuedConnection);
	}

	TransferBridge* m_bridge;
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
	explicit QueueReporter(TransferBridge* bridge)
		: m_bridge(bridge)
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

		QMetaObject::invokeMethod(bridge, [bridge, filename, size, tth] {
			emit bridge->downloadCompleted(filename, size, tth);
		}, Qt::QueuedConnection);
	}

private:
	void announce()
	{
		TransferBridge* bridge = m_bridge;

		QMetaObject::invokeMethod(bridge, [bridge] {
			emit bridge->queueChanged();
		}, Qt::QueuedConnection);
	}

	TransferBridge* m_bridge;
};

}

Qdc::TransferBridge::TransferBridge(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
{
	if (!m_thread) return;

	m_thread->invoke([this] {
		QuickDC::Core* core = m_thread->core();
		if (!core || !core->transfers) return;

		m_reporter = new TransferReporter(this);
		core->transfers->addListener(m_reporter);

		/* A core built without a queue file has no queue to listen to. */
		if (!core->queue) return;

		m_queueReporter = new QueueReporter(this);
		core->queue->addListener(m_queueReporter);
	});
}

Qdc::TransferBridge::~TransferBridge()
{
	if (!m_thread || (!m_reporter && !m_queueReporter)) return;

	/*
	 * Blocking: the manager holds a raw pointer to the reporter and would call
	 * it on the next transfer event. Unregistering has to be finished before
	 * this object goes.
	 */
	m_thread->invoke([this] {
		QuickDC::Core* core = m_thread->core();
		if (core && core->transfers)
			core->transfers->removeListener(m_reporter);

		if (core && core->queue && m_queueReporter)
			core->queue->removeListener(m_queueReporter);

		delete m_reporter;
		m_reporter = nullptr;

		delete m_queueReporter;
		m_queueReporter = nullptr;
	});
}

QVector<Qdc::TransferInfo> Qdc::TransferBridge::activeTransfers() const
{
	QVector<TransferInfo> transfers;
	if (!m_thread) return transfers;

	m_thread->invoke([this, &transfers] {
		QuickDC::Core* core = m_thread->core();
		if (!core || !core->transfers) return;

		/*
		 * first()/next() walk one iterator that lives in the manager, so this
		 * has to run to completion before anything else uses it. On the core
		 * thread, inside a job, nothing can.
		 */
		for (QuickDC::Transfer* transfer = core->transfers->first();
		     transfer;
		     transfer = core->transfers->next())
		{
			transfers.append(convert(transfer));
		}
	});

	return transfers;
}

QVector<Qdc::QueueEntry> Qdc::TransferBridge::queueSnapshot() const
{
	QVector<QueueEntry> entries;
	if (!m_thread) return entries;

	m_thread->invoke([this, &entries] {
		QuickDC::Core* core = m_thread->core();
		if (!core || !core->queue) return;

		std::vector<QuickDC::QueueStatus> status;
		core->queue->getStatus(status);

		entries.reserve(static_cast<qsizetype>(status.size()));
		for (const QuickDC::QueueStatus& entry : status)
			entries.append(convert(entry));
	});

	return entries;
}

quint32 Qdc::TransferBridge::activeTransferCount() const
{
	quint32 count = 0;
	if (!m_thread) return count;

	m_thread->invoke([this, &count] {
		QuickDC::Core* core = m_thread->core();
		if (core && core->queue)
			count = static_cast<quint32>(core->queue->countActiveTransfers());
	});

	return count;
}

void Qdc::TransferBridge::setPriority(const QString& filename, Priority priority)
{
	if (!m_thread || filename.isEmpty()) return;

	const QByteArray name = filename.toUtf8();

	m_thread->invoke([this, &name, priority] {
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

	m_thread->invoke([this, &name, deletePartial] {
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

	m_thread->invoke([this, &name, &hash, size] {
		QuickDC::Core* core = m_thread->core();
		if (!core || !core->queue) return;

		core->queue->add(name.constData(),
		                 hash.isEmpty() ? nullptr : hash.constData(), size);
	});
}

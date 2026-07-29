/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "filelistbridge.h"

#include "corethread.h"

#include <api/core.h>
#include <api/filelist.h>
#include <samurai/io/file.h>

#include <QTimer>

namespace {

/**
 * How often the pending set is checked.
 *
 * A browse completes when the peer gets round to it, so this only decides how
 * long an arrived list sits unnoticed. A second is short enough not to be felt
 * and long enough that an idle window is doing nothing.
 */
constexpr int POLL_INTERVAL_MS = 1000;

/** Recursively copy the core's decoded tree into value types. */
Qdc::RemoteDirInfo convert(const Share::RemoteDir* dir)
{
	Qdc::RemoteDirInfo info;
	if (!dir) return info;

	info.name = QString::fromUtf8(dir->getName());
	info.size = dir->getSize();
	info.fileCount = dir->getFiles();

	const size_t files = dir->countEntries();
	info.files.reserve(files);

	for (size_t n = 0; n < files; ++n)
	{
		const Share::RemoteFile* file = dir->getEntry(n);
		if (!file) continue;

		Qdc::RemoteFileInfo entry;
		entry.name = QString::fromUtf8(file->getName());
		entry.size = file->getSize();

		/* Absent from a list that predates TTH, so not every entry has one. */
		if (const char* tth = file->getTTH()) entry.tth = QString::fromUtf8(tth);

		info.files.push_back(std::move(entry));
	}

	const size_t subdirs = dir->countSubdirs();
	info.dirs.reserve(subdirs);

	for (size_t n = 0; n < subdirs; ++n)
	{
		if (const Share::RemoteDir* subdir = dir->getSubdir(n))
			info.dirs.push_back(convert(subdir));
	}

	return info;
}

}

Qdc::FilelistBridge::FilelistBridge(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
	, m_timer(new QTimer(this))
{
	m_timer->setInterval(POLL_INTERVAL_MS);
	connect(m_timer, &QTimer::timeout, this, &FilelistBridge::poll);
}

void Qdc::FilelistBridge::request(const QString& hubUrl, const QString& sourceId)
{
	if (!m_thread || hubUrl.isEmpty() || sourceId.isEmpty()) return;
	if (isPending(hubUrl, sourceId)) return;

	const QByteArray hub = hubUrl.toUtf8();
	const QByteArray cid = sourceId.toUtf8();
	QString localFile;

	m_thread->invoke([this, &hub, &cid, &localFile] {
		QuickDC::Core* core = m_thread->core();
		if (!core || !core->queue) return;

		QuickDC::FilelistRequest* request =
			core->queue->requestFilelist(hub.constData(), cid.constData());

		if (!request) return;

		if (const char* path = request->getLocalFilename())
			localFile = QString::fromUtf8(path);

		/*
		 * Run the queue now rather than waiting for the next turn, so the request
		 * goes out immediately. The reference client does the same in cmd_browse().
		 */
		core->queue->process();
	});

	if (localFile.isEmpty())
	{
		emit filelistFailed(hubUrl, sourceId,
			tr("The download queue would not take the request."));
		return;
	}

	m_pending.append(Pending{hubUrl, sourceId, localFile});

	if (!m_timer->isActive()) m_timer->start();
}

bool Qdc::FilelistBridge::isPending(const QString& hubUrl, const QString& sourceId) const
{
	for (const Pending& pending : m_pending)
	{
		if (pending.hubUrl == hubUrl && pending.sourceId == sourceId) return true;
	}

	return false;
}

int Qdc::FilelistBridge::pendingCount() const
{
	return static_cast<int>(m_pending.size());
}

void Qdc::FilelistBridge::poll()
{
	if (!m_thread || m_pending.isEmpty())
	{
		m_timer->stop();
		return;
	}

	/*
	 * What each outstanding request resolved to, decided in one pass on the core
	 * thread. Collected rather than acted on there: emitting from inside a job
	 * would run a slot on the core thread.
	 */
	struct Outcome {
		int index = 0;
		bool done = false;
		bool ready = false;
		RemoteDirInfo root;
	};

	QList<Outcome> outcomes;

	m_thread->invoke([this, &outcomes] {
		QuickDC::Core* core = m_thread->core();
		if (!core || !core->queue) return;

		for (int n = 0; n < m_pending.size(); ++n)
		{
			const Pending& pending = m_pending.at(n);

			const QByteArray hub = pending.hubUrl.toUtf8();
			const QByteArray cid = pending.sourceId.toUtf8();

			/* Still queued or in flight; nothing to say yet. */
			if (core->queue->getFilelistRequest(hub.constData(), cid.constData()))
				continue;

			Outcome outcome;
			outcome.index = n;
			outcome.done = true;

			/*
			 * The request is gone, which means either the list arrived or the
			 * queue gave up on the peer. The file being there is the only way to
			 * tell the two apart.
			 */
			Samurai::IO::File file(pending.localFile.toStdString());
			if (!file.exists())
			{
				outcomes.append(outcome);
				continue;
			}

			Share::RemoteFileList list(&file);
			if (list.decode())
			{
				outcome.ready = true;
				outcome.root = convert(list.root);
			}

			outcomes.append(outcome);
		}
	});

	/*
	 * Removed back to front, so that an earlier removal does not shift the index
	 * of a later one.
	 */
	for (int n = static_cast<int>(outcomes.size()) - 1; n >= 0; --n)
	{
		const Outcome& outcome = outcomes.at(n);
		if (!outcome.done) continue;

		const Pending pending = m_pending.takeAt(outcome.index);

		if (outcome.ready)
			emit filelistReady(pending.hubUrl, pending.sourceId, outcome.root);
		else
			emit filelistFailed(pending.hubUrl, pending.sourceId,
				tr("The file list did not arrive."));
	}

	if (m_pending.isEmpty()) m_timer->stop();
}

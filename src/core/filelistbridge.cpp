/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "filelistbridge.h"

#include "corethread.h"

#include <api/attachments.h>
#include <api/core.h>
#include <api/filelist.h>
#include <api/hub.h>
#include <network/hubmanager.h>
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

/** What one outstanding request turned out to be, decided on the core thread. */
struct Outcome {
	QString hubUrl;
	QString sourceId;
	bool ready = false;
	Qdc::RemoteDirInfo root;
};

/** What making a request came to; see FilelistBridge::request(). */
struct Started {
	/** Where a browse over the network will land, or empty if none was made. */
	QString localFile;

	/** This names our own share, so nothing was asked of anybody. */
	bool ours = false;

	bool decoded = false;
	Qdc::RemoteDirInfo root;
};

/**
 * Our own file list, read off this disk.
 *
 * The share manager writes it as the share is hashed, so it is normally current; it
 * is built here only when there is none at all, which is a share still being read at
 * start-up.
 */
Qdc::RemoteDirInfo own_filelist(QuickDC::Share::Manager* shares, bool* decoded)
{
	Qdc::RemoteDirInfo root;
	*decoded = false;

	if (!shares) return root;

	const std::string path = shares->getFilelistDirectory() + "/files.xml";

	if (!Samurai::IO::File(path).exists()) shares->createFileLists(true);

	/* A fresh handle, because the one above was asked its question before the
	   list existed. */
	Samurai::IO::File file(path);
	if (!file.exists()) return root;

	Share::RemoteFileList list(&file);
	if (!list.decode()) return root;

	*decoded = true;
	return convert(list.root);
}

}

Qdc::FilelistBridge::FilelistBridge(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
	, m_relay(Relay::create())
	, m_timer(new QTimer(this))
{
	m_timer->setInterval(POLL_INTERVAL_MS);
	connect(m_timer, &QTimer::timeout, this, &FilelistBridge::poll);
}

Qdc::FilelistBridge::~FilelistBridge()
{
	/* A pass still out with the core has nowhere to report to now. */
	m_relay->detach();
}

void Qdc::FilelistBridge::request(const QString& hubUrl, const QString& sourceId)
{
	if (!m_thread || hubUrl.isEmpty() || sourceId.isEmpty()) return;
	if (isPending(hubUrl, sourceId)) return;

	const QByteArray hub = hubUrl.toUtf8();
	const QByteArray cid = sourceId.toUtf8();

	/*
	 * Recorded before the core has been asked, so that a second request for the same
	 * peer while this one is still out is turned away rather than made twice. Where
	 * it will land is filled in when the core says, and the entry is taken out again
	 * if the queue would not have it.
	 */
	m_pending.append(Pending{hubUrl, sourceId, QString()});

	m_thread->call(m_relay, [thread = m_thread, hub, cid] {
		Started started;

		QuickDC::Core* core = thread->core();
		if (!core) return started;

		/* Ourselves, which is read off this disk rather than asked for. */
		QuickDC::Hub* here = (core->hubs) ? core->hubs->getHubByUrl(hub.constData())
		                                  : 0;

		if (here && here->isLocalUser(cid.constData()))
		{
			started.ours = true;
			started.root = own_filelist(core->shares, &started.decoded);
			return started;
		}

		if (!core->queue) return started;

		QuickDC::FilelistRequest* request =
			core->queue->requestFilelist(hub.constData(), cid.constData());

		if (!request) return started;

		if (const char* path = request->getLocalFilename())
			started.localFile = QString::fromUtf8(path);

		/*
		 * Run the queue now rather than waiting for the next turn, so the request
		 * goes out immediately. The reference client does the same in cmd_browse().
		 */
		core->queue->process();

		return started;
	}, [this, hubUrl, sourceId](const Started& started) {
		for (int n = 0; n < m_pending.size(); ++n)
		{
			if (m_pending.at(n).hubUrl != hubUrl) continue;
			if (m_pending.at(n).sourceId != sourceId) continue;

			if (started.ours)
			{
				/* Answered already, so there is nothing for the poll to wait for. */
				m_pending.removeAt(n);

				if (started.decoded)
					emit filelistReady(hubUrl, sourceId, started.root);
				else
					emit filelistFailed(hubUrl, sourceId,
						tr("This client's own file list could not be read."));

				if (m_pending.isEmpty()) m_timer->stop();
				return;
			}

			if (started.localFile.isEmpty())
			{
				m_pending.removeAt(n);

				emit filelistFailed(hubUrl, sourceId,
					tr("The download queue would not take the request."));

				if (m_pending.isEmpty()) m_timer->stop();
				return;
			}

			m_pending[n].localFile = started.localFile;

			if (!m_timer->isActive()) m_timer->start();
			return;
		}
	});
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

	if (m_polling) return;

	/*
	 * Carried into the job by value rather than read from there. The list belongs to
	 * this thread, and a job that walked it would be reading it while this thread is
	 * free to add to it - which it no longer is prevented from doing, now that
	 * nothing waits here.
	 */
	QList<Pending> asking;

	for (const Pending& pending : m_pending)
	{
		if (!pending.localFile.isEmpty()) asking.append(pending);
	}

	if (asking.isEmpty()) return;

	m_polling = true;

	m_thread->call(m_relay, [thread = m_thread, asking] {
		QList<Outcome> outcomes;

		QuickDC::Core* core = thread->core();
		if (!core || !core->queue) return outcomes;

		for (const Pending& pending : asking)
		{
			const QByteArray hub = pending.hubUrl.toUtf8();
			const QByteArray cid = pending.sourceId.toUtf8();

			/* Still queued or in flight; nothing to say yet. */
			if (core->queue->getFilelistRequest(hub.constData(), cid.constData()))
				continue;

			Outcome outcome;
			outcome.hubUrl = pending.hubUrl;
			outcome.sourceId = pending.sourceId;

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

		return outcomes;
	}, [this](const QList<Outcome>& outcomes) {
		m_polling = false;

		for (const Outcome& outcome : outcomes)
		{
			/*
			 * Looked up again rather than by the index the job saw: nothing stopped
			 * this thread adding to or taking from the list while the core was
			 * working through the copy it was given.
			 */
			for (int n = 0; n < m_pending.size(); ++n)
			{
				if (m_pending.at(n).hubUrl != outcome.hubUrl) continue;
				if (m_pending.at(n).sourceId != outcome.sourceId) continue;

				m_pending.removeAt(n);

				if (outcome.ready)
					emit filelistReady(outcome.hubUrl, outcome.sourceId, outcome.root);
				else
					emit filelistFailed(outcome.hubUrl, outcome.sourceId,
						tr("The file list did not arrive."));

				break;
			}
		}

		if (m_pending.isEmpty()) m_timer->stop();
	});
}

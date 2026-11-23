/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "filelistbridge.h"

#include "corechannel.h"
#include "corethread.h"



namespace {

Qdc::RemoteDirInfo converted(const QuickDC::Api::RemoteDirectory& dir)
{
	Qdc::RemoteDirInfo info;
	info.name = QString::fromStdString(dir.name);
	info.size = dir.size;
	info.fileCount = dir.fileCount;

	info.files.reserve(dir.files.size());
	for (const QuickDC::Api::RemoteFile& file : dir.files)
	{
		Qdc::RemoteFileInfo entry;
		entry.name = QString::fromStdString(file.name);
		entry.size = file.size;
		entry.tth = QString::fromStdString(file.tth);
		info.files.push_back(std::move(entry));
	}

	info.dirs.reserve(dir.dirs.size());
	for (const QuickDC::Api::RemoteDirectory& subdir : dir.dirs)
		info.dirs.push_back(converted(subdir));

	return info;
}

}

Qdc::FilelistBridge::FilelistBridge(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
{
	if (m_thread)
		connect(m_thread->channel(), &CoreChannel::event, this, &FilelistBridge::onCoreEvent);
}

Qdc::FilelistBridge::~FilelistBridge() = default;

void Qdc::FilelistBridge::request(const QString& hubUrl, const QString& sourceId)
{
	if (!m_thread || hubUrl.isEmpty() || sourceId.isEmpty()) return;
	if (isPending(hubUrl, sourceId)) return;

	if (!m_thread->submit(QuickDC::Api::Commands::RequestFilelist{
			hubUrl.toStdString(), sourceId.toStdString()}))
		return;

	m_pending.append(Pending{hubUrl, sourceId});
}

bool Qdc::FilelistBridge::isPending(const QString& hubUrl, const QString& sourceId) const
{
	for (const Pending& pending : m_pending)
		if (pending.hubUrl == hubUrl && pending.sourceId == sourceId) return true;
	return false;
}

int Qdc::FilelistBridge::pendingCount() const
{
	return static_cast<int>(m_pending.size());
}

bool Qdc::FilelistBridge::take(const QString& hubUrl, const QString& sourceId)
{
	for (int n = 0; n < m_pending.size(); ++n)
	{
		if (m_pending.at(n).hubUrl != hubUrl || m_pending.at(n).sourceId != sourceId)
			continue;
		m_pending.removeAt(n);
		return true;
	}
	return false;
}

void Qdc::FilelistBridge::onCoreEvent(const QuickDC::Api::Event& event)
{
	using namespace QuickDC::Api::Events;

	if (const auto* ready = event.as<FilelistReady>())
	{
		const QString hub = QString::fromStdString(ready->hubUrl);
		const QString source = QString::fromStdString(ready->sourceId);
		if (take(hub, source)) emit filelistReady(hub, source, converted(ready->root));
	}
	else if (const auto* failed = event.as<FilelistFailed>())
	{
		const QString hub = QString::fromStdString(failed->hubUrl);
		const QString source = QString::fromStdString(failed->sourceId);
		if (!take(hub, source)) return;

		switch (failed->why)
		{
			case FilelistFailed::Why::Refused:
				emit filelistFailed(hub, source, tr("The download queue would not take the request."));
				break;

			case FilelistFailed::Why::OwnListUnreadable:
				emit filelistFailed(hub, source, tr("This client's own file list could not be read."));
				break;

			case FilelistFailed::Why::NotArrived:
				emit filelistFailed(hub, source, tr("The file list did not arrive."));
				break;
		}
	}
}

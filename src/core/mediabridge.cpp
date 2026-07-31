/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "mediabridge.h"

#include "corethread.h"

#include <api/attachments.h>
#include <api/core.h>

#include <QMetaObject>

namespace Qdc {

/**
 * The core side half of a MediaBridge.
 *
 * Built and destroyed inside jobs on the core thread, and only ever called from there.
 * Every event copies what the core lent it before handing the copy across, for the same
 * reason the hub events do: the strings point into the core's own buffers.
 */
class Attacher final : public QuickDC::Share::AttachmentListener {

public:
	explicit Attacher(MediaBridge* bridge)
		: m_bridge(bridge)
	{
	}

	void EventAttachmentReady(const char* path, const char* tth, uint64_t size) override
	{
		const QString file = QString::fromUtf8(path ? path : "");
		const QString hash = QString::fromUtf8(tth ? tth : "");
		const quint64 bytes = size;

		report([file, hash, bytes](MediaBridge* bridge) {
			emit bridge->attached(file, hash, bytes);
		});
	}

	void EventAttachmentFailed(const char* path, const char* reason) override
	{
		const QString file = QString::fromUtf8(path ? path : "");
		const QString why = QString::fromUtf8(reason ? reason : "");

		report([file, why](MediaBridge* bridge) {
			emit bridge->attachFailed(file, why);
		});
	}

private:
	using Work = std::function<void(MediaBridge*)>;

	void report(Work work)
	{
		MediaBridge* bridge = m_bridge;
		QMetaObject::invokeMethod(bridge, [bridge, work] { work(bridge); },
		                          Qt::QueuedConnection);
	}

	MediaBridge* m_bridge;
};

}

Qdc::MediaBridge::MediaBridge(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
{
	if (!m_thread) return;

	m_thread->invoke([this] { m_attacher = new Attacher(this); });
}

Qdc::MediaBridge::~MediaBridge()
{
	if (!m_thread || !m_attacher) return;

	/*
	 * Torn down on the core thread, and only after the core has stopped calling it: the
	 * share manager holds this pointer for as long as a hash job is outstanding.
	 */
	m_thread->invoke([this] {
		delete m_attacher;
		m_attacher = nullptr;
	});
}

void Qdc::MediaBridge::attach(const QString& path)
{
	if (path.isEmpty()) return;

	if (!m_thread || !m_attacher)
	{
		emit attachFailed(path, tr("The core is not running."));
		return;
	}

	const QByteArray local = path.toUtf8();

	m_thread->post([this, local, path] {
		QuickDC::Core* core = m_thread->core();

		if (!core || !core->shares)
		{
			const QString message = tr("There is nothing to offer files from.");
			QMetaObject::invokeMethod(this, [this, path, message] {
				emit attachFailed(path, message);
			}, Qt::QueuedConnection);
			return;
		}

		if (core->shares->attach(local.constData(), m_attacher)) return;

		const QString message = tr("'%1' cannot be offered: it is not a file that can "
		                           "be read.").arg(path);

		QMetaObject::invokeMethod(this, [this, path, message] {
			emit attachFailed(path, message);
		}, Qt::QueuedConnection);
	});
}

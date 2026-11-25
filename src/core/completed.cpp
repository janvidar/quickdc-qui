/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "completed.h"

#include "corechannel.h"
#include "corethread.h"

#include <config/userlists.h>

namespace {

namespace Ev = QuickDC::Api::Events;

QList<Qdc::CompletedDownload> copied(const Ev::CompletedDownloads& told)
{
	QList<Qdc::CompletedDownload> entries;
	for (const QuickDC::Api::CompletedDownload& one : told.downloads)
	{
		Qdc::CompletedDownload entry;
		entry.filename = QString::fromStdString(one.filename);
		entry.tth = QString::fromStdString(one.tth);
		entry.size = one.size;
		entry.finished = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(one.finished));
		entries.append(entry);
	}
	return entries;
}

}

Qdc::Completed::Completed(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
{
	if (!m_thread) return;

	if (const Ev::CompletedDownloads* known = m_thread->channel()->latest<Ev::CompletedDownloads>())
		m_entries = copied(*known);

	connect(m_thread->channel(), &CoreChannel::received, this, &Completed::take);
}

QList<Qdc::CompletedDownload> Qdc::Completed::entries() const
{
	return m_entries;
}

int Qdc::Completed::count() const
{
	return static_cast<int>(m_entries.size());
}

int Qdc::Completed::limit()
{
	return QuickDC::CompletedDownloads::LIMIT;
}

void Qdc::Completed::remove(const QString& filename)
{
	if (m_thread) m_thread->submit(QuickDC::Api::Commands::ForgetCompletedDownload{filename.toStdString()});
}

void Qdc::Completed::clear()
{
	if (m_thread) m_thread->submit(QuickDC::Api::Commands::ClearCompletedDownloads{});
}

void Qdc::Completed::take(const QuickDC::Api::Event& event)
{
	const auto* told = event.as<Ev::CompletedDownloads>();
	if (!told) return;

	m_entries = copied(*told);
	emit changed();
}

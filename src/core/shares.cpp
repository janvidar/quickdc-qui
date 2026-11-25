/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "shares.h"

#include "corechannel.h"
#include "corethread.h"

#include <atomic>

namespace {

namespace Ev = QuickDC::Api::Events;

std::string to_std(const QString& text)
{
	const QByteArray utf8 = text.toUtf8();
	return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

}

Qdc::Shares::Shares(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
{
	if (!m_thread) return;

	if (const Ev::ShareFolders* known = m_thread->channel()->latest<Ev::ShareFolders>())
		adopt(*known);

	connect(m_thread->channel(), &CoreChannel::received, this, &Shares::onCoreEvent);
}

QList<Qdc::SharedFolder> Qdc::Shares::folders() const
{
	return m_folders;
}

int Qdc::Shares::count() const
{
	return static_cast<int>(m_folders.size());
}

std::uint64_t Qdc::Shares::nextRequest()
{
	/* Every Shares hears every answer, so the numbers are unique across them. */
	static std::atomic<std::uint64_t> requests{0};
	const std::uint64_t request = ++requests;
	m_asked.append(request);
	return request;
}

void Qdc::Shares::add(const QString& path, const QString& name)
{
	if (!m_thread) return;
	m_thread->submit(QuickDC::Api::Commands::AddShareFolder{to_std(path), to_std(name), nextRequest()});
}

void Qdc::Shares::removeAt(int index)
{
	if (!m_thread || index < 0 || index >= m_folders.size()) return;
	m_thread->submit(QuickDC::Api::Commands::RemoveShareFolder{
		to_std(m_folders.at(index).path), nextRequest()});
}

void Qdc::Shares::renameAt(int index, const QString& name)
{
	if (!m_thread || index < 0 || index >= m_folders.size()) return;
	m_thread->submit(QuickDC::Api::Commands::RenameShareFolder{
		to_std(m_folders.at(index).path), to_std(name), nextRequest()});
}

void Qdc::Shares::adopt(const Ev::ShareFolders& told)
{
	m_folders.clear();
	for (const QuickDC::Api::ShareFolder& folder : told.folders)
		m_folders.append(SharedFolder{QString::fromStdString(folder.name),
		                              QString::fromStdString(folder.path)});
}

void Qdc::Shares::onCoreEvent(const QuickDC::Api::Event& event)
{
	if (const auto* told = event.as<Ev::ShareFolders>())
	{
		m_asked.removeOne(told->request);
		adopt(*told);
		emit changed();
		return;
	}

	const auto* refusal = event.as<Ev::ShareFolderRefused>();
	if (!refusal || !m_asked.removeOne(refusal->request)) return;

	using Why = Ev::ShareFolderRefused::Why;
	ShareRefusal why = ShareRefusal::Other;
	if (refusal->why == Why::AlreadyShared || refusal->why == Why::InsideShared)
		why = ShareRefusal::AlreadyShared;
	else if (refusal->why == Why::NotADirectory)
		why = ShareRefusal::NotAFolder;

	emit refused(QString::fromStdString(refusal->path), why);
}

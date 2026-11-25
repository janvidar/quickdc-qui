/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "mediabridge.h"

#include "configbridge.h"
#include "corechannel.h"
#include "corethread.h"



namespace {

/** A tiger root in base32, which is how long an entry's hash is. */
constexpr int TTH_LENGTH = 39;

}

Qdc::MediaBridge::MediaBridge(CoreThread* thread, ConfigBridge* config, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
{
	loadOffered(config);

	if (m_thread)
		connect(m_thread->channel(), &CoreChannel::received, this, &MediaBridge::onCoreEvent);
}

Qdc::MediaBridge::~MediaBridge() = default;

void Qdc::MediaBridge::loadOffered(ConfigBridge* config)
{
	if (!config) return;

	/*
	 * Read once, here, rather than whenever somebody asks: pathFor() is called while a
	 * message is being laid out, and a configuration read is a blocking trip to the core
	 * thread - which is exactly what must never happen while drawing.
	 *
	 * The group is the core's, written when a file is attached: one entry per file, a
	 * tiger root and a path. Parsed by position because a path may contain spaces.
	 */
	const int total = config->getNumber(ConfigStore::Application,
	                                    QStringLiteral("Attachments"),
	                                    QStringLiteral("Total"));

	for (int n = 0; n < total; ++n)
	{
		const QString entry = config->getString(ConfigStore::Application,
			QStringLiteral("Attachments"), QStringLiteral("File %1").arg(n));

		if (entry.size() < TTH_LENGTH + 2 || entry.at(TTH_LENGTH) != QLatin1Char(' '))
			continue;

		m_offered.insert(entry.left(TTH_LENGTH), entry.mid(TTH_LENGTH + 1));
	}
}

QString Qdc::MediaBridge::pathFor(const QString& tth) const
{
	return m_offered.value(tth);
}

void Qdc::MediaBridge::attach(const QString& path, Offer offer)
{
	if (path.isEmpty()) return;

	const bool post = offer == Offer::Post;
	QuickDC::Api::Commands::AttachFile command;
	command.path = path.toStdString();
	command.remembered = !post;
	command.freeSlot = post;

	if (!m_thread || !m_thread->submit(command))
	{
		emit attachFailed(path, tr("The core is not running."));
		return;
	}

	m_asked.insert(path);
}

quint64 Qdc::MediaBridge::attachPasted(const QByteArray& data, const QString& suffix)
{
	static quint64 requests = 0;

	const QuickDC::Api::Commands::AttachPasted command{
		++requests, data.toStdString(), suffix.toStdString()};
	if (!m_thread || !m_thread->submit(command)) return 0;

	m_pasting.insert(command.request);
	return command.request;
}

void Qdc::MediaBridge::fetch(const QString& tth, const QString& name, quint64 size)
{
	if (m_thread && !tth.isEmpty())
		m_thread->submit(QuickDC::Api::Commands::FetchAttachment{0, tth.toStdString(), name.toStdString(), size});
}

void Qdc::MediaBridge::setFetchOn(const QString& hubUrl, std::optional<bool> fetch)
{
	using Choice = QuickDC::Api::Commands::SetChatMediaFetch::Choice;
	const Choice choice = !fetch ? Choice::Follow : *fetch ? Choice::Always : Choice::Never;
	if (m_thread) m_thread->submit(QuickDC::Api::Commands::SetChatMediaFetch{hubUrl.toStdString(), choice});
}

void Qdc::MediaBridge::onCoreEvent(const QuickDC::Api::Event& event)
{
	using namespace QuickDC::Api::Events;

	if (const auto* kept = event.as<PastedKept>())
	{
		if (!m_pasting.remove(kept->request)) return;

		const QString path = QString::fromStdString(kept->path);
		if (!path.isEmpty()) m_asked.insert(path);
		emit pasted(kept->request, path, QString::fromStdString(kept->reason));
	}
	else if (const auto* ready = event.as<AttachmentReady>())
	{
		const QString path = QString::fromStdString(ready->path);
		const QString tth = QString::fromStdString(ready->tth);

		/* Whoever asked, the file is offered now, and this can name it by hash. */
		m_offered.insert(tth, path);

		if (m_asked.remove(path)) emit attached(path, tth, ready->size);
	}
	else if (const auto* failed = event.as<AttachmentFailed>())
	{
		const QString path = QString::fromStdString(failed->path);
		if (!m_asked.remove(path)) return;

		switch (failed->why)
		{
			case AttachmentFailed::Why::NoShares:
				emit attachFailed(path, tr("There is nothing to offer files from."));
				break;

			case AttachmentFailed::Why::Unreadable:
				emit attachFailed(path, tr("'%1' cannot be offered: it is not a file "
				                           "that can be read.").arg(path));
				break;

			case AttachmentFailed::Why::Failed:
				emit attachFailed(path, QString::fromStdString(failed->reason));
				break;
		}
	}
}

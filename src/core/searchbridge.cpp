/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "searchbridge.h"

#include "corechannel.h"
#include "corethread.h"


#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace {

/**
 * What each category stands for.
 *
 * The groupings clients have always offered, which is why they are a table here
 * rather than anything the protocol knows about: the wire carries extensions, one
 * by one, and the grouping is entirely this side's idea of what somebody looking
 * for music means.
 */
struct Category {
	Qdc::SearchCategory category;
	const char* extensions;
};

const Category CATEGORIES[] = {
	{ Qdc::SearchCategory::Audio,
	  "mp3 flac ogg opus m4a aac wav wma ape mpc aiff alac" },

	{ Qdc::SearchCategory::Video,
	  "mkv mp4 avi m4v mov webm mpg mpeg wmv flv vob ts ogv" },

	{ Qdc::SearchCategory::Picture,
	  "jpg jpeg png gif webp bmp tiff tif svg avif heic" },

	{ Qdc::SearchCategory::Document,
	  "pdf epub mobi azw3 djvu txt rtf doc docx odt xls xlsx ods ppt pptx odp" },

	{ Qdc::SearchCategory::Archive,
	  "zip rar 7z gz bz2 xz zst tar tgz iso lz4 cab arj lzh" }
};

QuickDC::Api::Commands::Search::Size convert(Qdc::SearchSize size)
{
	using Size = QuickDC::Api::Commands::Search::Size;

	switch (size)
	{
		case Qdc::SearchSize::AtLeast: return Size::AtLeast;
		case Qdc::SearchSize::AtMost:  return Size::AtMost;
		case Qdc::SearchSize::Any:     break;
	}

	return Size::Any;
}

}

Qdc::SearchBridge::SearchBridge(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
{
	if (m_thread)
		connect(m_thread->channel(), &CoreChannel::received, this, &SearchBridge::onCoreEvent);
}

Qdc::SearchBridge::~SearchBridge() = default;

QStringList Qdc::SearchBridge::extensionsFor(SearchCategory category)
{
	for (const Category& entry : CATEGORIES)
	{
		if (entry.category == category)
			return QString::fromLatin1(entry.extensions).split(QLatin1Char(' '));
	}

	return QStringList();
}

Qdc::SearchCategory Qdc::SearchBridge::categoryOf(const QString& filename)
{
	/*
	 * The suffix of the name rather than of the path: a peer's result is a path in
	 * their share, and QFileInfo is what knows where the name in it starts.
	 */
	const QString suffix = QFileInfo(filename).suffix().toLower();
	if (suffix.isEmpty()) return SearchCategory::Any;

	for (const Category& entry : CATEGORIES)
	{
		if (QString::fromLatin1(entry.extensions)
			.split(QLatin1Char(' ')).contains(suffix))
		{
			return entry.category;
		}
	}

	return SearchCategory::Any;
}

QString Qdc::SearchBridge::start(const SearchQuery& query)
{
	if (!m_thread || !m_thread->isCoreRunning()) return QString();

	const QString token = QString::number(m_nextToken++);

	QuickDC::Api::Commands::Search command;
	command.token = token.toStdString();
	command.sizePolicy = convert(query.sizePolicy);
	command.size = query.size;

	const QStringList words = query.terms.split(QRegularExpression(QStringLiteral("\\s+")),
	                                            Qt::SkipEmptyParts);
	for (const QString& word : words)
	{
		if (word.startsWith(QLatin1Char('-')) && word.size() > 1)
			command.exclude.push_back(word.mid(1).toStdString());
		else
			command.include.push_back(word.toStdString());
	}

	for (const QString& ext : extensionsFor(query.category))
		command.extensions.push_back(ext.toStdString());

	if (!m_thread->submit(command)) return QString();

	m_sent.insert(token);
	return token;
}

void Qdc::SearchBridge::onCoreEvent(const QuickDC::Api::Event& event)
{
	const auto* sent = event.as<QuickDC::Api::Events::SearchSent>();
	if (!sent) return;

	const QString token = QString::fromStdString(sent->token);
	if (m_sent.remove(token)) emit searchSent(token, sent->hubsAsked);
}

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "searchbridge.h"

#include "corethread.h"

#include <api/core.h>
#include <network/hubmanager.h>
#include <share/searchrequest.h>

#include <QFileInfo>
#include <QMetaObject>
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

QuickDC::Share::SearchRequest::SizePolicy convert(Qdc::SearchSize size)
{
	switch (size)
	{
		case Qdc::SearchSize::AtLeast: return QuickDC::Share::SearchRequest::SizeMin;
		case Qdc::SearchSize::AtMost:  return QuickDC::Share::SearchRequest::SizeMax;
		case Qdc::SearchSize::Any:     break;
	}

	return QuickDC::Share::SearchRequest::SizeAny;
}

}

Qdc::SearchBridge::SearchBridge(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
	, m_relay(Relay::create())
{
}

Qdc::SearchBridge::~SearchBridge()
{
	/* A request still out with the core has nowhere to report to now. */
	m_relay->detach();
}

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

	/*
	 * Split here rather than on the core thread, because this is where the terms
	 * are a QString. Everything crossing over is a QByteArray, since a std::string
	 * built from a QString on this thread is as safe to carry as the bytes are and
	 * a QString is not: it shares its buffer with the copy this closure captured.
	 */
	QList<QByteArray> include;
	QList<QByteArray> exclude;

	const QStringList words = query.terms.split(QRegularExpression(QStringLiteral("\\s+")),
	                                            Qt::SkipEmptyParts);

	for (const QString& word : words)
	{
		if (word.startsWith(QLatin1Char('-')) && word.size() > 1)
			exclude.append(word.mid(1).toUtf8());
		else
			include.append(word.toUtf8());
	}

	QList<QByteArray> extensions;
	for (const QString& ext : extensionsFor(query.category))
		extensions.append(ext.toUtf8());

	const QByteArray tokenBytes = token.toUtf8();
	const auto policy = convert(query.sizePolicy);
	const quint64 size = query.size;

	m_thread->call(m_relay, [thread = m_thread, include, exclude, extensions,
	                         tokenBytes, policy, size] {
		QuickDC::Core* core = thread->core();
		if (!core || !core->hubs) return -1;

		/*
		 * No reply handler: that argument is for answering somebody else's search
		 * out of our own share, which the share manager does. This request is
		 * only ever going outwards.
		 */
		QuickDC::Share::SearchRequest request(policy, size, nullptr,
		                                      tokenBytes.constData(), nullptr);

		for (const QByteArray& term : include)
			request.addInclusion(term.constData());

		for (const QByteArray& term : exclude)
			request.addExclusion(term.constData());

		for (const QByteArray& ext : extensions)
			request.addExtension(ext.constData());

		/*
		 * On the stack, because nothing keeps it: each session reads the request,
		 * builds a command from it and sends that, and neither the hub manager nor
		 * the hubs hold a reference afterwards.
		 */
		return static_cast<int>(core->hubs->searchAllHubs(&request));
	}, [this, token](int asked) {
		if (asked >= 0) emit searchSent(token, asked);
	});

	return token;
}

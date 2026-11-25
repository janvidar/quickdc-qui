/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "favourites.h"

#include "corechannel.h"
#include "corethread.h"
#include "hubbridge.h"

namespace {

namespace Ev = QuickDC::Api::Events;

QStringList copied(const Ev::FavouriteHubs& told)
{
	QStringList urls;
	for (const std::string& url : told.urls) urls.append(QString::fromStdString(url));
	return urls;
}

}

Qdc::Favourites::Favourites(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
{
	if (!m_thread) return;

	if (const Ev::FavouriteHubs* known = m_thread->channel()->latest<Ev::FavouriteHubs>())
		m_urls = copied(*known);

	connect(m_thread->channel(), &CoreChannel::received, this, &Favourites::take);
}

QStringList Qdc::Favourites::urls() const
{
	return m_urls;
}

bool Qdc::Favourites::contains(const QString& url) const
{
	return m_urls.contains(HubBridge::normalisedUrl(url));
}

void Qdc::Favourites::add(const QString& url)
{
	if (m_thread) m_thread->submit(QuickDC::Api::Commands::AddFavouriteHub{url.toStdString()});
}

void Qdc::Favourites::remove(const QString& url)
{
	if (m_thread) m_thread->submit(QuickDC::Api::Commands::RemoveFavouriteHub{url.toStdString()});
}

void Qdc::Favourites::toggle(const QString& url)
{
	if (contains(url)) remove(url);
	else add(url);
}

void Qdc::Favourites::take(const QuickDC::Api::Event& event)
{
	const auto* told = event.as<Ev::FavouriteHubs>();
	if (!told) return;

	m_urls = copied(*told);
	emit changed();
}

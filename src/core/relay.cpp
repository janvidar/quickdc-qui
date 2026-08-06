/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "relay.h"

#include <QThread>

std::shared_ptr<Qdc::Relay> Qdc::Relay::create()
{
	return std::shared_ptr<Relay>(new Relay, [](Relay* relay) {
		/*
		 * Straight out where we are already the thread it belongs to, and only
		 * handed to the event loop where we are not. deleteLater() takes the lock
		 * on that thread's list of posted events, and one of the places the last
		 * reference is dropped is Qt clearing that very list on the way out - which
		 * is holding the lock, so asking for it there is waiting for ourselves.
		 */
		QThread* owner = relay->thread();

		if (!owner || owner == QThread::currentThread())
			delete relay;
		else
			relay->deleteLater();
	});
}

void Qdc::Relay::detach()
{
	m_detached.store(true, std::memory_order_release);
}

bool Qdc::Relay::detached() const
{
	return m_detached.load(std::memory_order_acquire);
}

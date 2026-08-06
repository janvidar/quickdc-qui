/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_RELAY_H
#define HAVE_QUICKDC_QUI_RELAY_H

#include <QObject>

#include <atomic>
#include <memory>

namespace Qdc {

/**
 * The address a core thread job answers to.
 *
 * A job runs on the core thread, and by the time it has something to say the
 * bridge that wanted it may be gone. A raw QObject* cannot be tested for that
 * from another thread and QPointer is no safer, so the answer is addressed here
 * instead: the job holds the relay alive through a shared_ptr for as long as it
 * needs somewhere to deliver to, and the bridge detaches on its way out, which
 * turns every answer still in flight into a no-op.
 *
 * The relay belongs to the thread that created it, which is the thread its
 * answers are delivered on. It is never given a parent: the shared_ptr owns it,
 * and the last reference - which may well be dropped on the core thread - hands
 * it to deleteLater() so that it is destroyed on its own thread rather than
 * whichever one happened to let go of it last.
 */
class Relay final : public QObject {
public:
	static std::shared_ptr<Relay> create();

	/**
	 * Stop answering.
	 *
	 * Safe from any thread, and idempotent. Called by the owning bridge before
	 * it goes, so that a reply already queued behind it is dropped rather than
	 * run against a half destroyed object.
	 */
	void detach();

	/** Whether detach() has been called. Safe from any thread. */
	bool detached() const;

private:
	Relay() = default;

	std::atomic<bool> m_detached{false};
};

}

#endif // HAVE_QUICKDC_QUI_RELAY_H

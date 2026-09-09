/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_FILELISTBRIDGE_H
#define HAVE_QUICKDC_QUI_FILELISTBRIDGE_H

#include "coretypes.h"
#include "relay.h"

#include <QList>
#include <QObject>
#include <QString>

#include <memory>

class QTimer;

namespace Qdc {

class CoreThread;

/**
 * Browsing what another user shares.
 *
 * Polled, not pushed, and that is the core's doing rather than a choice: the
 * queue unpacks an arriving file list, moves it into place and drops the request,
 * and tells nobody. QueueManager::onFilelistDownloaded() is internal and returns
 * only how many sources it found. So this watches the pending set instead, and a
 * request it was waiting for disappearing is what counts as an answer - the list
 * being on disk afterwards is what distinguishes success from the queue having
 * given up.
 *
 * One request per user at a time, which is also the queue's own rule:
 * requestFilelist() returns the request already pending rather than making a
 * second one.
 */
class FilelistBridge final : public QObject {
	Q_OBJECT

public:
	explicit FilelistBridge(CoreThread* thread, QObject* parent = nullptr);
	~FilelistBridge() override;

	/**
	 * Ask for a user's file list.
	 *
	 * @param hubUrl the hub's canonical URL, as ChatMessage::hubUrl carries it.
	 * @param sourceId UserInfo::sourceId - the client ID on ADC, the nickname on
	 *        NMDC. Not UserInfo::id.
	 *
	 * Returns at once; the answer arrives as filelistReady() or filelistFailed(),
	 * which may be minutes later if the peer has to be retried. Asking again for
	 * a browse already outstanding does nothing.
	 *
	 * A browse of our own share is answered from the file list on this disk, with
	 * nothing asked of the hub and no connection made: it is the same list this
	 * client hands to anybody who browses it, through the same decoder, so what
	 * the user sees of their own files is what everybody else sees. The answer
	 * still arrives as filelistReady(), on a later turn like any other.
	 */
	void request(const QString& hubUrl, const QString& sourceId);

	bool isPending(const QString& hubUrl, const QString& sourceId) const;

	/** How many browses are outstanding, for a progress indicator. */
	int pendingCount() const;

signals:
	void filelistReady(const QString& hubUrl, const QString& sourceId,
	                   const Qdc::RemoteDirInfo& root);

	void filelistFailed(const QString& hubUrl, const QString& sourceId,
	                    const QString& reason);

private:
	/**
	 * One pass over the outstanding requests, on the core thread.
	 *
	 * Runs from a timer rather than continuously, because a browse takes as long
	 * as the peer takes and there is nothing to be gained by asking often.
	 */
	void poll();

	struct Pending {
		QString hubUrl;
		QString sourceId;

		/**
		 * Where the queue said the list would land. Captured when the request
		 * was made, because by the time it completes the FilelistRequest that
		 * knew is gone.
		 */
		QString localFile;
	};

	CoreThread* m_thread;

	/** Where a reply from the core lands; see Qdc::Relay. */
	std::shared_ptr<Relay> m_relay;

	QTimer* m_timer;
	QList<Pending> m_pending;

	/**
	 * A pass is out with the core and has not answered yet.
	 *
	 * Without it a core that is busy for longer than the interval collects a pass
	 * per tick, all of them asking the same question.
	 */
	bool m_polling = false;
};

}

#endif // HAVE_QUICKDC_QUI_FILELISTBRIDGE_H

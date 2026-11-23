/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_FILELISTBRIDGE_H
#define HAVE_QUICKDC_QUI_FILELISTBRIDGE_H

#include "coretypes.h"

#include <QList>
#include <QObject>
#include <QString>


namespace QuickDC { namespace Api { struct Event; } }

namespace Qdc {

class CoreThread;

/**
 * Browsing what another user shares.
 *
 * Asked for with a RequestFilelist command and answered by the core's
 * FilelistReady or FilelistFailed event, which the queue's own announcement of a
 * request ending produces - so an answer arrives when the list does, rather than
 * at the next look at whether it has.
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
	void onCoreEvent(const QuickDC::Api::Event& event);

	/** Remove the request for {hubUrl, sourceId}; false where none was waiting. */
	bool take(const QString& hubUrl, const QString& sourceId);

	struct Pending {
		QString hubUrl;
		QString sourceId;
	};

	CoreThread* m_thread;
	QList<Pending> m_pending;
};

}

#endif // HAVE_QUICKDC_QUI_FILELISTBRIDGE_H

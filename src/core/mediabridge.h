/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_MEDIABRIDGE_H
#define HAVE_QUICKDC_QUI_MEDIABRIDGE_H

#include <QObject>
#include <QString>

namespace Qdc {

class CoreThread;
class Attacher;

/**
 * Offering a local file to a conversation, as a view sees it.
 *
 * A message cannot carry a file, so it carries the file's hash instead and whoever wants
 * it asks the hub who has that hash. Before a message can name a file this way, two
 * things have to happen: the file has to be hashed, and it has to be offered - meaning a
 * request for that hash gets an answer.
 *
 * What it is not is shared. The file never appears in the file list and no name search
 * will match it, so it is not something anybody can browse or stumble on; the hash is
 * the only way in, and only the people in the conversation are given it.
 *
 * Hashing takes as long as reading the file, so attach() answers on a signal rather than
 * returning. Lives on the thread that constructed it.
 */
class MediaBridge final : public QObject {
	Q_OBJECT

public:
	explicit MediaBridge(CoreThread* thread, QObject* parent = nullptr);
	~MediaBridge() override;

	/**
	 * Hash a local file and offer it by that hash.
	 *
	 * Answered by attached() or attachFailed(), always with the path as it was given, so
	 * a caller waiting on several at once can tell them apart.
	 *
	 * Forgotten when the core stops: nothing is written down, so a file attached before
	 * a restart is no longer offered afterwards and a link to it in an old message will
	 * find nobody.
	 */
	void attach(const QString& path);

signals:
	/** @param size the file's length, which the message carries so a reader knows the
	                cost before asking for it. */
	void attached(const QString& path, const QString& tth, quint64 size);

	/** @param reason for a person to read. */
	void attachFailed(const QString& path, const QString& reason);

private:
	friend class Attacher;

	CoreThread* m_thread = nullptr;

	/** The core side half, built and destroyed inside jobs on the core thread. */
	Attacher* m_attacher = nullptr;
};

}

#endif // HAVE_QUICKDC_QUI_MEDIABRIDGE_H

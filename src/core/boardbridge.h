/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_BOARDBRIDGE_H
#define HAVE_QUICKDC_QUI_BOARDBRIDGE_H

#include "coretypes.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

#include <optional>

namespace QuickDC { namespace Api { struct Event; } }

namespace Qdc {

class CoreThread;

/**
 * What the core says about the bulletin boards, as a view draws it.
 *
 * The core follows every board a hub describes, keeps the entries and the read
 * state, and decides when a post's document is fetched, offered and let go of;
 * this hands on what it announces and the intents a view has - open a board, read
 * a post, write one, a hub's own choices - and keeps the read state and what is
 * known of each post where a row being painted can ask without waiting.
 */
class BoardBridge final : public QObject {
	Q_OBJECT

public:
	/** Whether what is held for a board can still be caught up with. */
	enum class View {
		/** No descriptor has been seen, so the question cannot be answered. */
		Unknown,

		/** Everything before what is held will still be replayed. */
		Complete,

		/** The entries between the cursor and the hub's oldest are not coming. */
		Gap
	};
	Q_ENUM(View)

	/** A hub's own choices about its boards; see setPolicy(). */
	enum class Policy { FetchAhead, Serve };

	explicit BoardBridge(CoreThread* thread, QObject* parent = nullptr);
	~BoardBridge() override;

	/** The name a hub's boards are filed under: its "{scheme}://{host}:{port}". */
	static QString hubKey(const QString& url);

	/** What the core last said about a document; Announced where it said nothing. */
	PostState postState(const QString& tth) const;

	/** Who a post is attributed to: the CID of its index entry, never the document's. */
	QString attribution(const QString& tth) const;

	bool isRead(const QString& hubUrl, const QString& board, const QString& tth) const;

	/** How many of a board's posts nobody has opened, as the core counts them. */
	int unread(const QString& hubUrl, const QString& board) const;

	/** Take what the core announced; public so that a test can stand in for it. */
	void take(const QuickDC::Api::Event& event);

public slots:
	/** Read a board back off the disk; answered by entriesLoaded() and boardStateReady(). */
	void loadEntries(const QString& hubUrl, const QString& board);

	/**
	 * Somebody opened a post: it is read, and shown where its document is here. With
	 * 'fetch' they asked for its body, which the core fetches - with the rest of the
	 * thread where the hub's choice says.
	 */
	void read(const QString& hubUrl, const QString& board, const QString& tth, bool fetch);

	/** Mark everything held for a board read, which is somebody dismissing it. */
	void markAllRead(const QString& hubUrl, const QString& board);

	/** A hub's own choice, or with none the general setting. */
	void setPolicy(const QString& hubUrl, Policy policy, std::optional<bool> choice);

	/**
	 * Follow a board again: one left, or with 'fromStart' every entry the hub holds
	 * rather than from the highest timestamp held, which is the only way a hub that
	 * reordered or removed entries becomes visible. The core follows every board a
	 * hub describes by itself.
	 */
	void subscribe(const QString& hubUrl, const QString& board, bool fromStart = false);

	/** Leave a board; its descriptors do not take it up again while the session lasts. */
	void leave(const QString& hubUrl, const QString& board);

	/**
	 * Ask for one entry by hash: the parent of an orphaned reply, most often. A
	 * question rather than a subscription: the hub answers with the single entry or
	 * with a refusal carrying code 76.
	 */
	void requestEntry(const QString& hubUrl, const QString& board, const QString& tth);

	/**
	 * Write a post on the hub, the core composing, signing and offering the document;
	 * a refusal comes back as postRefused(), and the entry coming back publishes it.
	 *
	 * @param parent empty for a post that starts a thread.
	 */
	void write(const QString& hubUrl, const QString& board, const QString& parent,
	           const QString& subject, const QString& body, bool richText);

	/**
	 * Ask the hub to withdraw a post. That removes an index entry and not the
	 * document, which every client that read it may go on serving, so it must never
	 * be presented as a guarantee that a post is gone.
	 */
	void withdraw(const QString& hubUrl, const QString& board, const QString& tth);

signals:
	/**
	 * A post has been fetched or read off the disk, and checked. BoardPost::cid is
	 * the author the document claims; where the state is Disputed the post is
	 * attributed to attribution() instead.
	 */
	void postReady(const QString& tth, const Qdc::BoardPost& post);

	/** Not this time, in words fit to show somebody; the entry stays. */
	void postUnavailable(const QString& tth, const QString& reason);

	/** The document is here and is not the post its entry named: final. */
	void postUnreadable(const QString& tth, const QString& reason);

	void postStateChanged(const QString& tth, Qdc::PostState state);

	/** The whole of a board as held, oldest first. */
	void entriesLoaded(const QString& hubUrl, const QString& board,
	                   const QVector<Qdc::BoardEntry>& entries);

	/** Entries that arrived and were kept, tombstones among them. */
	void entriesStored(const QString& hubUrl, const QString& board,
	                   const QVector<Qdc::BoardEntry>& entries);

	/**
	 * Where a board stands. 'described' says whether 'board' is what the hub said
	 * of it; 'left' whether somebody left it on this session.
	 */
	void boardStateReady(const QString& hubUrl, const Qdc::BoardInfo& board, bool described,
	                     quint64 cursor, Qdc::BoardBridge::View view, bool left);

	/** The hub said the board is no more. */
	void boardGone(const QString& hubUrl, const QString& board);

	/** What unread() and isRead() answer about a board has moved. */
	void readStateChanged(const QString& hubUrl, const QString& board);

	/** A post written here could not be sent, in words fit to show somebody. */
	void postRefused(const QString& reason);

private:
	struct Read {
		int unread = 0;
		QSet<QString> read;
	};

	static QString boardKey(const QString& hubUrl, const QString& board);

	void noteRead(const QString& hubUrl, const QString& board, int unread,
	              const QVector<QString>& read, bool whole);

	void setState(const QString& tth, PostState state);

	CoreThread* m_thread = nullptr;

	QHash<QString, PostState> m_state;
	QHash<QString, QString> m_attributed;
	QHash<QString, Read> m_read;

	/** The posts written here whose answer is still owed, by request. */
	QSet<quint64> m_writing;
};

}

#endif // HAVE_QUICKDC_QUI_BOARDBRIDGE_H

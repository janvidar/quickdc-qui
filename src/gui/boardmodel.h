/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_BOARDMODEL_H
#define HAVE_QUICKDC_QUI_BOARDMODEL_H

#include <core/coretypes.h>

#include <QAbstractItemModel>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Qdc {

/**
 * One board's index, drawn as the threads it is made of.
 *
 * A tree rather than a table, because a board is a conversation: a post that starts a
 * thread is a row of its own and everything descending from it sits under it. The
 * levels are the reply depth and there is no fixed number of them - unlike the two of
 * SearchModel, which are a file and the peers offering it.
 *
 * Everything in here arrived as the hub's testimony. The subject and the parent were
 * copied by the hub out of a document it has never read, so they are hints until the
 * document has been fetched and hashed; the board, the CID, the nickname, the thread
 * root and the timestamp are the hub's own and can be checked against nothing at all.
 * The model therefore says how much is known about each row - see StateRole and
 * DocumentColumn - and never lets a subject drawn before the document arrives look
 * like one that was read out of it.
 *
 * Three rules decide the shape of the tree, and each of them is the specification's
 * rather than a preference:
 *
 * - Grouping is rebuilt from 'parent' and not from 'thread'. The thread root is the
 *   hub's convenience and is never verified, while the parent link is inside the
 *   document's hash and is load-bearing; where the two disagree the parent wins, so a
 *   hub that groups threads wrongly produces nothing worse than a wrong 'thread'
 *   field that nothing reads.
 * - Siblings are ordered by the hub's timestamp and never by the author's claimed
 *   composition date, which nothing verifies. Two entries may share a timestamp, and
 *   the order of those is broken on the tiger root: see the note on sorting in
 *   boardmodel.cpp for why that is the only tie-break that survives a restart.
 * - An orphan is kept. A reply whose parent is not held is ordinary rather than
 *   broken, so it is attached to its thread and marked as having a parent that is not
 *   here, and the parent it wants can be asked for; see missingParents().
 *
 * A tombstone leaves the view and is still held: replies to a withdrawn post are
 * separate posts and stay, drawn as descending from a withdrawn parent, and the
 * withdrawal's own timestamp is what keeps a replayed copy of the original entry from
 * bringing it back.
 */
class BoardModel final : public QAbstractItemModel {
	Q_OBJECT

public:
	enum Column {
		/** The subject, and whose subject it is depends on StateRole. */
		SubjectColumn,

		/** What a reader recognises the author by, which is not what identifies them. */
		AuthorColumn,

		/** When the hub says it accepted the post. */
		WhenColumn,

		/**
		 * This client's own word about the row's parent: missing, withdrawn, or nothing.
		 *
		 * A column of ours rather than a marker stuck to the subject, because the
		 * subject is a stranger's text and our words must not be mistaken for part of
		 * it - which is exactly what BBS0 asks of a board's untrusted strings.
		 */
		ParentColumn,

		/** How much of the document is actually known; PostState in a word. */
		DocumentColumn,

		ColumnCount
	};

	enum Role {
		/** The post's identity: the tiger root of its document. */
		TthRole = Qt::UserRole + 1,

		/** The whole index entry, for a delegate that wants more than a cell. */
		EntryRole,

		/** PostState as an int, so a view can draw an unfetched row as unfetched. */
		StateRole,

		/** The thread root as this model rebuilt it from parent links. */
		ThreadRole,

		/** The parent this row names, held or not; empty in a post that starts a thread. */
		ParentTthRole,

		/** How deep the row is drawn, root zero. Bounded; see MaximumDepth. */
		DepthRole,

		/** The row names a parent that is not held at all. */
		MissingParentRole,

		/** The row names a parent that is held as a tombstone. */
		WithdrawnParentRole,

		/** The row is drawn shallower than it really is, the nesting bound having been met. */
		FlattenedRole
	};

	/**
	 * How deep the tree is allowed to nest.
	 *
	 * A legitimate thread can be arbitrarily deep - the specification says as much and
	 * requires display depth to be bounded - and a view indenting a thousand levels has
	 * put its text off the right hand edge. Past this, a reply is drawn as a sibling of
	 * its parent instead of under it and says so through FlattenedRole, so the thread
	 * carries on as a flat run rather than being cut off: no entry is ever dropped for
	 * being too deep.
	 */
	static constexpr int MaximumDepth = 12;

	/**
	 * How far ahead of this machine's clock a timestamp may be and still be drawn.
	 *
	 * A hub owns the clock and may set it to anything; one that stamps a post far in
	 * the future pins it to the head of the board for ever. Such a value is presented
	 * as an unknown time and never corrected, because the value is what a cursor is
	 * made of and rounding it would move every cursor derived from it. A day is
	 * generous enough that an ordinarily wrong clock still draws as a time.
	 */
	static constexpr quint64 FutureTolerance = 24 * 60 * 60;

	explicit BoardModel(QObject* parent = nullptr);

	/**
	 * Which board this holds, after which entries naming another are refused.
	 *
	 * One hub's assertions about one board, and merging two of them produces a board
	 * that exists nowhere - so a model told what it is refuses to be fed anything else.
	 * Left unset it holds whatever it is given, which is what a test wants.
	 */
	void setBoard(const QString& board);
	QString board() const;

	/**
	 * Take one index entry, or a batch of them.
	 *
	 * Keyed by the tiger root, as the specification requires: an entry for a root
	 * already held replaces it when the arriving timestamp is greater and is otherwise
	 * ignored. That one rule covers both cases it has to - a duplicate from the second
	 * a resumption starts in has an equal timestamp and is dropped, while a tombstone
	 * carries the time of the withdrawal and so takes effect.
	 */
	void add(const Qdc::BoardEntry& entry);
	void add(const QVector<Qdc::BoardEntry>& entries);

	/** Forget the board: the entries, the tree and what was known about each document. */
	void clear();

	/**
	 * Record how much of a document is known.
	 *
	 * Kept beside the entries rather than in them, because it is what this client found
	 * out and not what the hub said - and because it is per document: verification is
	 * against a hash and means the same thing on every board that indexes it.
	 */
	void setState(const QString& tth, Qdc::PostState state);

	/**
	 * What is known about a document. Announced for anything never fetched.
	 *
	 * A tombstone answers Withdrawn whatever was cached about the document before it,
	 * since the entry that named it is gone.
	 */
	Qdc::PostState stateOf(const QString& tth) const;

	/** Whether an entry is held for this root, tombstone or not. */
	bool holds(const QString& tth) const;

	/** Whether what is held for this root is a tombstone. */
	bool isWithdrawn(const QString& tth) const;

	/**
	 * The entry held for a root, or a default-constructed one.
	 *
	 * Answers for a tombstone as well as for a row, which is what lets a reading pane
	 * go on saying something sensible about the post it was showing when the withdrawal
	 * arrived.
	 */
	Qdc::BoardEntry entryOf(const QString& tth) const;

	/** The entry a row stands for, or a default-constructed one for anything else. */
	Qdc::BoardEntry entryAt(const QModelIndex& index) const;

	QString tthAt(const QModelIndex& index) const;

	/** Where a root is drawn, or an invalid index when it is not drawn at all. */
	QModelIndex indexOf(const QString& tth) const;

	/** The thread root this model put a post under, walking parent links. */
	QString threadOf(const QString& tth) const;

	/**
	 * Every parent some held reply names and that is not held here, sorted.
	 *
	 * What a client may ask the hub for one entry at a time. Sorted rather than in the
	 * order they were noticed, so that asking for them twice asks in the same order.
	 */
	QStringList missingParents() const;

	/** Rows drawn: entries held, tombstones excluded. */
	int postCount() const;

	/** Top level rows, which is threads as this model grouped them. */
	int threadCount() const;

	/**
	 * A hub timestamp as a person should read it.
	 *
	 * Public and static because the reading pane draws the same seconds under the same
	 * rule, and a second opinion about what is plausible would be a second answer to
	 * the same question.
	 */
	static QString describeTimestamp(quint64 seconds);

	/** Whether a timestamp is one this client is willing to draw at all. */
	static bool isPlausible(quint64 seconds);

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation,
	                    int role = Qt::DisplayRole) const override;

	QModelIndex index(int row, int column,
	                  const QModelIndex& parent = QModelIndex()) const override;
	QModelIndex parent(const QModelIndex& child) const override;

private:
	/** One drawn row: which post, where it sits, and what is odd about its parent. */
	struct Node {
		QString tth;

		/** The thread root, as rebuilt from parent links rather than as the hub said. */
		QString thread;

		/** Into m_nodes, or -1 for a top level row. */
		int parent = -1;

		/** Among its siblings, so parent() can answer without searching. */
		int row = 0;

		int depth = 0;

		QList<int> children;

		bool missingParent = false;
		bool withdrawnParent = false;
		bool flattened = false;
	};

	/**
	 * Build the whole tree again from the entries.
	 *
	 * A rebuild rather than an insertion, and wrapped in a model reset by every caller.
	 * A parent may arrive after its children - the hub is entitled to send a reply
	 * before what it replies to, and an orphan is adopted the moment its parent turns
	 * up - so an arriving entry can move rows that were already drawn anywhere in the
	 * tree. A board arrives at the speed of a subscription rather than of a wide
	 * search, so the cost of being obviously correct here is one nobody notices; the
	 * pane keeps the selection across the reset by remembering the root rather than the
	 * row.
	 */
	void rebuild();

	/** The live entry for a root, or null for a tombstone or a root not held. */
	const Qdc::BoardEntry* live(const QString& tth) const;

	/**
	 * Where a reply's subtree hangs, and how deep, honouring MaximumDepth.
	 *
	 * @return the node made for @a tth.
	 */
	int attach(const QString& tth, int parentNode, int depth, bool flattened,
	           bool missingParent, bool withdrawnParent);

	/** Sort every child list and stamp Node::row, once the tree is complete. */
	void order();

	/** Whether one root sorts before another among siblings. */
	bool precedes(const QString& left, const QString& right) const;

	static bool isNode(const QModelIndex& index) { return index.internalId() != 0; }
	static int nodeOf(const QModelIndex& index)
	{ return static_cast<int>(index.internalId()) - 1; }

	const Node* nodeAt(const QModelIndex& index) const;

	QVariant display(const Node& node, const Qdc::BoardEntry& entry, int column) const;
	QString tooltip(const Node& node, const Qdc::BoardEntry& entry) const;

	/** What this client would call the row's state, in its own words. */
	QString documentWord(Qdc::PostState state) const;

	/** The sentence a tooltip carries about that state. */
	QString documentSentence(Qdc::PostState state) const;

	QString m_board;

	/** Every entry, tombstones included, keyed by the root that identifies it. */
	QHash<QString, Qdc::BoardEntry> m_entries;

	/** What was found out about each document, keyed by root. Outlives a rebuild. */
	QHash<QString, Qdc::PostState> m_states;

	/**
	 * The drawn tree. An index carries a node's position here plus one, so that zero
	 * means the invisible root and node zero is still reachable.
	 */
	QList<Node> m_nodes;

	QHash<QString, int> m_nodeByTth;

	QList<int> m_roots;

	/** Parents named by a held reply and not held; the set behind missingParents(). */
	QStringList m_missing;
};

}

#endif // HAVE_QUICKDC_QUI_BOARDMODEL_H

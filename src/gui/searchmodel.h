/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_SEARCHMODEL_H
#define HAVE_QUICKDC_QUI_SEARCHMODEL_H

#include <core/coretypes.h>

#include <QAbstractItemModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QVector>

namespace Qdc {

/**
 * The answers to one search, a file at a time.
 *
 * Two levels: a file, and under it every peer that has it. A popular file draws
 * forty replies and is one row rather than forty, which is the whole reason this is
 * a tree and not a table.
 *
 * Grouped on the tiger root, because that is the only thing that says two files are
 * the same file - the same bytes are called different things on different disks, and
 * two files of the same name and size are routinely not the same file at all. A
 * reply that carried no root cannot be grouped and stands alone, which on NMDC is
 * most of them.
 *
 * Rows only ever arrive. A search is never told that it is over, so nothing is
 * removed and the model grows for as long as replies keep coming; a search is
 * finished when the caller stops caring, and it throws the whole model away then.
 */
class SearchModel final : public QAbstractItemModel {
	Q_OBJECT

public:
	enum Column {
		NameColumn,
		SizeColumn,
		UsersColumn,
		SlotsColumn,
		TypeColumn,

		/**
		 * Which hub it was offered on.
		 *
		 * A column of its own rather than reusing the one beside it, so that no
		 * header stands over something it does not name. A file offered from more
		 * than one hub says how many rather than picking one of them.
		 */
		HubColumn,

		ColumnCount
	};

	explicit SearchModel(QObject* parent = nullptr);

	/** File it under its root, making a new group if this is the first sight of it. */
	void add(const Qdc::SearchHit& hit);

	/**
	 * The same, for a batch of replies that arrived together.
	 *
	 * A search on several busy hubs answers in bursts of hundreds. Taken one at a
	 * time each is a model transaction of its own and the view lays itself out again
	 * for every one of them, which is the whole of what a search freezing the window
	 * was. Everything landing in one group here is one insertion instead.
	 */
	void add(const QVector<Qdc::SearchHit>& hits);

	/**
	 * The hit a row stands for.
	 *
	 * A peer row answers with its own. A file row answers with its best source -
	 * the most free slots, and the first seen among equals - so that acting on a
	 * collapsed row does what it looks like it should.
	 *
	 * A default-constructed hit, whose name is empty, for an index that is not a
	 * row of this model.
	 */
	Qdc::SearchHit hitAt(const QModelIndex& index) const;

	/** How many distinct files, and how many replies went into them. */
	int fileCount() const;
	int hitCount() const;

	/** How many peers answered, counted once each however many files they offered. */
	int peerCount() const;

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation,
	                    int role = Qt::DisplayRole) const override;

	QModelIndex index(int row, int column,
	                  const QModelIndex& parent = QModelIndex()) const override;
	QModelIndex parent(const QModelIndex& child) const override;

private:
	/** One file, and everywhere it was offered from. */
	struct Group {
		/**
		 * The root every hit in here shares, or empty for a group of one that had
		 * none. An empty key is never looked up, which is what keeps the ungrouped
		 * ones apart.
		 */
		QString tth;

		/**
		 * The shortest name any peer gave it.
		 *
		 * Shortest rather than first: a name is a path within somebody's share, and
		 * the shortest of them is the one carrying the least of somebody else's
		 * folder structure.
		 */
		QString name;

		/** Zero until a peer says otherwise; not every reply carries a size. */
		quint64 size = 0;

		/** Taken from the first reply; the same root is the same thing. */
		bool directory = false;

		/**
		 * What kind of file the name says it is.
		 *
		 * Worked out when the group is made rather than when a cell is drawn: the
		 * answer comes from splitting a table of extensions, and a view redrawing a
		 * screenful of rows asked for it once per row per repaint.
		 */
		Qdc::SearchCategory category = Qdc::SearchCategory::Any;

		QList<Qdc::SearchHit> hits;
	};

	/** Add one hit without telling anybody; returns the group it landed in. */
	int place(const Qdc::SearchHit& hit, bool& isNewGroup);

	static QString peerKey(const Qdc::SearchHit& hit);

	/**
	 * Whether an index points at a peer under a file, and which file.
	 *
	 * The id is the parent row plus one, so that zero means a top level row and
	 * every group is reachable - row zero included.
	 */
	static bool isPeer(const QModelIndex& index) { return index.internalId() != 0; }
	static int groupOf(const QModelIndex& index)
	{ return static_cast<int>(index.internalId()) - 1; }

	QVariant fileData(const Group& group, int column, int role) const;
	QVariant peerData(const Qdc::SearchHit& hit, int column, int role) const;

	QList<Group> m_groups;

	/** Root to its row in m_groups. Holds no entry for the ungrouped. */
	QHash<QString, int> m_byRoot;

	/**
	 * Every peer that has answered, by hub and id.
	 *
	 * Kept as replies arrive rather than counted when asked. The count is under the
	 * results the whole time a search is open, and walking every hit of every group
	 * to work it out again on each arriving reply is quadratic in the number of
	 * replies - which a wide search has thousands of.
	 */
	QSet<QString> m_peers;

	int m_hits = 0;
};

}

#endif // HAVE_QUICKDC_QUI_SEARCHMODEL_H

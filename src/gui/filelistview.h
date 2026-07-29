/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_FILELISTVIEW_H
#define HAVE_QUICKDC_QUI_FILELISTVIEW_H

#include <core/coretypes.h>

#include <QAbstractItemModel>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTreeView;

namespace Qdc {

class FilelistBridge;

/**
 * A decoded file list, as a tree.
 *
 * The tree is held as value types and walked by pointer into it, which is what
 * makes the model indices stable: RemoteDirInfo holds its children by value in a
 * std::vector, so a pointer to one stays good as long as the root does and the
 * tree is never modified after it is set.
 *
 * Filtering is done by rebuilding rather than through a proxy model, because a
 * name filter over a tree wants to keep a directory whose descendants match, and
 * a QSortFilterProxyModel only asks about a row's own text.
 */
class FilelistModel final : public QAbstractItemModel {
	Q_OBJECT

public:
	enum Column {
		NameColumn,
		SizeColumn,
		TthColumn,
		ColumnCount
	};

	explicit FilelistModel(QObject* parent = nullptr);
	~FilelistModel() override;

	QModelIndex index(int row, int column,
	                  const QModelIndex& parent = QModelIndex()) const override;
	QModelIndex parent(const QModelIndex& child) const override;
	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation,
	                    int role) const override;

	void setRoot(const RemoteDirInfo& root);
	void clear();

	/**
	 * Keep only entries whose name contains 'needle', and the directories on the
	 * way to them. An empty needle restores the whole tree.
	 */
	void setFilter(const QString& needle);

	/** Whether the index is a directory rather than a file. */
	bool isDirectory(const QModelIndex& index) const;

	/** Empty for a directory, or for an entry the list gave no hash for. */
	QString tthFor(const QModelIndex& index) const;

	/** The size in bytes, unformatted; a directory's is the recursive total. */
	quint64 sizeFor(const QModelIndex& index) const;

	/** Totals for the whole list, as decoded. */
	quint64 totalSize() const;
	quint64 totalFiles() const;

private:
	/**
	 * One node, flattened so that a QModelIndex can point at it.
	 *
	 * The model cannot use RemoteDirInfo directly: a QModelIndex has to be able
	 * to find its parent, and the value tree has no parent links.
	 */
	struct Node {
		QString name;
		QString tth;
		quint64 size = 0;
		bool directory = false;

		Node* parent = nullptr;
		int rowInParent = 0;
		std::vector<Node*> children;
	};

	/**
	 * Build a node tree from 'source', keeping only what passes 'needle'.
	 *
	 * @param ancestorMatched a directory on the way here matched by name, so
	 *        everything below it is kept whole - filtering for a folder should
	 *        show that folder's contents, not just the entries that happen to
	 *        repeat its name.
	 * @return null if nothing here survived the filter.
	 */
	Node* build(const RemoteDirInfo& source, Node* parent, const QString& needle,
	            bool ancestorMatched);

	static void destroy(Node* node);

	Node* nodeFor(const QModelIndex& index) const;

	/** Rebuild from the retained source with the current filter. */
	void rebuild();

	RemoteDirInfo m_source;
	Node* m_root = nullptr;
	QString m_filter;
};

/**
 * The Files tab of a private conversation: fetch someone's file list, then look
 * through it.
 *
 * Three states, because a browse genuinely has three: nothing asked for yet,
 * waiting on a peer who may never answer, and a list to show. The waiting state
 * is not a spinner over an empty tree - it can last minutes and can fail, and
 * saying so is more use than implying it is nearly done.
 */
class FilelistView final : public QWidget {
	Q_OBJECT

public:
	/** @param filelists may be null; the view then says browsing is unavailable. */
	FilelistView(FilelistBridge* filelists, const QString& hubUrl,
	             const QString& sourceId, QWidget* parent = nullptr);

	/** Follows the user, whose source id may only be known later. */
	void setSource(const QString& hubUrl, const QString& sourceId);

signals:
	/** A file was chosen. Nothing downloads it yet; see the notes in TODO. */
	void fileActivated(const QString& name, quint64 size, const QString& tth);

private:
	void requestList();
	void onReady(const QString& hubUrl, const QString& sourceId,
	             const RemoteDirInfo& root);
	void onFailed(const QString& hubUrl, const QString& sourceId,
	              const QString& reason);

	/** Whether an event names this view's user. */
	bool matches(const QString& hubUrl, const QString& sourceId) const;

	enum Page {
		IdlePage,
		WaitingPage,
		TreePage
	};

	FilelistBridge* m_filelists;
	QString m_hubUrl;
	QString m_sourceId;

	QStackedWidget* m_pages;
	QLabel* m_idle;
	QPushButton* m_fetch;
	QLabel* m_waiting;
	QTreeView* m_tree;
	FilelistModel* m_model;
	QLineEdit* m_filter;
	QLabel* m_summary;
};

}

#endif // HAVE_QUICKDC_QUI_FILELISTVIEW_H

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "filelistview.h"

#include "richtext.h"

#include <core/filelistbridge.h>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

QString format_size(quint64 bytes)
{
	if (bytes == 0) return QString();
	return QLocale().formattedDataSize(static_cast<qint64>(bytes));
}

}

Qdc::FilelistModel::FilelistModel(QObject* parent)
	: QAbstractItemModel(parent)
{
}

Qdc::FilelistModel::~FilelistModel()
{
	destroy(m_root);
}

void Qdc::FilelistModel::destroy(Node* node)
{
	if (!node) return;

	for (Node* child : node->children) destroy(child);
	delete node;
}

Qdc::FilelistModel::Node* Qdc::FilelistModel::build(const RemoteDirInfo& source,
                                                    Node* parent,
                                                    const QString& needle,
                                                    bool ancestorMatched)
{
	const bool filtering = !needle.isEmpty();
	const bool nameMatches = filtering
		&& source.name.contains(needle, Qt::CaseInsensitive);

	/* Not filtering, or inside something that matched: everything stays. */
	const bool keepAll = !filtering || ancestorMatched || nameMatches;

	Node* node = new Node;
	node->name = source.name;
	node->size = source.size;
	node->directory = true;
	node->parent = parent;

	for (const RemoteDirInfo& child : source.dirs)
	{
		if (Node* built = build(child, node, needle, keepAll))
		{
			built->rowInParent = static_cast<int>(node->children.size());
			node->children.push_back(built);
		}
	}

	for (const RemoteFileInfo& file : source.files)
	{
		if (!keepAll && !file.name.contains(needle, Qt::CaseInsensitive))
			continue;

		Node* leaf = new Node;
		leaf->name = file.name;
		leaf->tth = file.tth;
		leaf->size = file.size;
		leaf->directory = false;
		leaf->parent = node;
		leaf->rowInParent = static_cast<int>(node->children.size());

		node->children.push_back(leaf);
	}

	/* A directory holding nothing that matched, and not matching itself, goes. */
	if (filtering && !keepAll && node->children.empty())
	{
		delete node;
		return nullptr;
	}

	return node;
}

void Qdc::FilelistModel::rebuild()
{
	beginResetModel();

	destroy(m_root);
	m_root = build(m_source, nullptr, m_filter, false);

	endResetModel();
}

void Qdc::FilelistModel::setRoot(const RemoteDirInfo& root)
{
	m_source = root;
	rebuild();
}

void Qdc::FilelistModel::clear()
{
	m_source = RemoteDirInfo();
	m_filter.clear();
	rebuild();
}

void Qdc::FilelistModel::setFilter(const QString& needle)
{
	if (needle == m_filter) return;

	m_filter = needle;
	rebuild();
}

Qdc::FilelistModel::Node* Qdc::FilelistModel::nodeFor(const QModelIndex& index) const
{
	/*
	 * An invalid index is the root, which is not itself a row - its children are
	 * the top level. That is why the root node's own name is never displayed.
	 */
	if (!index.isValid()) return m_root;
	return static_cast<Node*>(index.internalPointer());
}

QModelIndex Qdc::FilelistModel::index(int row, int column,
                                      const QModelIndex& parent) const
{
	if (!hasIndex(row, column, parent)) return QModelIndex();

	Node* node = nodeFor(parent);
	if (!node || row < 0 || row >= static_cast<int>(node->children.size()))
		return QModelIndex();

	return createIndex(row, column, node->children[static_cast<size_t>(row)]);
}

QModelIndex Qdc::FilelistModel::parent(const QModelIndex& child) const
{
	Node* node = child.isValid() ? static_cast<Node*>(child.internalPointer())
	                             : nullptr;

	if (!node || !node->parent || node->parent == m_root) return QModelIndex();

	return createIndex(node->parent->rowInParent, 0, node->parent);
}

int Qdc::FilelistModel::rowCount(const QModelIndex& parent) const
{
	/* Only the name column has children, as with any tree view. */
	if (parent.isValid() && parent.column() != NameColumn) return 0;

	Node* node = nodeFor(parent);
	return node ? static_cast<int>(node->children.size()) : 0;
}

int Qdc::FilelistModel::columnCount(const QModelIndex&) const
{
	return ColumnCount;
}

QVariant Qdc::FilelistModel::data(const QModelIndex& index, int role) const
{
	Node* node = index.isValid() ? static_cast<Node*>(index.internalPointer())
	                             : nullptr;
	if (!node) return QVariant();

	if (role == Qt::TextAlignmentRole && index.column() == SizeColumn)
		return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);

	if (role != Qt::DisplayRole && role != Qt::ToolTipRole) return QVariant();

	QString text;

	switch (index.column())
	{
		case NameColumn:
			/*
			 * A whole file list is names a stranger chose, drawn as a dense tree - the
			 * case RTF0 has in mind. Disarmed here and not in build(), so that the
			 * filter still matches what the peer sent and nameFor() can still be asked
			 * for a name to download by.
			 */
			text = RichText::displayText(node->name);
			break;

		case SizeColumn:
			text = format_size(node->size);
			break;

		case TthColumn:
			text = node->tth;
			break;

		default:
			return QVariant();
	}

	/*
	 * The tooltip is the same text - it is here because a name is wider than the column
	 * far more often than not - but a tooltip is drawn as markup where a row is not, so
	 * it is handed over as markup of ours that says so.
	 */
	if (role == Qt::ToolTipRole) return RichText::literalHtml(text);

	return text;
}

QVariant Qdc::FilelistModel::headerData(int section, Qt::Orientation orientation,
                                        int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return QVariant();

	switch (section)
	{
		case NameColumn: return tr("Name");
		case SizeColumn: return tr("Size");
		case TthColumn:  return tr("TTH");
		default:         return QVariant();
	}
}

bool Qdc::FilelistModel::isDirectory(const QModelIndex& index) const
{
	Node* node = index.isValid() ? static_cast<Node*>(index.internalPointer())
	                             : nullptr;
	return node && node->directory;
}

QString Qdc::FilelistModel::nameFor(const QModelIndex& index) const
{
	Node* node = index.isValid() ? static_cast<Node*>(index.internalPointer())
	                             : nullptr;
	return node ? node->name : QString();
}

QString Qdc::FilelistModel::tthFor(const QModelIndex& index) const
{
	Node* node = index.isValid() ? static_cast<Node*>(index.internalPointer())
	                             : nullptr;
	return node ? node->tth : QString();
}

quint64 Qdc::FilelistModel::sizeFor(const QModelIndex& index) const
{
	Node* node = index.isValid() ? static_cast<Node*>(index.internalPointer())
	                             : nullptr;
	return node ? node->size : 0;
}

quint64 Qdc::FilelistModel::totalSize() const
{
	return m_source.size;
}

quint64 Qdc::FilelistModel::totalFiles() const
{
	return m_source.fileCount;
}

Qdc::FilelistView::FilelistView(FilelistBridge* filelists, const QString& hubUrl,
                                const QString& sourceId, QWidget* parent)
	: QWidget(parent)
	, m_filelists(filelists)
	, m_hubUrl(hubUrl)
	, m_sourceId(sourceId)
	, m_pages(new QStackedWidget(this))
	, m_idle(new QLabel(this))
	, m_fetch(new QPushButton(tr("Fetch the file list"), this))
	, m_waiting(new QLabel(this))
	, m_tree(new QTreeView(this))
	, m_model(new FilelistModel(this))
	, m_filter(new QLineEdit(this))
	, m_summary(new QLabel(this))
{
	m_idle->setAlignment(Qt::AlignCenter);
	m_idle->setWordWrap(true);
	m_idle->setText(m_filelists
		? tr("Ask this user for a list of what they share.")
		: tr("Browsing is not available."));

	m_fetch->setEnabled(m_filelists != nullptr);
	connect(m_fetch, &QPushButton::clicked, this, &FilelistView::requestList);

	QWidget* idlePage = new QWidget(this);
	QVBoxLayout* idleLayout = new QVBoxLayout(idlePage);
	idleLayout->addStretch(1);
	idleLayout->addWidget(m_idle);
	idleLayout->addSpacing(8);
	idleLayout->addWidget(m_fetch, 0, Qt::AlignHCenter);
	idleLayout->addStretch(2);

	m_waiting->setAlignment(Qt::AlignCenter);
	m_waiting->setWordWrap(true);
	m_waiting->setText(tr("Waiting for the file list.\n\n"
	                      "This takes as long as the other client takes to answer, "
	                      "and will be retried if they are not reachable yet."));

	QWidget* waitingPage = new QWidget(this);
	QVBoxLayout* waitingLayout = new QVBoxLayout(waitingPage);
	waitingLayout->addStretch(1);
	waitingLayout->addWidget(m_waiting);
	waitingLayout->addStretch(2);

	m_tree->setModel(m_model);
	m_tree->setUniformRowHeights(true);
	m_tree->setAlternatingRowColors(true);
	m_tree->setSortingEnabled(false);
	m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_tree->header()->setSectionResizeMode(FilelistModel::NameColumn,
	                                       QHeaderView::Stretch);
	m_tree->header()->setSectionResizeMode(FilelistModel::SizeColumn,
	                                       QHeaderView::ResizeToContents);

	/* The hash is long, exact, and rarely wanted; there if it is asked for. */
	m_tree->setColumnHidden(FilelistModel::TthColumn, true);

	connect(m_tree, &QTreeView::activated, this, [this](const QModelIndex& index) {
		if (m_model->isDirectory(index)) return;

		/*
		 * The name as it arrived rather than as it is drawn: what goes out is a request
		 * for a file, and the row's text has been reduced for reading.
		 */
		emit fileActivated(m_model->nameFor(index),
		                   m_model->sizeFor(index),
		                   m_model->tthFor(index));
	});

	m_filter->setPlaceholderText(tr("Filter by name"));
	m_filter->setClearButtonEnabled(true);

	connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& text) {
		m_model->setFilter(text.trimmed());

		/*
		 * Expanded while filtering, because the matches are usually deep and a
		 * collapsed tree of surviving folders looks like nothing was found.
		 */
		if (!text.trimmed().isEmpty()) m_tree->expandAll();
	});

	QHBoxLayout* tools = new QHBoxLayout;
	tools->setContentsMargins(0, 0, 0, 0);
	tools->addWidget(m_filter, 1);
	tools->addWidget(m_summary);

	QWidget* treePage = new QWidget(this);
	QVBoxLayout* treeLayout = new QVBoxLayout(treePage);
	treeLayout->setContentsMargins(6, 6, 6, 6);
	treeLayout->setSpacing(6);
	treeLayout->addLayout(tools);
	treeLayout->addWidget(m_tree, 1);

	m_pages->insertWidget(IdlePage, idlePage);
	m_pages->insertWidget(WaitingPage, waitingPage);
	m_pages->insertWidget(TreePage, treePage);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_pages);

	if (m_filelists)
	{
		connect(m_filelists, &FilelistBridge::filelistReady,
		        this, &FilelistView::onReady);
		connect(m_filelists, &FilelistBridge::filelistFailed,
		        this, &FilelistView::onFailed);
	}
}

void Qdc::FilelistView::setSource(const QString& hubUrl, const QString& sourceId)
{
	if (hubUrl == m_hubUrl && sourceId == m_sourceId) return;

	m_hubUrl = hubUrl;
	m_sourceId = sourceId;

	/* A list belonging to someone else is no longer relevant. */
	m_model->clear();
	m_pages->setCurrentIndex(IdlePage);
}

bool Qdc::FilelistView::matches(const QString& hubUrl, const QString& sourceId) const
{
	return hubUrl == m_hubUrl && sourceId == m_sourceId;
}

void Qdc::FilelistView::requestList()
{
	if (!m_filelists || m_sourceId.isEmpty()) return;

	m_filelists->request(m_hubUrl, m_sourceId);
	m_pages->setCurrentIndex(WaitingPage);
}

void Qdc::FilelistView::onReady(const QString& hubUrl, const QString& sourceId,
                                const RemoteDirInfo& root)
{
	if (!matches(hubUrl, sourceId)) return;

	m_model->setRoot(root);

	m_summary->setText(tr("%1 in %n file(s)", nullptr,
	                      static_cast<int>(m_model->totalFiles()))
		.arg(format_size(m_model->totalSize())));

	m_pages->setCurrentIndex(TreePage);
}

void Qdc::FilelistView::onFailed(const QString& hubUrl, const QString& sourceId,
                                 const QString& reason)
{
	if (!matches(hubUrl, sourceId)) return;

	m_idle->setText(reason);
	m_fetch->setText(tr("Try again"));
	m_pages->setCurrentIndex(IdlePage);
}

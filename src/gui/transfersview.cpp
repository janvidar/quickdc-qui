/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "transfersview.h"

#include "fileactions.h"
#include "progressdelegate.h"
#include "transfermodels.h"

#include <core/completed.h>
#include <core/transferbridge.h>

#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QSet>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

/**
 * How often the active table is refreshed.
 *
 * The core polls its sockets every 250ms and a rate estimate is smoothed over
 * several seconds, so asking faster than this would show the same numbers at more
 * cost. Only runs while the view is visible.
 */
constexpr int POLL_INTERVAL_MS = 1000;

/**
 * How wide a progress column is.
 *
 * Fixed rather than sized to its contents, because its contents are a bar and the
 * header word "Progress" is narrower than a bar wants to be.
 */
constexpr int PROGRESS_WIDTH = 130;

/**
 * How long the readings asked for by a transfer ending are coalesced over.
 *
 * A file arriving in chunks ends a transfer every time one of them completes, and
 * one reading per chunk is both a job on the core thread several times a second
 * and a poor sample of what is moving - it is taken in the very moment nothing is
 * in flight. Short enough that a transfer which has really ended leaves the table
 * promptly, since the timer above is not running while this view is hidden.
 */
constexpr int END_REFRESH_DELAY_MS = 500;

/**
 * What makes two of the core's transfers the same thing being moved.
 *
 * The events are one per chunk, so this is how four of them are recognised as one
 * file. The direction is part of it because the same file can be going both ways
 * at once, and a transfer with no file to name is only ever itself. Same rule as
 * TransferBridge::foldByFile(), which folds the rows the table draws.
 */
QString live_key(const Qdc::TransferInfo& transfer)
{
	if (transfer.filename.isEmpty())
		return QStringLiteral("?") + QString::number(transfer.id);

	return (transfer.upload ? QStringLiteral("up:") : QStringLiteral("down:"))
		+ transfer.filename;
}

/** The order the priority buttons step through. */
const QList<Qdc::Priority>& priority_ladder()
{
	static const QList<Qdc::Priority> ladder = {
		Qdc::Priority::Paused,
		Qdc::Priority::Low,
		Qdc::Priority::Normal,
		Qdc::Priority::High
	};

	return ladder;
}

/**
 * @param stretchColumn the column that takes the slack; every other column is
 *        sized to its contents, because a value that does not fit wraps onto a
 *        second line and makes the whole row twice as tall.
 */
void prepare(QTableView* table, int stretchColumn)
{
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	table->setAlternatingRowColors(true);
	table->setShowGrid(false);
	table->setFrameShape(QFrame::NoFrame);
	table->setContextMenuPolicy(Qt::CustomContextMenu);
	table->setSortingEnabled(false);

	table->verticalHeader()->setVisible(false);
	table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

	QHeaderView* header = table->horizontalHeader();
	header->setHighlightSections(false);

	const int columns = table->model() ? table->model()->columnCount() : 0;

	for (int n = 0; n < columns; ++n)
	{
		header->setSectionResizeMode(n, (n == stretchColumn)
			? QHeaderView::Stretch : QHeaderView::ResizeToContents);
	}
}

}

Qdc::TransfersView::TransfersView(TransferBridge* transfers, Completed* completed,
                                  QWidget* parent)
	: View(parent)
	, m_transfers(transfers)
	, m_completed(completed)
	, m_tabs(new QTabWidget(this))
	, m_queueTable(new QTableView(this))
	, m_queueModel(new QueueModel(this))
	, m_activeTable(new QTableView(this))
	, m_activeModel(new ActiveTransfersModel(this))
	, m_completedTable(new QTableView(this))
	, m_completedModel(new CompletedModel(this))
	, m_summary(new QLabel(this))
	, m_poll(new QTimer(this))
	, m_endRefresh(new QTimer(this))
{
	m_queueTable->setModel(m_queueModel);
	m_queueTable->setItemDelegateForColumn(QueueModel::ProgressColumn,
		new ProgressDelegate(QueueModel::FractionRole, this));
	prepare(m_queueTable, QueueModel::NameColumn);
	m_queueTable->horizontalHeader()->setSectionResizeMode(
		QueueModel::ProgressColumn, QHeaderView::Fixed);
	m_queueTable->setColumnWidth(QueueModel::ProgressColumn, PROGRESS_WIDTH);

	m_activeTable->setModel(m_activeModel);
	m_activeTable->setItemDelegateForColumn(ActiveTransfersModel::ProgressColumn,
		new ProgressDelegate(ActiveTransfersModel::FractionRole, this));
	prepare(m_activeTable, ActiveTransfersModel::NameColumn);
	m_activeTable->horizontalHeader()->setSectionResizeMode(
		ActiveTransfersModel::ProgressColumn, QHeaderView::Fixed);
	m_activeTable->setColumnWidth(ActiveTransfersModel::ProgressColumn,
	                              PROGRESS_WIDTH);

	m_completedTable->setModel(m_completedModel);
	prepare(m_completedTable, CompletedModel::NameColumn);

	m_tabs->setDocumentMode(true);
	m_tabs->addTab(m_queueTable, tr("Queue"));
	m_tabs->addTab(m_activeTable, tr("Active"));
	m_tabs->addTab(m_completedTable, tr("Completed"));

	m_summary->setContentsMargins(8, 2, 8, 4);

	m_summary->setObjectName(QStringLiteral("mutedText"));

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(m_tabs, 1);
	layout->addWidget(m_summary);

	connect(m_queueTable, &QTableView::customContextMenuRequested,
	        this, &TransfersView::showQueueMenu);
	connect(m_activeTable, &QTableView::customContextMenuRequested,
	        this, &TransfersView::showActiveMenu);
	connect(m_completedTable, &QTableView::customContextMenuRequested,
	        this, &TransfersView::showCompletedMenu);

	connect(m_completedTable, &QTableView::activated, this, [this](const QModelIndex& index) {
		const CompletedDownload entry = m_completedModel->at(index.row());
		if (!FileActions::open(entry.filename))
		{
			QMessageBox::warning(this, tr("Open"),
				tr("'%1' could not be opened. It may have been moved or deleted.")
					.arg(entry.filename));
		}
	});

	m_poll->setInterval(POLL_INTERVAL_MS);

	m_endRefresh->setSingleShot(true);
	m_endRefresh->setInterval(END_REFRESH_DELAY_MS);

	connect(m_endRefresh, &QTimer::timeout, this, [this] {
		if (m_transfers) m_transfers->refresh();
	});

	/*
	 * The timer asks; the answer arrives on a signal. Reading the figures straight out
	 * of the core meant waiting for whatever its thread was in the middle of, which
	 * stopped this window drawing for as long as that took.
	 */
	connect(m_poll, &QTimer::timeout, this, [this] {
		if (m_transfers) m_transfers->refresh();
	});

	if (m_completed)
	{
		connect(m_completed, &Completed::changed,
		        this, &TransfersView::refreshCompleted);
	}

	if (m_transfers)
	{
		connect(m_transfers, &TransferBridge::snapshotChanged, this, [this] {
			refreshQueue();
			refreshActive();
			refreshCounters();
		});

		connect(m_transfers, &TransferBridge::transferStarted, this,
		        [this](const TransferInfo& transfer) {
			m_live.insert(transfer.id, live_key(transfer));
			refreshCounters();
		});

		/* All three endings are the same to a count. */
		const auto ended = [this](const TransferInfo& transfer) {
			m_live.remove(transfer.id);
			refreshCounters();

			/*
			 * Asked for rather than read: the figures follow on snapshotChanged().
			 * Coalesced, because a chunk of a file ending is one of these and there
			 * are several of those a second - see END_REFRESH_DELAY_MS.
			 */
			if (!m_endRefresh->isActive()) m_endRefresh->start();
		};

		connect(m_transfers, &TransferBridge::transferFinished, this, ended);
		connect(m_transfers, &TransferBridge::transferStopped, this, ended);
		connect(m_transfers, &TransferBridge::transferError, this, ended);

		connect(m_transfers, &TransferBridge::downloadCompleted, this,
		        [this](const QString& filename, quint64 size, const QString& tth) {
			if (!m_completed) return;

			CompletedDownload entry;
			entry.filename = filename;
			entry.size = size;
			entry.tth = tth;
			entry.finished = QDateTime::currentDateTime();

			m_completed->add(entry);
		});

		/*
		 * Nothing is seeded from the snapshot on purpose. Its rows stand for files
		 * rather than for the core's transfers, so an ending - which names one
		 * transfer - could never retire a row put here from one, and the count would
		 * stay too high for the rest of the run. refreshCounters() consults the
		 * snapshot directly instead.
		 */
	}

	refreshQueue();
	refreshActive();
	refreshCompleted();
}

QString Qdc::TransfersView::title() const
{
	return tr("Transfers");
}

Qdc::Glyph Qdc::TransfersView::glyph() const
{
	return Glyph::Transfers;
}

int Qdc::TransfersView::currentTab() const
{
	return m_tabs->currentIndex();
}

void Qdc::TransfersView::setCurrentTab(int index)
{
	m_tabs->setCurrentIndex(index);
}

int Qdc::TransfersView::queueRowCount() const
{
	return m_queueModel->rowCount();
}

int Qdc::TransfersView::activeRowCount() const
{
	return m_activeModel->rowCount();
}

int Qdc::TransfersView::completedRowCount() const
{
	return m_completedModel->rowCount();
}

void Qdc::TransfersView::showEvent(QShowEvent* event)
{
	View::showEvent(event);

	/* So the tables are not a second out of date the moment they appear. */
	if (m_transfers) m_transfers->refresh();

	refreshQueue();
	refreshActive();
	m_poll->start();
}

void Qdc::TransfersView::hideEvent(QHideEvent* event)
{
	View::hideEvent(event);
	m_poll->stop();
}

void Qdc::TransfersView::refreshQueue()
{
	/*
	 * What is waiting, rather than the whole queue: a file being fetched is in the
	 * queue until it is complete, and it is already on the next tab along with a
	 * rate and a progress bar. Listing it here as well only says it twice.
	 */
	if (m_transfers) m_queueModel->setEntries(m_transfers->waitingEntries());
	refreshCounters();
}

void Qdc::TransfersView::refreshActive()
{
	if (m_transfers) m_activeModel->setTransfers(m_transfers->activeTransfers());
	refreshCounters();
}

void Qdc::TransfersView::refreshCompleted()
{
	if (m_completed) m_completedModel->setEntries(m_completed->entries());
	refreshCounters();
}

int Qdc::TransfersView::liveFileCount() const
{
	/*
	 * By name rather than by entry: the core starts a transfer per chunk, so a
	 * file being fetched in four of them arrived here four times.
	 */
	QSet<QString> files;

	for (const QString& file : m_live)
		files.insert(file);

	return static_cast<int>(files.size());
}

void Qdc::TransfersView::refreshCounters()
{
	/*
	 * The events and the snapshot each know something the other does not: an event
	 * arrives the moment a transfer starts but can be missed, while the snapshot is
	 * complete and up to a poll interval old - and is not refreshed at all while
	 * this view is hidden. Whichever of them says more files are moving is the one
	 * to believe.
	 */
	setBadgeCount(qMax(liveFileCount(), m_activeModel->rowCount()));

	m_tabs->setTabText(0, m_queueModel->rowCount() > 0
		? tr("Queue (%1)").arg(m_queueModel->rowCount())
		: tr("Queue"));

	m_tabs->setTabText(1, m_activeModel->rowCount() > 0
		? tr("Active (%1)").arg(m_activeModel->rowCount())
		: tr("Active"));

	quint64 up = 0;
	quint64 down = 0;

	for (int n = 0; n < m_activeModel->rowCount(); ++n)
	{
		const TransferInfo transfer = m_activeModel->at(n);
		if (transfer.upload)
			up += transfer.bytesPerSecond;
		else
			down += transfer.bytesPerSecond;
	}

	const QLocale locale;

	m_summary->setText(tr("↓ %1/s   ↑ %2/s")
		.arg(locale.formattedDataSize(static_cast<qint64>(down)),
		     locale.formattedDataSize(static_cast<qint64>(up))));
}

QVector<Qdc::QueueEntry> Qdc::TransfersView::selectedQueueEntries() const
{
	QVector<QueueEntry> entries;

	const QModelIndexList rows =
		m_queueTable->selectionModel()->selectedRows(QueueModel::NameColumn);

	for (const QModelIndex& index : rows)
	{
		const QueueEntry entry = m_queueModel->at(index.row());
		if (!entry.filename.isEmpty()) entries.append(entry);
	}

	return entries;
}

QVector<Qdc::QueueEntry> Qdc::TransfersView::selectedActiveEntries() const
{
	QVector<QueueEntry> entries;
	if (!m_transfers) return entries;

	/* The whole queue, transfers included: those rows are what this tab shows. */
	const QVector<QueueEntry> queued = m_transfers->queueSnapshot();

	const QModelIndexList rows = m_activeTable->selectionModel()
		->selectedRows(ActiveTransfersModel::NameColumn);

	for (const QModelIndex& index : rows)
	{
		const TransferInfo transfer = m_activeModel->at(index.row());
		if (transfer.upload || transfer.filename.isEmpty()) continue;

		for (const QueueEntry& entry : queued)
		{
			if (entry.filename != transfer.filename) continue;

			entries.append(entry);
			break;
		}
	}

	return entries;
}

void Qdc::TransfersView::nudgePriority(int steps)
{
	if (!m_transfers || steps == 0) return;

	const QList<Priority>& ladder = priority_ladder();

	for (const QueueEntry& entry : selectedQueueEntries())
	{
		const int at = static_cast<int>(ladder.indexOf(entry.priority));
		if (at < 0) continue;

		const int moved = qBound(0, at + steps,
		                         static_cast<int>(ladder.size()) - 1);
		if (moved == at) continue;

		m_transfers->setPriority(entry.filename, ladder.at(moved));
	}

	refreshQueue();
}

void Qdc::TransfersView::showQueueMenu(const QPoint& where)
{
	showEntryMenu(selectedQueueEntries(), m_queueTable, where);
}

void Qdc::TransfersView::showActiveMenu(const QPoint& where)
{
	showEntryMenu(selectedActiveEntries(), m_activeTable, where);
}

void Qdc::TransfersView::showEntryMenu(const QVector<QueueEntry>& selected,
                                       QTableView* table, const QPoint& where)
{
	if (selected.isEmpty() || !m_transfers || !table) return;

	QMenu menu(this);

	menu.addAction(tr("Raise priority"), this, [this] { nudgePriority(1); });
	menu.addAction(tr("Lower priority"), this, [this] { nudgePriority(-1); });

	menu.addSeparator();

	/*
	 * Pausing is the bottom of the priority ladder rather than a state of its own,
	 * which is the core's model - the scheduler simply starts nothing for it.
	 */
	const bool anyRunning = std::any_of(selected.begin(), selected.end(),
		[](const QueueEntry& entry) {
			return entry.priority != Priority::Paused;
		});

	if (anyRunning)
	{
		menu.addAction(tr("Pause"), this, [this, selected] {
			for (const QueueEntry& entry : selected)
				m_transfers->setPriority(entry.filename, Priority::Paused);

			refreshQueue();
		});
	}
	else
	{
		menu.addAction(tr("Resume"), this, [this, selected] {
			for (const QueueEntry& entry : selected)
				m_transfers->setPriority(entry.filename, Priority::Normal);

			refreshQueue();
		});
	}

	menu.addSeparator();

	menu.addAction(tr("Remove from queue"), this, [this, selected] {
		for (const QueueEntry& entry : selected)
			m_transfers->removeFromQueue(entry.filename, false);

		refreshQueue();
	});

	menu.addAction(tr("Remove and delete the partial file"), this,
	               [this, selected] {
		const QString question = (selected.size() == 1)
			? tr("Delete the partly downloaded '%1'?\n\n"
			     "What has been fetched so far will be lost.")
				.arg(QFileInfo(selected.first().filename).fileName())
			: tr("Delete %n partly downloaded file(s)?\n\n"
			     "What has been fetched so far will be lost.", nullptr,
			     static_cast<int>(selected.size()));

		/*
		 * Asked, because this throws away work that may have taken hours and
		 * cannot be undone. Removing the entry alone is not confirmed - the
		 * partial file survives and the download can be queued again.
		 */
		if (QMessageBox::question(this, tr("Delete partial files"), question,
				QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
			!= QMessageBox::Yes)
		{
			return;
		}

		for (const QueueEntry& entry : selected)
			m_transfers->removeFromQueue(entry.filename, true);

		refreshQueue();
	});

	menu.exec(table->viewport()->mapToGlobal(where));
}

void Qdc::TransfersView::showCompletedMenu(const QPoint& where)
{
	const QModelIndexList rows =
		m_completedTable->selectionModel()->selectedRows(CompletedModel::NameColumn);

	if (rows.isEmpty() || !m_completed) return;

	QList<CompletedDownload> selected;
	for (const QModelIndex& index : rows)
		selected.append(m_completedModel->at(index.row()));

	const CompletedDownload& first = selected.first();
	const bool present = FileActions::exists(first.filename);

	QMenu menu(this);

	QAction* open = menu.addAction(tr("Open"), this, [first] {
		FileActions::open(first.filename);
	});
	open->setEnabled(present && selected.size() == 1);

	QAction* reveal = menu.addAction(tr("Show in folder"), this, [first] {
		FileActions::reveal(first.filename);
	});
	reveal->setEnabled(present && selected.size() == 1);

	menu.addSeparator();

	QAction* erase = menu.addAction(tr("Delete from disk"), this, [this, selected] {
		const QString question = (selected.size() == 1)
			? tr("Delete '%1' from disk?")
				.arg(QFileInfo(selected.first().filename).fileName())
			: tr("Delete %n file(s) from disk?", nullptr,
			     static_cast<int>(selected.size()));

		if (QMessageBox::question(this, tr("Delete from disk"), question,
				QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
			!= QMessageBox::Yes)
		{
			return;
		}

		for (const CompletedDownload& entry : selected)
		{
			/*
			 * Dropped from the history whether or not the file was still there:
			 * either way it is gone now, and leaving the row would suggest
			 * otherwise.
			 */
			FileActions::remove(entry.filename);
			m_completed->remove(entry.filename);
		}
	});
	erase->setEnabled(present);

	menu.addAction(tr("Remove from this list"), this, [this, selected] {
		for (const CompletedDownload& entry : selected)
			m_completed->remove(entry.filename);
	});

	menu.addSeparator();

	menu.addAction(tr("Clear the list"), this, [this] {
		if (QMessageBox::question(this, tr("Clear the list"),
				tr("Forget every finished download?\n\nThe files are not touched."),
				QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
			== QMessageBox::Yes)
		{
			m_completed->clear();
		}
	});

	menu.exec(m_completedTable->viewport()->mapToGlobal(where));
}

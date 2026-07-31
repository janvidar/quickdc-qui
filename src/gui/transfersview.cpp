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

	QPalette muted = m_summary->palette();
	muted.setColor(QPalette::WindowText, muted.color(QPalette::PlaceholderText));
	m_summary->setPalette(muted);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(m_tabs, 1);
	layout->addWidget(m_summary);

	connect(m_queueTable, &QTableView::customContextMenuRequested,
	        this, &TransfersView::showQueueMenu);
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
			m_live.insert(transfer.id);
			refreshCounters();
		});

		/* All three endings are the same to a count. */
		const auto ended = [this](const TransferInfo& transfer) {
			m_live.remove(transfer.id);
			refreshCounters();

			/* Asked for rather than read: the figures follow on snapshotChanged(). */
			if (m_transfers) m_transfers->refresh();
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

		/* Whatever was already running or queued before this view existed. */
		for (const TransferInfo& transfer : m_transfers->activeTransfers())
			m_live.insert(transfer.id);
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
	if (m_transfers) m_queueModel->setEntries(m_transfers->queueSnapshot());
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

void Qdc::TransfersView::refreshCounters()
{
	setBadgeCount(static_cast<int>(m_live.size()));

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
	const QVector<QueueEntry> selected = selectedQueueEntries();
	if (selected.isEmpty() || !m_transfers) return;

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

	menu.exec(m_queueTable->viewport()->mapToGlobal(where));
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

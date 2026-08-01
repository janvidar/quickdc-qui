/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_TRANSFERSVIEW_H
#define HAVE_QUICKDC_QUI_TRANSFERSVIEW_H

#include "view.h"

#include <core/coretypes.h>

#include <QHash>

class QLabel;
class QTabWidget;
class QTableView;
class QTimer;

namespace Qdc {

class ActiveTransfersModel;
class Completed;
class CompletedModel;
class QueueModel;
class TransferBridge;

/**
 * The download queue, what is moving, and what has finished.
 *
 * Three tabs, because the three answer different questions and only one of them
 * is live. The queue changes when the core says so; the active table has to be
 * polled, since the core reports no progress; the completed list changes only when
 * a download finishes.
 *
 * The rail badge counts transfers in flight, kept from the bridge's start and stop
 * events rather than from the poll - those are the only moments it can change, and
 * a badge that waited for the next tick would lag behind the table beside it.
 */
class TransfersView final : public View {
	Q_OBJECT

public:
	/**
	 * @param transfers may be null, in which case every tab is empty and the
	 *        actions do nothing.
	 * @param completed may be null; the Completed tab then says so rather than
	 *        showing an empty list that will never fill.
	 */
	TransfersView(TransferBridge* transfers, Completed* completed,
	              QWidget* parent = nullptr);

	QString title() const override;
	Glyph glyph() const override;

	/** For tests: 0 queue, 1 active, 2 completed. */
	int currentTab() const;
	void setCurrentTab(int index);

	int queueRowCount() const;
	int activeRowCount() const;
	int completedRowCount() const;

protected:
	/**
	 * Polling runs only while the view is on screen. A rate nobody is looking at
	 * is not worth a job on the core thread every second; the badge is event
	 * driven and stays correct regardless.
	 */
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	void refreshQueue();
	void refreshActive();
	void refreshCompleted();

	/** Recount the badge and the tab labels from what is known. */
	void refreshCounters();

	/** How many distinct files m_live holds. */
	int liveFileCount() const;

	void showQueueMenu(const QPoint& where);
	void showActiveMenu(const QPoint& where);
	void showCompletedMenu(const QPoint& where);

	/**
	 * The menu for a set of queued files, wherever they happen to be selected.
	 *
	 * The two tabs show the same files from different angles - one what is waiting,
	 * the other what is moving - so the same actions have to be reachable from
	 * either. Without this, keeping a transfer out of the queue tab would take away
	 * the only way to pause or cancel one.
	 */
	void showEntryMenu(const QVector<QueueEntry>& selected, QTableView* table,
	                   const QPoint& where);

	/** Move the selected entries one step up or down the priority scale. */
	void nudgePriority(int steps);

	/** The queue entries currently selected, in row order. */
	QVector<QueueEntry> selectedQueueEntries() const;

	/**
	 * The queue entries behind the selected rows of the active table.
	 *
	 * Uploads are skipped: what somebody else is fetching from us is not ours to
	 * pause or remove, and there is no queue entry for it either.
	 */
	QVector<QueueEntry> selectedActiveEntries() const;

	TransferBridge* m_transfers;
	Completed* m_completed;

	QTabWidget* m_tabs;

	QTableView* m_queueTable;
	QueueModel* m_queueModel;

	QTableView* m_activeTable;
	ActiveTransfersModel* m_activeModel;

	QTableView* m_completedTable;
	CompletedModel* m_completedModel;

	QLabel* m_summary;
	QTimer* m_poll;

	/**
	 * Coalesces the readings asked for by transfers ending, which for a file
	 * arriving in chunks is several a second. Single shot; started when one ends
	 * and left alone until it has fired.
	 */
	QTimer* m_endRefresh;

	/**
	 * Transfers believed to be in flight, for the badge, each against the file it
	 * is moving. Kept rather than counted because the core can report a transfer
	 * ending that was never seen starting, and decrementing on that would take the
	 * count below zero.
	 *
	 * The badge counts the files rather than the entries: the core's events are one
	 * per chunk, and a file arriving in four of them is one thing being downloaded.
	 */
	QHash<quintptr, QString> m_live;
};

}

#endif // HAVE_QUICKDC_QUI_TRANSFERSVIEW_H

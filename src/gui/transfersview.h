/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_TRANSFERSVIEW_H
#define HAVE_QUICKDC_QUI_TRANSFERSVIEW_H

#include "view.h"

#include <core/coretypes.h>

#include <QSet>

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

	void showQueueMenu(const QPoint& where);
	void showCompletedMenu(const QPoint& where);

	/** Move the selected entries one step up or down the priority scale. */
	void nudgePriority(int steps);

	/** The queue entries currently selected, in row order. */
	QVector<QueueEntry> selectedQueueEntries() const;

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
	 * Transfers believed to be in flight, for the badge. A set rather than a
	 * counter because the core can report a transfer ending that was never seen
	 * starting, and decrementing on that would take the count below zero.
	 */
	QSet<quintptr> m_live;
};

}

#endif // HAVE_QUICKDC_QUI_TRANSFERSVIEW_H

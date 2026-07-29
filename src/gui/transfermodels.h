/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_TRANSFERMODELS_H
#define HAVE_QUICKDC_QUI_TRANSFERMODELS_H

#include <core/completed.h>
#include <core/coretypes.h>

#include <QAbstractTableModel>
#include <QList>
#include <QVector>

namespace Qdc {

/**
 * What is still wanted.
 *
 * Reset from a whole snapshot rather than patched entry by entry, because that is
 * the shape the core offers: QueueManager::getStatus() fills a vector, and its
 * change events say only that something changed. The queue is short enough - tens
 * of entries, not thousands - that rebuilding it is cheaper than tracking which
 * row moved.
 */
class QueueModel final : public QAbstractTableModel {
	Q_OBJECT

public:
	enum Column {
		NameColumn,
		ProgressColumn,
		SizeColumn,
		SourcesColumn,
		PriorityColumn,
		ColumnCount
	};

	enum Role {
		/** The whole QueueEntry, for the delegate and the context menu. */
		EntryRole = Qt::UserRole + 1,

		/** 0.0 to 1.0, for the progress delegate. */
		FractionRole
	};

	explicit QueueModel(QObject* parent = nullptr);

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation,
	                    int role) const override;

	void setEntries(const QVector<QueueEntry>& entries);

	/** An empty entry if there is no such row. */
	QueueEntry at(int row) const;

private:
	QVector<QueueEntry> m_entries;
};

/**
 * What is moving right now, uploads and downloads together.
 *
 * Also snapshot-driven, but for a different reason: the core never reports
 * progress. QuickDC::TransferListener declares an EventTransferProgress that
 * nothing emits, so a rate or a progress bar can only be had by asking - which
 * the view does on a timer.
 */
class ActiveTransfersModel final : public QAbstractTableModel {
	Q_OBJECT

public:
	enum Column {
		DirectionColumn,
		NameColumn,
		ProgressColumn,
		RateColumn,
		TransferredColumn,
		ColumnCount
	};

	enum Role {
		TransferRole = Qt::UserRole + 1,
		FractionRole
	};

	explicit ActiveTransfersModel(QObject* parent = nullptr);

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation,
	                    int role) const override;

	/**
	 * Replace the contents.
	 *
	 * Rows whose identity and byte counts are unchanged are left alone and the
	 * rest are reported as changed, rather than resetting the model - a reset on
	 * every tick would drop the selection and collapse the scroll position once a
	 * second.
	 */
	void setTransfers(const QVector<TransferInfo>& transfers);

	TransferInfo at(int row) const;

private:
	QVector<TransferInfo> m_transfers;
};

/** What has finished. Fed from the Completed store, not from the core. */
class CompletedModel final : public QAbstractTableModel {
	Q_OBJECT

public:
	enum Column {
		NameColumn,
		SizeColumn,
		FinishedColumn,
		ColumnCount
	};

	enum Role {
		DownloadRole = Qt::UserRole + 1,

		/** Whether the file is still where it was put. */
		PresentRole
	};

	explicit CompletedModel(QObject* parent = nullptr);

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation,
	                    int role) const override;

	void setEntries(const QList<CompletedDownload>& entries);

	CompletedDownload at(int row) const;

private:
	QList<CompletedDownload> m_entries;
};

}

#endif // HAVE_QUICKDC_QUI_TRANSFERMODELS_H

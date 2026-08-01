/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "transfermodels.h"

#include "fileactions.h"

#include <QFileInfo>
#include <QLocale>
#include <QPalette>
#include <QMetaEnum>

namespace {

QString base_name(const QString& path)
{
	const QString name = QFileInfo(path).fileName();
	return name.isEmpty() ? path : name;
}

QString format_size(quint64 bytes)
{
	return QLocale().formattedDataSize(static_cast<qint64>(bytes));
}

QString format_rate(quint64 bytesPerSecond)
{
	if (bytesPerSecond == 0) return QString();

	/* Per second, so the unit has to be spelled out; formattedDataSize does not. */
	return QObject::tr("%1/s").arg(format_size(bytesPerSecond));
}

double fraction(quint64 done, quint64 total)
{
	if (total == 0) return 0.0;
	return qBound(0.0, static_cast<double>(done) / static_cast<double>(total), 1.0);
}

QString priority_name(Qdc::Priority priority)
{
	switch (priority)
	{
		case Qdc::Priority::Paused: return QObject::tr("Paused");
		case Qdc::Priority::Low:    return QObject::tr("Low");
		case Qdc::Priority::Normal: return QObject::tr("Normal");
		case Qdc::Priority::High:   return QObject::tr("High");
	}

	return QString();
}

}

Qdc::QueueModel::QueueModel(QObject* parent)
	: QAbstractTableModel(parent)
{
}

int Qdc::QueueModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

int Qdc::QueueModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : ColumnCount;
}

QVariant Qdc::QueueModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
		return QVariant();

	const QueueEntry& entry = m_entries.at(index.row());

	switch (role)
	{
		case EntryRole:
			return QVariant::fromValue(entry);

		case FractionRole:
			return fraction(entry.done, entry.size);

		case Qt::TextAlignmentRole:
			return (index.column() == NameColumn)
				? QVariant()
				: QVariant(static_cast<int>(Qt::AlignRight | Qt::AlignVCenter));

		case Qt::ToolTipRole:
			/* The full path, which the Name column only shows the tail of. */
			return entry.filename;

		case Qt::DisplayRole:
			break;

		default:
			return QVariant();
	}

	switch (index.column())
	{
		case NameColumn:
			return base_name(entry.filename);

		case ProgressColumn:
			/* Painted by the delegate; the text is for accessibility and copying. */
			return QStringLiteral("%1%")
				.arg(fraction(entry.done, entry.size) * 100.0, 0, 'f', 1);

		case SizeColumn:
			return entry.size > 0 ? format_size(entry.size) : tr("unknown");

		case SourcesColumn:
			/*
			 * Both numbers, because they answer different questions: how many
			 * places it could come from, and how many are giving it to us now.
			 */
			return QStringLiteral("%1 / %2")
				.arg(entry.activeSources).arg(entry.sources);

		case PriorityColumn:
			return priority_name(entry.priority);

		default:
			return QVariant();
	}
}

QVariant Qdc::QueueModel::headerData(int section, Qt::Orientation orientation,
                                     int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return QVariant();

	switch (section)
	{
		case NameColumn:     return tr("Name");
		case ProgressColumn: return tr("Progress");
		case SizeColumn:     return tr("Size");
		case SourcesColumn:  return tr("Sources");
		case PriorityColumn: return tr("Priority");
		default:             return QVariant();
	}
}

void Qdc::QueueModel::setEntries(const QVector<QueueEntry>& entries)
{
	beginResetModel();
	m_entries = entries;
	endResetModel();
}

Qdc::QueueEntry Qdc::QueueModel::at(int row) const
{
	if (row < 0 || row >= m_entries.size()) return QueueEntry();
	return m_entries.at(row);
}

Qdc::ActiveTransfersModel::ActiveTransfersModel(QObject* parent)
	: QAbstractTableModel(parent)
{
}

int Qdc::ActiveTransfersModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(m_transfers.size());
}

int Qdc::ActiveTransfersModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : ColumnCount;
}

QVariant Qdc::ActiveTransfersModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= m_transfers.size())
		return QVariant();

	const TransferInfo& transfer = m_transfers.at(index.row());

	switch (role)
	{
		case TransferRole:
			return QVariant::fromValue(transfer);

		case FractionRole:
			return fraction(transfer.transferred, transfer.length);

		case Qt::TextAlignmentRole:
			return (index.column() == NameColumn)
				? QVariant()
				: QVariant(static_cast<int>(Qt::AlignRight | Qt::AlignVCenter));

		case Qt::ToolTipRole:
		{
			/*
			 * The row is the file; how many pieces it is arriving in is worth being
			 * able to find out, and is not worth a column of its own.
			 */
			if (transfer.chunks <= 1) return transfer.filename;

			return transfer.filename + QLatin1Char('\n')
				+ tr("%n chunk(s) at once", nullptr,
				     static_cast<int>(transfer.chunks));
		}

		case Qt::DisplayRole:
			break;

		default:
			return QVariant();
	}

	switch (index.column())
	{
		case DirectionColumn:
			/*
			 * Arrows rather than words: the column is one glyph wide and the
			 * direction is the thing being scanned for down a list.
			 */
			return transfer.upload ? QStringLiteral("↑") : QStringLiteral("↓");

		case NameColumn:
			return base_name(transfer.filename);

		case ProgressColumn:
			return QStringLiteral("%1%")
				.arg(fraction(transfer.transferred, transfer.length) * 100.0,
				     0, 'f', 1);

		case RateColumn:
			return format_rate(transfer.bytesPerSecond);

		case TransferredColumn:
			return transfer.length > 0
				? tr("%1 of %2").arg(format_size(transfer.transferred),
				                     format_size(transfer.length))
				: format_size(transfer.transferred);

		default:
			return QVariant();
	}
}

QVariant Qdc::ActiveTransfersModel::headerData(int section,
                                              Qt::Orientation orientation,
                                              int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return QVariant();

	switch (section)
	{
		case DirectionColumn:   return QString();
		case NameColumn:        return tr("Name");
		case ProgressColumn:    return tr("Progress");
		case RateColumn:        return tr("Rate");
		case TransferredColumn: return tr("Transferred");
		default:                return QVariant();
	}
}

void Qdc::ActiveTransfersModel::setTransfers(const QVector<TransferInfo>& transfers)
{
	/*
	 * A change in the number of rows is a reset; there is no cheap way to say
	 * which arrived or left, and it does not happen once a second.
	 */
	if (transfers.size() != m_transfers.size())
	{
		beginResetModel();
		m_transfers = transfers;
		endResetModel();
		return;
	}

	QVector<int> touched;

	for (int n = 0; n < transfers.size(); ++n)
	{
		const TransferInfo& fresh = transfers.at(n);
		const TransferInfo& stale = m_transfers.at(n);

		/*
		 * A different transfer in the same row, or the same one having moved. The
		 * rate is deliberately part of this: it changes every tick even when the
		 * byte count has not, because it is an estimate over the last few seconds.
		 */
		if (fresh.id != stale.id
		    || fresh.transferred != stale.transferred
		    || fresh.bytesPerSecond != stale.bytesPerSecond)
		{
			touched.append(n);
		}
	}

	m_transfers = transfers;

	for (const int row : touched)
	{
		emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
	}
}

Qdc::TransferInfo Qdc::ActiveTransfersModel::at(int row) const
{
	if (row < 0 || row >= m_transfers.size()) return TransferInfo();
	return m_transfers.at(row);
}

Qdc::CompletedModel::CompletedModel(QObject* parent)
	: QAbstractTableModel(parent)
{
}

int Qdc::CompletedModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

int Qdc::CompletedModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : ColumnCount;
}

QVariant Qdc::CompletedModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
		return QVariant();

	const CompletedDownload& entry = m_entries.at(index.row());

	switch (role)
	{
		case DownloadRole:
			return QVariant::fromValue(entry);

		case PresentRole:
			return FileActions::exists(entry.filename);

		case Qt::TextAlignmentRole:
			return (index.column() == NameColumn)
				? QVariant()
				: QVariant(static_cast<int>(Qt::AlignRight | Qt::AlignVCenter));

		case Qt::ToolTipRole:
			return FileActions::exists(entry.filename)
				? entry.filename
				: tr("%1\n\nThis file is no longer there.").arg(entry.filename);

		case Qt::ForegroundRole:
		{
			/* A file that has been moved or deleted since is dimmed, not hidden. */
			if (FileActions::exists(entry.filename)) return QVariant();

			QPalette palette;
			return palette.color(QPalette::PlaceholderText);
		}

		case Qt::DisplayRole:
			break;

		default:
			return QVariant();
	}

	switch (index.column())
	{
		case NameColumn:
			return base_name(entry.filename);

		case SizeColumn:
			return format_size(entry.size);

		case FinishedColumn:
			return QLocale().toString(entry.finished, QLocale::ShortFormat);

		default:
			return QVariant();
	}
}

QVariant Qdc::CompletedModel::headerData(int section, Qt::Orientation orientation,
                                         int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return QVariant();

	switch (section)
	{
		case NameColumn:     return tr("Name");
		case SizeColumn:     return tr("Size");
		case FinishedColumn: return tr("Finished");
		default:             return QVariant();
	}
}

void Qdc::CompletedModel::setEntries(const QList<CompletedDownload>& entries)
{
	beginResetModel();
	m_entries = entries;
	endResetModel();
}

Qdc::CompletedDownload Qdc::CompletedModel::at(int row) const
{
	if (row < 0 || row >= m_entries.size()) return CompletedDownload();
	return m_entries.at(row);
}

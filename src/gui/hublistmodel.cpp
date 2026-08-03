/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "hublistmodel.h"

#include <QLocale>

Qdc::HubListModel::HubListModel(QObject* parent)
	: QAbstractTableModel(parent)
{
}

int Qdc::HubListModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(m_hubs.size());
}

int Qdc::HubListModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : ColumnCount;
}

QVariant Qdc::HubListModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= m_hubs.size())
		return QVariant();

	const HubListEntry& hub = m_hubs.at(index.row());

	switch (role)
	{
		case HubRole:
			return QVariant::fromValue(hub);

		case AddressRole:
			return hub.address;

		case Qt::TextAlignmentRole:
			return (index.column() == UsersColumn)
				? QVariant(static_cast<int>(Qt::AlignRight | Qt::AlignVCenter))
				: QVariant();

		case Qt::ToolTipRole:
			/*
			 * The description in full and the address under it. A hublist
			 * description is a sentence or two written to be read, and the column is
			 * never wide enough for one.
			 */
			return hub.description.isEmpty()
				? hub.address
				: hub.description + QStringLiteral("\n") + hub.address;

		case Qt::EditRole:
			/* What a proxy sorts on; see the class comment. */
			if (index.column() == UsersColumn) return hub.users;
			break;

		case Qt::DisplayRole:
			break;

		default:
			return QVariant();
	}

	switch (index.column())
	{
		case NameColumn:
			/* A list may name no name, and then the address is what identifies it. */
			return hub.name.isEmpty() ? hub.address : hub.name;

		case UsersColumn:
			return hub.users > 0 ? QLocale().toString(hub.users) : QString();

		case DescriptionColumn:
			return hub.description;

		case CountryColumn:
			return hub.country;

		case AddressColumn:
			return hub.address;

		default:
			return QVariant();
	}
}

QVariant Qdc::HubListModel::headerData(int section, Qt::Orientation orientation,
                                       int role) const
{
	if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return QVariant();

	switch (section)
	{
		case NameColumn:        return tr("Hub");
		case UsersColumn:       return tr("Users");
		case DescriptionColumn: return tr("Description");
		case CountryColumn:     return tr("Country");
		case AddressColumn:     return tr("Address");
		default:                return QVariant();
	}
}

void Qdc::HubListModel::setHubs(const QVector<HubListEntry>& hubs)
{
	beginResetModel();
	m_hubs = hubs;
	endResetModel();
}

Qdc::HubListEntry Qdc::HubListModel::at(int row) const
{
	if (row < 0 || row >= m_hubs.size()) return HubListEntry();

	return m_hubs.at(row);
}

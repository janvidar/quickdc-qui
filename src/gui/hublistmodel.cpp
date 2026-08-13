/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "hublistmodel.h"

#include "richtext.h"

#include <core/favourites.h>

#include <QLocale>

Qdc::HubListModel::HubListModel(Favourites* favourites, QObject* parent)
	: QAbstractTableModel(parent)
	, m_favourites(favourites)
{
	if (m_favourites)
	{
		connect(m_favourites, &Favourites::changed,
		        this, &HubListModel::refreshFavourites);
	}
}

void Qdc::HubListModel::refreshFavourites()
{
	if (m_hubs.isEmpty()) return;

	emit dataChanged(index(0, FavouriteColumn),
	                 index(static_cast<int>(m_hubs.size()) - 1, FavouriteColumn),
	                 {Qt::DecorationRole, FavouriteRole});
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

		case FavouriteRole:
			return m_favourites && m_favourites->contains(hub.address);

		case Qt::TextAlignmentRole:
			return (index.column() == UsersColumn)
				? QVariant(static_cast<int>(Qt::AlignRight | Qt::AlignVCenter))
				: QVariant();

		case Qt::ToolTipRole:
			if (index.column() == FavouriteColumn)
			{
				return (m_favourites && m_favourites->contains(hub.address))
					? tr("Remove from favourites") : tr("Add to favourites");
			}

			/*
			 * The description in full and the address under it. A hublist
			 * description is a sentence or two written to be read, and the column is
			 * never wide enough for one.
			 *
			 * Both are what a hub's operator wrote, so both are disarmed - one at a
			 * time, since the newline between them is ours and a pass over the joined
			 * text would take it back out again.
			 */
			return hub.description.isEmpty()
				? RichText::displayText(hub.address)
				: RichText::displayText(hub.description) + QStringLiteral("\n")
					+ RichText::displayText(hub.address);

		case Qt::AccessibleTextRole:
			if (index.column() != FavouriteColumn) break;

			return (m_favourites && m_favourites->contains(hub.address))
				? tr("Favourite") : tr("Not a favourite");

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
		case FavouriteColumn:
			/* Nothing but the star, which the delegate draws. */
			return QVariant();

		case NameColumn:
			/* A list may name no name, and then the address is what identifies it. */
			return RichText::displayText(
				hub.name.isEmpty() ? hub.address : hub.name);

		case UsersColumn:
			return hub.users > 0 ? QLocale().toString(hub.users) : QString();

		case DescriptionColumn:
			return RichText::displayText(hub.description);

		case CountryColumn:
			return RichText::displayText(hub.country);

		case AddressColumn:
			/*
			 * Disarmed like the name and the description above it, since a host name
			 * reordered by an override is how one hub is dressed up as another. What
			 * connecting and bookmarking go by is AddressRole, which stays verbatim.
			 */
			return RichText::displayText(hub.address);

		default:
			return QVariant();
	}
}

QVariant Qdc::HubListModel::headerData(int section, Qt::Orientation orientation,
                                       int role) const
{
	if (orientation != Qt::Horizontal) return QVariant();

	/*
	 * The star's column is named to a pointer and to a screen reader but not on
	 * screen: the word is several times wider than the column.
	 */
	if (section == FavouriteColumn)
	{
		return (role == Qt::ToolTipRole || role == Qt::AccessibleTextRole)
			? tr("Favourite") : QVariant();
	}

	if (role != Qt::DisplayRole) return QVariant();

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

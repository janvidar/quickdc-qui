/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_HUBLISTMODEL_H
#define HAVE_QUICKDC_QUI_HUBLISTMODEL_H

#include <core/coretypes.h>

#include <QAbstractTableModel>
#include <QVector>

namespace Qdc {

class Favourites;

/**
 * The public hubs, as a table.
 *
 * Reset from a whole list rather than patched, because that is the shape a hublist
 * arrives in: a source is fetched, parsed and merged with the others, and what
 * comes out is a new list. Nothing about one hub is ever reported on its own.
 *
 * The user count is offered twice - formatted for Qt::DisplayRole and as a number
 * for Qt::EditRole - so that a proxy sorting on the edit role puts 1000 users after
 * 999 rather than between 100 and 2.
 */
class HubListModel final : public QAbstractTableModel {
	Q_OBJECT

public:
	enum Column {
		/** The star, which is all it holds; see FavouriteDelegate. */
		FavouriteColumn,

		NameColumn,
		UsersColumn,
		DescriptionColumn,
		CountryColumn,
		AddressColumn,
		ColumnCount
	};

	enum Role {
		/** The whole entry, for whatever the selection is used for. */
		HubRole = Qt::UserRole + 1,

		/** The address alone, which is what connecting needs. */
		AddressRole,

		/** Whether this hub is kept, for the star to draw itself by. */
		FavouriteRole
	};

	/**
	 * @param favourites which of these hubs are kept. May be null, in which case no
	 *        row is ever a favourite and the star is only ever an outline.
	 */
	explicit HubListModel(Favourites* favourites = nullptr,
	                      QObject* parent = nullptr);

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation,
	                    int role) const override;

	void setHubs(const QVector<HubListEntry>& hubs);

	/** An empty entry if there is no such row. */
	HubListEntry at(int row) const;

private:
	/**
	 * Say the stars have changed, and nothing else.
	 *
	 * One column of every row rather than a reset, so that the sort the header is
	 * showing, the selection and the scroll position all survive somebody keeping a
	 * hub - which a reset takes with it; see HubBrowser's stored sort.
	 */
	void refreshFavourites();

	Favourites* m_favourites;
	QVector<HubListEntry> m_hubs;
};

}

#endif // HAVE_QUICKDC_QUI_HUBLISTMODEL_H

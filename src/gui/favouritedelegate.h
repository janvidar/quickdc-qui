/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_FAVOURITEDELEGATE_H
#define HAVE_QUICKDC_QUI_FAVOURITEDELEGATE_H

#include <QStyledItemDelegate>

namespace Qdc {

/**
 * A star in front of a hub, which keeps it when pressed.
 *
 * One class for the three lists of hubs, which are three different kinds of view:
 * the public list is a table with a column of its own for this, and the other two
 * are single-column lists where the star has to lead the row's own text. The
 * difference is where the decoration goes and nothing else, so both are drawn by
 * the style rather than by hand and the rectangle to press is asked of the style
 * afterwards - which is the only way to be sure the two agree.
 *
 * The state and the address come from the model, under roles the caller names. The
 * store itself is not reached from here: pressing a star reports it and leaves
 * whoever owns the list to decide what that means.
 */
class FavouriteDelegate final : public QStyledItemDelegate {
	Q_OBJECT

public:
	enum Placement {
		/** A column with nothing else in it, as on the public hub list. */
		OwnColumn,

		/** In front of the row's text, as in a list with only one column. */
		BeforeTheText
	};

	FavouriteDelegate(Placement placement, int favouriteRole, int addressRole,
	                  QObject* parent = nullptr);

	/** How wide to make the column, where there is one. */
	static int columnWidth();

	void paint(QPainter* painter, const QStyleOptionViewItem& option,
	           const QModelIndex& index) const override;

signals:
	/** The star on the row for @a address was pressed. */
	void toggled(const QString& address);

protected:
	/**
	 * Presses on the star, and the space bar where a column can hold the focus.
	 *
	 * The view sends the delegate an event before it consults its edit triggers, so
	 * this needs no editable flag on the item.
	 */
	bool editorEvent(QEvent* event, QAbstractItemModel* model,
	                 const QStyleOptionViewItem& option,
	                 const QModelIndex& index) override;

private:
	/** Where the star was drawn, in the same coordinates a mouse event arrives in. */
	QRect starBox(const QStyleOptionViewItem& option, const QModelIndex& index) const;

	/** @a option with the star as its decoration, or unchanged if there is no star. */
	QStyleOptionViewItem starred(const QStyleOptionViewItem& option,
	                             const QModelIndex& index) const;

	Placement m_placement;
	int m_favouriteRole;
	int m_addressRole;
};

}

#endif // HAVE_QUICKDC_QUI_FAVOURITEDELEGATE_H

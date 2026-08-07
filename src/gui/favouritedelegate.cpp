/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "favouritedelegate.h"

#include "glyphs.h"
#include "theme.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace {

constexpr int GLYPH = 14;

/** Air on each side of the glyph, so the target is bigger than the drawing. */
constexpr int MARGIN = 5;

/**
 * The star, kept or not.
 *
 * Cached against the theme's own name, as the presence figures are: a list asks for
 * this on every row of every repaint, and a pixmap made before a scheme changed
 * would keep the old ink.
 */
QIcon star_icon(bool kept)
{
	constexpr qreal RATIO = 3.0;

	static QIcon stars[2];
	static QString painted;

	if (painted != Qdc::theme().name)
	{
		for (QIcon& star : stars) star = QIcon();
		painted = Qdc::theme().name;
	}

	QIcon& icon = stars[kept ? 1 : 0];

	if (icon.isNull())
	{
		const QColor ink = kept ? Qdc::theme().star : Qdc::theme().contentMuted;

		const QPixmap pixmap = Qdc::glyph_icon(
			kept ? Qdc::Glyph::Star : Qdc::Glyph::StarOutline, ink, RATIO, GLYPH)
			.pixmap(QSize(GLYPH, GLYPH) * RATIO);

		/*
		 * Under Selected as well as Normal. The style draws an item's decoration in
		 * whichever mode the row is in, and left to itself it would grey the star
		 * out on the selected row - the one being looked at.
		 */
		icon.addPixmap(pixmap, QIcon::Normal);
		icon.addPixmap(pixmap, QIcon::Selected);
	}

	return icon;
}

}

Qdc::FavouriteDelegate::FavouriteDelegate(Placement placement, int favouriteRole,
                                          int addressRole, QObject* parent)
	: QStyledItemDelegate(parent)
	, m_placement(placement)
	, m_favouriteRole(favouriteRole)
	, m_addressRole(addressRole)
{
}

int Qdc::FavouriteDelegate::columnWidth()
{
	return GLYPH + 2 * MARGIN;
}

QStyleOptionViewItem Qdc::FavouriteDelegate::starred(
	const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	QStyleOptionViewItem shown = option;
	initStyleOption(&shown, index);

	/*
	 * A row with no address is the note explaining an empty list rather than a hub;
	 * there is nothing to keep, so it gets no star and no place to press.
	 */
	if (index.data(m_addressRole).toString().isEmpty()) return shown;

	shown.icon = star_icon(index.data(m_favouriteRole).toBool());
	shown.decorationSize = QSize(GLYPH, GLYPH);
	shown.decorationPosition = QStyleOptionViewItem::Left;
	shown.features |= QStyleOptionViewItem::HasDecoration;

	shown.decorationAlignment = m_placement == OwnColumn
		? Qt::AlignCenter : (Qt::AlignLeft | Qt::AlignVCenter);

	return shown;
}

QRect Qdc::FavouriteDelegate::starBox(const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const
{
	const QStyleOptionViewItem shown = starred(option, index);
	if (!(shown.features & QStyleOptionViewItem::HasDecoration)) return QRect();

	/*
	 * A column of its own is all star, and a 14 pixel target in a cell three times
	 * that is unkind. In a row that also carries text, only where the style actually
	 * put the decoration - asked of the style rather than worked out, so the two
	 * cannot drift apart - widened by the margin on each side.
	 */
	if (m_placement == OwnColumn) return shown.rect;

	const QStyle* style = shown.widget ? shown.widget->style()
	                                   : QApplication::style();

	const QRect drawn = style->subElementRect(QStyle::SE_ItemViewItemDecoration,
	                                          &shown, shown.widget);

	return drawn.adjusted(-MARGIN, 0, MARGIN, 0);
}

void Qdc::FavouriteDelegate::paint(QPainter* painter,
                                   const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const
{
	const QStyleOptionViewItem shown = starred(option, index);

	const QStyle* style = shown.widget ? shown.widget->style()
	                                   : QApplication::style();

	style->drawControl(QStyle::CE_ItemViewItem, &shown, painter, shown.widget);
}

bool Qdc::FavouriteDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                         const QStyleOptionViewItem& option,
                                         const QModelIndex& index)
{
	const QString address = index.data(m_addressRole).toString();

	if (!address.isEmpty() && event->type() == QEvent::KeyPress
	    && m_placement == OwnColumn)
	{
		/*
		 * Only where the star has a cell that can hold the focus. In a list with one
		 * column the space bar already means something - it changes the selection -
		 * and taking that away would be worse than having no key for this.
		 */
		const int key = static_cast<QKeyEvent*>(event)->key();

		if (key == Qt::Key_Space || key == Qt::Key_Select)
		{
			emit toggled(address);
			return true;
		}
	}

	if (address.isEmpty()
	    || (event->type() != QEvent::MouseButtonRelease
	        && event->type() != QEvent::MouseButtonDblClick))
	{
		return QStyledItemDelegate::editorEvent(event, model, option, index);
	}

	QMouseEvent* mouse = static_cast<QMouseEvent*>(event);

	if (mouse->button() != Qt::LeftButton
	    || !starBox(option, index).contains(mouse->position().toPoint()))
	{
		return QStyledItemDelegate::editorEvent(event, model, option, index);
	}

	/*
	 * The second press of a double click is swallowed rather than acted on: without
	 * that, tapping a star twice would put the hub back as it was and open it as
	 * well. The first press is deliberately left alone, so pressing a star also
	 * selects its row and the buttons underneath agree about which hub that was.
	 */
	if (event->type() == QEvent::MouseButtonRelease) emit toggled(address);

	return true;
}

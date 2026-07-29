/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "progressdelegate.h"

#include <QApplication>
#include <QPainter>
#include <QStyle>

namespace {

constexpr int MARGIN_X = 4;
constexpr int MARGIN_Y = 4;
constexpr qreal RADIUS = 3.0;
constexpr int MINIMUM_HEIGHT = 20;

}

Qdc::ProgressDelegate::ProgressDelegate(int fractionRole, QObject* parent)
	: QStyledItemDelegate(parent)
	, m_fractionRole(fractionRole)
{
}

QSize Qdc::ProgressDelegate::sizeHint(const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const
{
	QSize size = QStyledItemDelegate::sizeHint(option, index);
	size.setHeight(qMax(size.height(), MINIMUM_HEIGHT));
	return size;
}

void Qdc::ProgressDelegate::paint(QPainter* painter,
                                 const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const
{
	QStyleOptionViewItem adjusted = option;
	initStyleOption(&adjusted, index);

	/* The style draws the row's background and selection; the bar is ours. */
	const QString text = adjusted.text;
	adjusted.text.clear();

	QStyle* style = adjusted.widget ? adjusted.widget->style()
	                                : QApplication::style();
	style->drawControl(QStyle::CE_ItemViewItem, &adjusted, painter,
	                   adjusted.widget);

	const QVariant value = index.data(m_fractionRole);
	if (!value.isValid()) return;

	const double done = qBound(0.0, value.toDouble(), 1.0);

	const QRectF track = QRectF(option.rect).adjusted(MARGIN_X, MARGIN_Y,
	                                                  -MARGIN_X, -MARGIN_Y);
	if (track.width() <= 0.0 || track.height() <= 0.0) return;

	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);

	const QPalette& colours = option.palette;
	const bool selected = (option.state & QStyle::State_Selected);

	/*
	 * On a selected row the track has to sit against the highlight rather than
	 * against the base, or it disappears into it.
	 */
	QColor trackColour = colours.color(selected ? QPalette::Highlight
	                                            : QPalette::AlternateBase);
	trackColour = selected ? trackColour.lighter(115) : trackColour.darker(108);

	painter->setPen(Qt::NoPen);
	painter->setBrush(trackColour);
	painter->drawRoundedRect(track, RADIUS, RADIUS);

	if (done > 0.0)
	{
		QRectF filled = track;
		filled.setWidth(track.width() * done);

		/*
		 * Never narrower than its own corner radius, so a barely started transfer
		 * shows a sliver rather than a rounded rectangle wider than the progress
		 * it represents.
		 */
		filled.setWidth(qMax(filled.width(), 2.0 * RADIUS));

		painter->setBrush(colours.color(selected ? QPalette::HighlightedText
		                                         : QPalette::Highlight));
		painter->drawRoundedRect(filled, RADIUS, RADIUS);
	}

	/* The percentage over the bar, in whichever colour reads on this row. */
	painter->setPen(colours.color(selected ? QPalette::HighlightedText
	                                       : QPalette::Text));

	QFont small = option.font;
	small.setPointSize(qMax(1, small.pointSize() - 2));
	painter->setFont(small);
	painter->drawText(option.rect, Qt::AlignCenter, text);

	painter->restore();
}

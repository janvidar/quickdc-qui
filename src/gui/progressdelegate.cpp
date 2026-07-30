/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "progressdelegate.h"

#include "theme.h"

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

	/*
	 * The same colours whether or not the row is selected: the selection is a pale
	 * tint rather than a strong fill, so the bar reads against it unchanged. It had
	 * to be inverted back when selection was a dark colour.
	 */
	painter->setPen(Qt::NoPen);
	painter->setBrush(theme().border);
	painter->drawRoundedRect(track, RADIUS, RADIUS);

	QRectF filled;

	if (done > 0.0)
	{
		filled = track;
		filled.setWidth(track.width() * done);

		/*
		 * Never narrower than its own corner radius, so a barely started transfer
		 * shows a sliver rather than a rounded rectangle wider than the progress
		 * it represents.
		 */
		filled.setWidth(qMax(filled.width(), 2.0 * RADIUS));

		painter->setBrush(theme().accent);
		painter->drawRoundedRect(filled, RADIUS, RADIUS);
	}

	QFont small = option.font;
	small.setPointSize(qMax(1, small.pointSize() - 2));
	painter->setFont(small);

	/*
	 * The percentage is drawn twice, clipped to either side of the fill: once in
	 * the colour that reads on the filled part and once in the colour that reads
	 * on the track. A single pass cannot do it - wherever the text crosses the
	 * edge of the bar, one half of it disappears into the background.
	 */
	const QRect whole = option.rect;
	const QRect filledPart = filled.isEmpty() ? QRect() : filled.toAlignedRect();

	if (!filledPart.isEmpty())
	{
		painter->save();
		painter->setClipRect(filledPart);
		painter->setPen(theme().accentText);
		painter->drawText(whole, Qt::AlignCenter, text);
		painter->restore();
	}

	painter->save();
	painter->setClipRegion(QRegion(whole).subtracted(QRegion(filledPart)));
	painter->setPen(theme().contentText);
	painter->drawText(whole, Qt::AlignCenter, text);
	painter->restore();

	painter->restore();
}

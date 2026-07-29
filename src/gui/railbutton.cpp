/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "railbutton.h"

#include "glyphs.h"

#include <QEnterEvent>
#include <QFontMetricsF>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>

namespace {

constexpr int WIDTH = 68;
constexpr int HEIGHT = 60;
constexpr qreal GLYPH_SIZE = 24.0;
constexpr qreal GLYPH_TOP = 8.0;
constexpr qreal CAPTION_HEIGHT = 14.0;
constexpr qreal SELECTION_RADIUS = 8.0;

/** The caption is smaller than the body font; how much smaller. */
constexpr int CAPTION_POINT_DROP = 2;

QString badge_text(int count)
{
	return (count > 99) ? QStringLiteral("99+") : QString::number(count);
}

}

Qdc::RailButton::RailButton(Glyph glyph, const QString& caption, QWidget* parent)
	: QAbstractButton(parent)
	, m_glyph(glyph)
{
	setText(caption);
	setCheckable(true);
	setFocusPolicy(Qt::TabFocus);
	setCursor(Qt::PointingHandCursor);
	setToolTip(caption);
}

void Qdc::RailButton::setBadgeCount(int count)
{
	const int clamped = (count > 0) ? count : 0;
	if (clamped == m_badgeCount) return;

	m_badgeCount = clamped;

	/*
	 * Announced to assistive technology as well as repainted: a badge that only
	 * exists as pixels is invisible to anyone using a screen reader.
	 */
	setAccessibleName(m_badgeCount > 0
		? tr("%1, %2 waiting").arg(text()).arg(m_badgeCount)
		: text());

	update();
}

int Qdc::RailButton::badgeCount() const
{
	return m_badgeCount;
}

QSize Qdc::RailButton::sizeHint() const
{
	return QSize(WIDTH, HEIGHT);
}

QSize Qdc::RailButton::minimumSizeHint() const
{
	return sizeHint();
}

QRectF Qdc::RailButton::badgeRect(const QRectF& glyphBox, const QString& text) const
{
	QFont badgeFont = font();
	badgeFont.setPointSize(qMax(1, badgeFont.pointSize() - CAPTION_POINT_DROP));
	badgeFont.setBold(true);

	const QFontMetricsF metrics(badgeFont);
	const qreal height = metrics.height() + 2.0;

	/* Round while there is one digit, stretching into a pill for more. */
	const qreal width = qMax(height, metrics.horizontalAdvance(text) + 8.0);

	/*
	 * Pulled back inside the glyph box by a third of the badge, so it overlaps
	 * the corner instead of floating clear of it - and so a wide badge grows
	 * inwards and cannot leave the button.
	 */
	return QRectF(glyphBox.right() - width / 3.0,
	              glyphBox.top() - height / 3.0,
	              width, height);
}

void Qdc::RailButton::enterEvent(QEnterEvent* event)
{
	QAbstractButton::enterEvent(event);
	update();
}

void Qdc::RailButton::leaveEvent(QEvent* event)
{
	QAbstractButton::leaveEvent(event);
	update();
}

void Qdc::RailButton::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);

	const QPalette& colours = palette();
	const bool selected = isChecked();

	/*
	 * Highlight behind the whole button when selected, and a lighter wash on
	 * hover. Both are drawn from the palette so the rail follows the desktop
	 * into dark mode without a stylesheet.
	 */
	if (selected)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(colours.color(QPalette::Highlight));
		painter.drawRoundedRect(rect().adjusted(4, 2, -4, -2),
		                        SELECTION_RADIUS, SELECTION_RADIUS);
	}
	else if (underMouse() && isEnabled())
	{
		QColor wash = colours.color(QPalette::Highlight);
		wash.setAlpha(40);
		painter.setPen(Qt::NoPen);
		painter.setBrush(wash);
		painter.drawRoundedRect(rect().adjusted(4, 2, -4, -2),
		                        SELECTION_RADIUS, SELECTION_RADIUS);
	}

	QColor ink = colours.color(selected ? QPalette::HighlightedText
	                                    : QPalette::WindowText);
	if (!isEnabled()) ink.setAlpha(90);

	const QRectF glyphBox((width() - GLYPH_SIZE) / 2.0, GLYPH_TOP,
	                      GLYPH_SIZE, GLYPH_SIZE);
	paint_glyph(painter, m_glyph, glyphBox, ink);

	QFont captionFont = font();
	captionFont.setPointSize(qMax(1, captionFont.pointSize() - CAPTION_POINT_DROP));
	painter.setFont(captionFont);
	painter.setPen(ink);

	const QRectF captionBox(0.0, glyphBox.bottom() + 3.0,
	                        width(), CAPTION_HEIGHT);
	painter.drawText(captionBox, Qt::AlignHCenter | Qt::AlignTop,
	                 QFontMetricsF(captionFont).elidedText(text(), Qt::ElideRight,
	                                                       width() - 6.0));

	if (m_badgeCount <= 0) return;

	const QString badge = badge_text(m_badgeCount);
	const QRectF box = badgeRect(glyphBox, badge);

	/*
	 * Outlined in the rail's own background so the badge reads as sitting above
	 * the glyph rather than merging into it, which matters most when the button
	 * is selected and both are strong colours.
	 */
	painter.setPen(QPen(colours.color(QPalette::Window), 2.0));
	painter.setBrush(QColor(0xd6, 0x3c, 0x3c));
	painter.drawRoundedRect(box, box.height() / 2.0, box.height() / 2.0);

	QFont badgeFont = captionFont;
	badgeFont.setBold(true);
	painter.setFont(badgeFont);
	painter.setPen(Qt::white);
	painter.drawText(box, Qt::AlignCenter, badge);
}

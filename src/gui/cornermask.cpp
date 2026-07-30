/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "cornermask.h"

#include "theme.h"

#include <QPainter>
#include <QPainterPath>

Qdc::CornerMask::CornerMask(QWidget* parent)
	: QWidget(parent)
{
	/*
	 * Nothing here can be clicked and nothing fills its background: the corners are
	 * all that is ever drawn, and everything else has to stay visible through it.
	 */
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
	setFocusPolicy(Qt::NoFocus);
}

void Qdc::CornerMask::cover(const QRect& area)
{
	setGeometry(area);
	raise();
}

void Qdc::CornerMask::paintEvent(QPaintEvent*)
{
	const qreal radius = theme().radiusLarge;

	QPainterPath square;
	square.addRect(QRectF(rect()));

	QPainterPath rounded;
	rounded.addRoundedRect(QRectF(rect()), radius, radius);

	/*
	 * Antialiased over whatever is already on screen. The widget clears nothing, so
	 * the curve blends into the content beneath it rather than into a fill of its
	 * own, which is what keeps the edge smooth instead of stepped.
	 */
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(Qt::NoPen);
	painter.setBrush(theme().frame);
	painter.drawPath(square.subtracted(rounded));
}

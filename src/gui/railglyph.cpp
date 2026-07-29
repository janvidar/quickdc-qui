/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "railglyph.h"

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QTransform>

namespace {

/** The grid the paths below are drawn on. */
constexpr qreal GRID = 24.0;

/** A house: a roof, and a body with a doorway cut out of it. */
QPainterPath home_path()
{
	QPainterPath path;

	path.moveTo(12.0, 3.0);
	path.lineTo(22.0, 11.5);
	path.lineTo(19.5, 11.5);
	path.lineTo(19.5, 21.0);
	path.lineTo(14.5, 21.0);
	path.lineTo(14.5, 14.0);
	path.lineTo(9.5, 14.0);
	path.lineTo(9.5, 21.0);
	path.lineTo(4.5, 21.0);
	path.lineTo(4.5, 11.5);
	path.lineTo(2.0, 11.5);
	path.closeSubpath();

	return path;
}

/**
 * Two speech bubbles, one behind the other: a hub is a conversation, and the
 * second bubble says there is more than one of them.
 *
 * Only the front bubble carries a tail. Two tails at this size read as noise
 * rather than as two speakers.
 *
 * The bubbles overlap, so the back one has a gap cut out of it where the front
 * one passes over. Without that they merge into a single blob at 24 pixels,
 * which is the size this is actually drawn at - two shapes are only legible
 * here if something separates them.
 */
QPainterPath hubs_path()
{
	QPainterPath back;
	back.addRoundedRect(QRectF(2.0, 2.5, 13.0, 9.0), 3.0, 3.0);

	QPainterPath front;
	front.addRoundedRect(QRectF(9.0, 10.0, 13.0, 9.0), 3.0, 3.0);
	/*
	 * The base is set inside the bubble rather than on its edge, so the tail
	 * joins it without a seam where the rounded corner curves away.
	 */
	front.moveTo(11.5, 17.5);
	front.lineTo(10.5, 23.0);
	front.lineTo(16.5, 17.5);
	front.closeSubpath();

	/*
	 * The gap is the front bubble grown by half the stroke width on each side.
	 * Stroking and uniting rather than scaling, because scaling a path about its
	 * centre moves its edges by different amounts depending how far they are
	 * from that centre, and the gap has to be even all the way round.
	 */
	QPainterPathStroker stroker;
	stroker.setWidth(3.0);

	const QPainterPath halo = stroker.createStroke(front).united(front);

	QPainterPath path;
	path.addPath(back.subtracted(halo).united(front));

	return path;
}

/** An arrow down and an arrow up, the two directions a transfer can go. */
QPainterPath transfers_path()
{
	QPainterPath path;

	path.moveTo(7.0, 3.0);
	path.lineTo(9.5, 3.0);
	path.lineTo(9.5, 15.5);
	path.lineTo(13.0, 15.5);
	path.lineTo(8.25, 21.0);
	path.lineTo(3.5, 15.5);
	path.lineTo(7.0, 15.5);
	path.closeSubpath();

	path.moveTo(14.5, 21.0);
	path.lineTo(17.0, 21.0);
	path.lineTo(17.0, 8.5);
	path.lineTo(20.5, 8.5);
	path.lineTo(15.75, 3.0);
	path.lineTo(11.0, 8.5);
	path.lineTo(14.5, 8.5);
	path.closeSubpath();

	return path;
}

/**
 * A gear: a ring of teeth around a hub, with the centre cut out.
 *
 * The hole is a subpath wound inside the body and filled odd-even, which is why
 * the caller must not switch to a winding fill.
 */
QPainterPath settings_path()
{
	constexpr int TEETH = 8;
	constexpr qreal CENTRE = 12.0;

	QPainterPath body;
	body.addEllipse(QPointF(CENTRE, CENTRE), 7.0, 7.0);

	for (int n = 0; n < TEETH; ++n)
	{
		QPainterPath tooth;
		tooth.addRoundedRect(QRectF(CENTRE - 1.9, 1.6, 3.8, 6.0), 1.0, 1.0);

		QTransform rotation;
		rotation.translate(CENTRE, CENTRE);
		rotation.rotate(n * (360.0 / TEETH));
		rotation.translate(-CENTRE, -CENTRE);

		body = body.united(rotation.map(tooth));
	}

	QPainterPath hole;
	hole.addEllipse(QPointF(CENTRE, CENTRE), 3.0, 3.0);

	QPainterPath path;
	path.addPath(body);
	path.addPath(hole);
	path.setFillRule(Qt::OddEvenFill);

	return path;
}

QPainterPath glyph_path(Qdc::View::Glyph glyph)
{
	switch (glyph)
	{
		case Qdc::View::Glyph::Home:      return home_path();
		case Qdc::View::Glyph::Hubs:      return hubs_path();
		case Qdc::View::Glyph::Transfers: return transfers_path();
		case Qdc::View::Glyph::Settings:  return settings_path();
	}

	return QPainterPath();
}

}

void Qdc::paint_glyph(QPainter& painter, View::Glyph glyph, const QRectF& box,
                      const QColor& colour)
{
	const QPainterPath path = glyph_path(glyph);
	if (path.isEmpty()) return;

	/* Uniform, and centred on the shorter side, so nothing is stretched. */
	const qreal scale = qMin(box.width(), box.height()) / GRID;

	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.translate(box.center());
	painter.scale(scale, scale);
	painter.translate(-GRID / 2.0, -GRID / 2.0);

	painter.setPen(Qt::NoPen);
	painter.setBrush(colour);
	painter.drawPath(path);

	painter.restore();
}

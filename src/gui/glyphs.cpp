/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "glyphs.h"

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

/**
 * A five pointed star, filled.
 *
 * Points are stepped round the circle at 36 degree intervals, alternating
 * between the outer and inner radius, which is what makes the arms even. Starting
 * at -90 degrees puts a point at the top rather than an arm to the right.
 */
QPainterPath star_path()
{
	constexpr qreal CENTRE = 12.0;
	constexpr qreal OUTER = 9.4;
	constexpr qreal INNER = 3.9;
	constexpr int POINTS = 5;

	QPainterPath path;

	for (int n = 0; n < POINTS * 2; ++n)
	{
		const qreal radius = (n % 2 == 0) ? OUTER : INNER;
		const qreal angle = qDegreesToRadians(-90.0 + n * 36.0);

		const QPointF point(CENTRE + radius * qCos(angle),
		                    CENTRE + radius * qSin(angle));

		if (n == 0)
			path.moveTo(point);
		else
			path.lineTo(point);
	}

	path.closeSubpath();
	return path;
}

/**
 * The same star as an outline.
 *
 * Stroked rather than drawn as two nested stars, because the gap between an outer
 * and an inner star of the same shape is not of even width - the arms taper, so
 * scaling one down moves its edges by different amounts.
 */
QPainterPath star_outline_path()
{
	QPainterPathStroker stroker;
	stroker.setWidth(2.0);
	stroker.setJoinStyle(Qt::MiterJoin);

	return stroker.createStroke(star_path());
}

/**
 * A padlock. 'closed' puts both legs of the shackle into the body; open lifts it
 * clear on one side.
 */
QPainterPath lock_path(bool closed)
{
	constexpr qreal BODY_TOP = 11.5;

	QPainterPath body;
	body.addRoundedRect(QRectF(5.0, BODY_TOP, 14.0, 9.5), 2.0, 2.0);

	/* The shackle is a stroked semicircle, offset sideways when open. */
	const qreal shift = closed ? 0.0 : 3.5;
	const QRectF arc(8.0 + shift, 3.5, 8.0, 8.0);

	QPainterPath shackle;
	shackle.moveTo(arc.right(), closed ? BODY_TOP + 1.0 : arc.center().y());
	shackle.lineTo(arc.right(), arc.center().y());
	shackle.arcTo(arc, 0.0, 180.0);
	shackle.lineTo(arc.left(), BODY_TOP + 1.0);

	QPainterPathStroker stroker;
	stroker.setWidth(2.1);
	stroker.setCapStyle(Qt::RoundCap);

	QPainterPath keyhole;
	keyhole.addEllipse(QPointF(12.0, 16.2), 1.6, 1.6);

	QPainterPath path;
	path.addPath(body.united(stroker.createStroke(shackle)));
	path.addPath(keyhole);
	path.setFillRule(Qt::OddEvenFill);

	return path;
}

/** Two figures, for the button that reveals the user list. */
QPainterPath people_path()
{
	QPainterPath path;

	QPainterPath back;
	back.addEllipse(QPointF(15.8, 8.6), 3.1, 3.1);
	back.addRoundedRect(QRectF(11.6, 13.0, 8.4, 6.6), 3.0, 3.0);

	/* Cut a gap where the front figure crosses, as with the hubs bubbles. */
	QPainterPath front;
	front.addEllipse(QPointF(9.4, 7.9), 3.8, 3.8);
	front.addRoundedRect(QRectF(4.2, 13.0, 10.4, 7.4), 3.4, 3.4);

	QPainterPathStroker stroker;
	stroker.setWidth(2.6);

	path.addPath(back.subtracted(stroker.createStroke(front).united(front))
		.united(front));

	return path;
}

/** A cross, for leaving a conversation. */
QPainterPath close_path()
{
	QPainterPath strokes;
	strokes.moveTo(6.5, 6.5);
	strokes.lineTo(17.5, 17.5);
	strokes.moveTo(17.5, 6.5);
	strokes.lineTo(6.5, 17.5);

	QPainterPathStroker stroker;
	stroker.setWidth(2.4);
	stroker.setCapStyle(Qt::RoundCap);

	return stroker.createStroke(strokes);
}

QPainterPath glyph_path(Qdc::Glyph glyph)
{
	switch (glyph)
	{
		case Qdc::Glyph::Home:        return home_path();
		case Qdc::Glyph::Hubs:        return hubs_path();
		case Qdc::Glyph::Transfers:   return transfers_path();
		case Qdc::Glyph::Settings:    return settings_path();
		case Qdc::Glyph::Star:        return star_path();
		case Qdc::Glyph::StarOutline: return star_outline_path();
		case Qdc::Glyph::Lock:        return lock_path(true);
		case Qdc::Glyph::LockOpen:    return lock_path(false);
		case Qdc::Glyph::People:      return people_path();
		case Qdc::Glyph::Close:       return close_path();
	}

	return QPainterPath();
}

}

void Qdc::paint_glyph(QPainter& painter, Glyph glyph, const QRectF& box,
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

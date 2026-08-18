/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "glyphs.h"

#include "theme.h"

#include <QColor>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRectF>
#include <QTransform>

namespace {

/**
 * 'ink' moved 'percent' of the way towards 'muted'.
 *
 * A blend rather than the same colour at a lower alpha, which is what this was
 * first: alpha takes its contrast from whatever is behind it, and a pale figure
 * faded against a white user list came out almost invisible while the same fade
 * over the dark scheme was fine. Mixing keeps the ink opaque, so what changes is
 * the colour rather than how much of the background shows through it.
 */
QColor faded_toward(const QColor& ink, const QColor& muted, int percent)
{
	const int keep = 100 - percent;

	return QColor((ink.red()   * keep + muted.red()   * percent) / 100,
	              (ink.green() * keep + muted.green() * percent) / 100,
	              (ink.blue()  * keep + muted.blue()  * percent) / 100);
}

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

/**
 * A magnifier: a ring, and a handle running down from its lower right.
 *
 * The handle starts inside the ring rather than at its edge, so that the join is
 * covered by the ring's own stroke. Butted against the outside instead, the two
 * strokes meet at a tangent and leave a notch at small sizes.
 */
QPainterPath search_path()
{
	constexpr qreal RADIUS = 6.1;
	const QPointF centre(10.3, 10.3);

	QPainterPath ring;
	ring.addEllipse(centre, RADIUS, RADIUS);

	QPainterPathStroker hoop;
	hoop.setWidth(2.2);

	/*
	 * Down the diagonal, which is where the ring's own centre line points, so the
	 * start is one component of a 45 degree offset in from the rim.
	 */
	constexpr qreal DIAGONAL = 0.70710678;
	const qreal inset = (RADIUS - 1.0) * DIAGONAL;

	QPainterPath handle;
	handle.moveTo(centre.x() + inset, centre.y() + inset);
	handle.lineTo(19.3, 19.3);

	QPainterPathStroker grip;
	grip.setWidth(2.5);
	grip.setCapStyle(Qt::RoundCap);

	QPainterPath path;
	path.addPath(hoop.createStroke(ring).united(grip.createStroke(handle)));

	return path;
}

/**
 * One figure: a head, and shoulders under it.
 *
 * The same construction as the front figure of people_path(), which is what makes a
 * row in the user list and the button that reveals it read as the same thing.
 */
QPainterPath person_path()
{
	QPainterPath path;
	path.addEllipse(QPointF(12.0, 8.0), 3.9, 3.9);
	path.addRoundedRect(QRectF(5.4, 13.4, 13.2, 7.4), 3.6, 3.6);

	return path;
}

/**
 * The same figure as an outline.
 *
 * Stroked from the filled shape rather than drawn as a smaller figure inside a
 * larger one, for the reason given at star_outline_path(): scaling a shape down
 * moves its edges by different amounts and the gap comes out uneven.
 */
QPainterPath person_outline_path()
{
	QPainterPathStroker stroker;
	stroker.setWidth(1.7);
	stroker.setJoinStyle(Qt::RoundJoin);

	return stroker.createStroke(person_path());
}

/**
 * A notice with a pin through the top of it, for a bulletin board.
 *
 * The head of the pin stands above the sheet and overlaps it, so the two read as one
 * object pinned up rather than as a disc resting on a box. Two lines of writing, the
 * second of them short, because a single line reads as a label and two say prose.
 *
 * The writing is cut out of the sheet rather than drawn over it, so the glyph is one
 * shape in one ink like its neighbours. That is a subpath filled odd-even, as with
 * the gear's centre, which is why the caller must not switch to a winding fill.
 */
QPainterPath board_path()
{
	QPainterPath sheet;
	sheet.addRoundedRect(QRectF(4.0, 5.0, 16.0, 16.0), 2.5, 2.5);

	QPainterPath pin;
	pin.addEllipse(QPointF(12.0, 3.6), 2.3, 2.3);

	QPainterPath writing;
	writing.addRoundedRect(QRectF(7.5, 11.0, 9.0, 2.0), 1.0, 1.0);
	writing.addRoundedRect(QRectF(7.5, 15.2, 6.0, 2.0), 1.0, 1.0);

	QPainterPath path;
	path.addPath(sheet.united(pin));
	path.addPath(writing);
	path.setFillRule(Qt::OddEvenFill);

	return path;
}

/**
 * Two units stacked with a light on each, for the hub this program runs itself.
 *
 * A machine, because the row it marks is the only hub in the list that is not
 * somewhere else, and at fifteen pixels beside a speech bubble that has to be a
 * different shape rather than the same shape with a mark on it.
 *
 * The lights are cut out of the units rather than drawn over them, so the glyph is
 * one shape in one ink like its neighbours. That is a subpath filled odd-even, as
 * with the gear's centre, which is why the caller must not switch to a winding fill.
 */
QPainterPath local_hub_path()
{
	QPainterPath units;
	units.addRoundedRect(QRectF(3.0, 4.5, 18.0, 7.0), 2.0, 2.0);
	units.addRoundedRect(QRectF(3.0, 13.0, 18.0, 7.0), 2.0, 2.0);

	QPainterPath lights;
	lights.addEllipse(QPointF(6.75, 8.0), 1.15, 1.15);
	lights.addEllipse(QPointF(6.75, 16.5), 1.15, 1.15);

	QPainterPath path;
	path.addPath(units);
	path.addPath(lights);
	path.setFillRule(Qt::OddEvenFill);

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

/** A tick, drawn on the same weight as the cross it is answered against. */
QPainterPath check_path()
{
	QPainterPath strokes;
	strokes.moveTo(5.5, 12.5);
	strokes.lineTo(10.0, 17.0);
	strokes.lineTo(18.5, 7.5);

	QPainterPathStroker stroker;
	stroker.setWidth(2.4);
	stroker.setCapStyle(Qt::RoundCap);
	stroker.setJoinStyle(Qt::RoundJoin);

	return stroker.createStroke(strokes);
}

/**
 * A dash, for an answer nobody has.
 *
 * Shorter than the width of the tick and the cross so that it reads as less
 * rather than as a third thing of equal weight, and centred on the same line
 * they cross at so a column of the three does not look ragged.
 */
QPainterPath dash_path()
{
	QPainterPath strokes;
	strokes.moveTo(7.0, 12.0);
	strokes.lineTo(17.0, 12.0);

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
		case Qdc::Glyph::Search:      return search_path();
		case Qdc::Glyph::Transfers:   return transfers_path();
		case Qdc::Glyph::Settings:    return settings_path();
		case Qdc::Glyph::Star:        return star_path();
		case Qdc::Glyph::StarOutline: return star_outline_path();
		case Qdc::Glyph::Lock:        return lock_path(true);
		case Qdc::Glyph::LockOpen:    return lock_path(false);
		case Qdc::Glyph::People:      return people_path();
		case Qdc::Glyph::Close:       return close_path();
		case Qdc::Glyph::Board:       return board_path();
		case Qdc::Glyph::LocalHub:    return local_hub_path();
		case Qdc::Glyph::Person:      return person_path();
		case Qdc::Glyph::PersonOutline: return person_outline_path();
		case Qdc::Glyph::Check:       return check_path();
		case Qdc::Glyph::Dash:        return dash_path();
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

QIcon Qdc::glyph_icon(Glyph glyph, const QColor& colour, qreal ratio, int size)
{
	QPixmap pixmap(QSize(size, size) * ratio);
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(Qt::transparent);

	QPainter painter(&pixmap);
	paint_glyph(painter, glyph, QRectF(0, 0, size, size), colour);
	painter.end();

	return QIcon(pixmap);
}

/**
 * A figure for one user, saying two things at once.
 *
 * The shape is whether they are here: filled for present, an outline for somebody who
 * says they are away. The colour is what they are: the operator colour for an
 * operator, the presence colour for everybody else.
 *
 * Two attributes, two ways of showing them, so neither hides the other. Colour alone
 * would leave the two states of an operator indistinguishable to somebody who cannot
 * tell the inks apart, and shape alone would lose the operator.
 *
 * Which is why an away user keeps their own colour, dimmed, rather than going a
 * neutral grey: grey would say "away" a second time and stop saying who they are, and
 * an operator is worth knowing about whether or not they are reading the chat.
 *
 * Built rather than shipped as an asset so it follows the theme, and cached because a
 * user list asks for it on every row of every repaint. Cached against the theme's own
 * name as well, since the colours can change while a list is open and a pixmap made
 * before that would keep the old ink.
 */
QIcon Qdc::presence_icon(bool away, bool op, int size)
{
	constexpr qreal RATIO = 3.0;

	/*
	 * Keyed by size as well as by what it says, because the same figure is wanted
	 * at two sizes - a row of the user list, and the strip above a conversation.
	 */
	static QHash<int, QIcon> figures;
	static QString painted;

	if (painted != Qdc::theme().name)
	{
		figures.clear();
		painted = Qdc::theme().name;
	}

	QIcon& icon = figures[(size * 4) + (op ? 2 : 0) + (away ? 1 : 0)];

	if (icon.isNull())
	{
		QPixmap pixmap(QSize(size, size) * RATIO);
		pixmap.setDevicePixelRatio(RATIO);
		pixmap.fill(Qt::transparent);

		/*
		 * Faded towards the muted colour rather than replaced by it: enough to
		 * recede next to a filled figure, and not so far that an away operator stops
		 * being tellable from anybody else who has stepped out.
		 */
		constexpr int AWAY_MIX = 45;

		QColor ink = op ? Qdc::theme().presenceOperator : Qdc::theme().presence;

		if (away) ink = faded_toward(ink, Qdc::theme().contentMuted, AWAY_MIX);

		QPainter painter(&pixmap);

		paint_glyph(painter, away ? Glyph::PersonOutline : Glyph::Person,
		            QRectF(0, 0, size, size), ink);

		painter.end();

		icon = QIcon(pixmap);
	}

	return icon;
}

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_GLYPHS_H
#define HAVE_QUICKDC_QUI_GLYPHS_H

#include <QIcon>

class QColor;
class QPainter;
class QRectF;

namespace Qdc {

/**
 * The icons this application draws.
 *
 * Painted rather than loaded, and there are two reasons that is worth the code.
 * There is no icon theme to ask on macOS, so a themed icon would have to ship as
 * an asset anyway; and a glyph that takes its colour from the caller follows the
 * palette into dark mode and into the selected state with no second asset and no
 * reloading.
 */
enum class Glyph {
	/* Navigation rail destinations. */
	Home,
	Hubs,
	Search,
	Transfers,
	Settings,

	/* Conversation header. */
	Star,
	StarOutline,
	Lock,
	LockOpen,
	People,
	Close,

	/**
	 * A bulletin board, in the list of what is open on a hub.
	 *
	 * A notice with a pin through it, and filled rather than outlined: it is listed
	 * beside a hub's bubbles and a conversation's figures, and a lighter shape among
	 * those two would read as a row that cannot be opened.
	 */
	Board,

	/**
	 * The hub this program runs itself, in the list of what is open.
	 *
	 * A machine rather than the speech bubbles every other hub is drawn as. It is
	 * the one row in that list which is not somewhere else, and the difference has
	 * to survive being drawn at fifteen pixels next to a bubble - which a bubble
	 * with something added to it would not.
	 */
	LocalHub,

	/**
	 * One user, in a list of them.
	 *
	 * Two forms rather than two colours, as with the star: filled for somebody who
	 * is here, an outline for somebody who says they are away. A difference in shape
	 * is one that survives being looked at by somebody who cannot tell the two inks
	 * apart.
	 */
	Person,
	PersonOutline,

	/**
	 * Something measured and found to work, in a grid of such answers.
	 *
	 * Paired with Close for the opposite answer and Dash for one nobody
	 * established, which is three shapes for three states rather than three inks:
	 * whether a port can be reached is not a thing to leave resting on telling
	 * green from red.
	 */
	Check,

	/**
	 * Nothing measured, in that same grid.
	 *
	 * Not a weaker cross. A test the server does not offer, or a family with no
	 * route to run one over, is a question nobody asked - and drawing it as a
	 * failure would have somebody set about fixing a network that is fine.
	 */
	Dash
};

/**
 * Draw 'glyph' into 'box', in 'colour'.
 *
 * The paths are laid out on a nominal 24 by 24 grid and scaled uniformly to fit
 * the shorter side of 'box', so a caller may ask for any size.
 */
void paint_glyph(QPainter& painter, Glyph glyph, const QRectF& box,
                 const QColor& colour);

/**
 * 'glyph' as an icon, at 'ratio' device pixels to the point.
 *
 * An icon rather than a painted button, so that the style keeps drawing the
 * pressed and hovered states of whatever shows it. The ratio has to be applied
 * by hand: a pixmap made at the logical size is blurred on a retina display.
 */
QIcon glyph_icon(Glyph glyph, const QColor& colour, qreal ratio, int size = 16);

/**
 * The figure for one user, saying two things at once.
 *
 * The shape is whether they are here: filled for present, an outline for somebody
 * who says they are away. The colour is what they are: the operator colour for an
 * operator, the presence colour for everybody else.
 *
 * Two attributes, two ways of showing them, so neither hides the other. Shared
 * rather than drawn twice, because the same figure names a row of the user list and
 * the person a conversation is with, and the two must not drift apart.
 */
QIcon presence_icon(bool away, bool op, int size = 13);

}

#endif // HAVE_QUICKDC_QUI_GLYPHS_H

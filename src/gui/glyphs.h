/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_GLYPHS_H
#define HAVE_QUICKDC_QUI_GLYPHS_H

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
	Transfers,
	Settings,

	/* Conversation header. */
	Star,
	StarOutline,
	Lock,
	LockOpen,
	People,
	Close
};

/**
 * Draw 'glyph' into 'box', in 'colour'.
 *
 * The paths are laid out on a nominal 24 by 24 grid and scaled uniformly to fit
 * the shorter side of 'box', so a caller may ask for any size.
 */
void paint_glyph(QPainter& painter, Glyph glyph, const QRectF& box,
                 const QColor& colour);

}

#endif // HAVE_QUICKDC_QUI_GLYPHS_H

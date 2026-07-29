/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_RAILGLYPH_H
#define HAVE_QUICKDC_QUI_RAILGLYPH_H

#include "view.h"

class QColor;
class QPainter;
class QRectF;

namespace Qdc {

/**
 * Draw a rail glyph into 'box', in 'colour'.
 *
 * Painted rather than loaded, and there are two reasons that is worth the code.
 * There is no icon theme to ask on macOS, so a themed icon would have to ship as
 * an asset anyway; and a glyph that takes its colour from the caller follows the
 * palette into dark mode and into the selected state without a second asset or
 * any reloading.
 *
 * The paths are laid out on a nominal 24 by 24 grid and scaled to 'box', so a
 * caller may pick any size.
 */
void paint_glyph(QPainter& painter, View::Glyph glyph, const QRectF& box,
                 const QColor& colour);

}

#endif // HAVE_QUICKDC_QUI_RAILGLYPH_H

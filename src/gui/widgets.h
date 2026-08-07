/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_WIDGETS_H
#define HAVE_QUICKDC_QUI_WIDGETS_H

class QLabel;
class QString;
class QWidget;

namespace Qdc {

/**
 * A line of explanation under whatever it explains.
 *
 * Wrapped, a point smaller than the text around it, and named 'mutedText' so the
 * stylesheet colours it rather than each caller copying the colour into a palette
 * of its own - a copy that cannot hear about a theme change and so survives one.
 */
QLabel* makeNote(const QString& text, QWidget* parent);

}

#endif // HAVE_QUICKDC_QUI_WIDGETS_H

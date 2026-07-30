/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_NATIVEWINDOW_H
#define HAVE_QUICKDC_QUI_NATIVEWINDOW_H

class QWidget;

namespace Qdc {

/**
 * Give @a window a title bar in the frame colour.
 *
 * The title bar belongs to the platform, not to Qt, so no palette or stylesheet
 * reaches it: left alone it is a system-coloured strip above a themed window,
 * which is the one seam the scheme cannot hide. There is an implementation for
 * macOS and a deliberate no-op everywhere else, where the title bar is drawn by a
 * window manager that an application has no business repainting.
 *
 * Safe to call before the window is shown - it does nothing until there is a
 * native window to talk to - and meant to be called again afterwards, on every
 * layout: Qt rewrites the window's style mask whenever its size constraints change
 * and undoes this while it is there, so it has to be put back. Cheap when there is
 * nothing to do.
 */
void styleTitleBar(QWidget* window);

}

#endif // HAVE_QUICKDC_QUI_NATIVEWINDOW_H

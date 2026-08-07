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

/**
 * Whether the title bar above a window of ours is one we paint.
 *
 * True on macOS, where styleTitleBar() colours it and the window's own title is
 * drawn in it, so the row reads as part of the window. False where the title bar
 * belongs to a window manager and is drawn to its theme rather than ours - and
 * there the content needs a band of frame colour under it, or it begins against
 * somebody else's decoration with nothing between.
 *
 * A property of the build rather than of the window, so it may be asked before
 * there is one.
 */
bool titleBarIsOurs();

}

#endif // HAVE_QUICKDC_QUI_NATIVEWINDOW_H

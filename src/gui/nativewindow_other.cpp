/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "nativewindow.h"

/*
 * Nothing to do. On X11 and Wayland the title bar is the window manager's, drawn
 * to its own theme and from another process, and an application that painted over
 * it would look wrong in every desktop but the one it was tried in.
 */
void Qdc::styleTitleBar(QWidget*)
{
}

bool Qdc::titleBarIsOurs()
{
	return false;
}

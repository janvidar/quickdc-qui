/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "nativewindow.h"

#include "theme.h"

#include <QWidget>
#include <QWindow>

#import <AppKit/AppKit.h>

void Qdc::styleTitleBar(QWidget* widget)
{
	if (!widget) return;

	QWindow* handle = widget->windowHandle();
	if (!handle) return;

	/*
	 * Checked rather than trusted: winId() is only an NSView once the platform window
	 * has been made, and asking before then - during construction, say - hands back
	 * something that is not an object at all.
	 */
	id handled = reinterpret_cast<id>(handle->winId());
	if (!handled || ![handled isKindOfClass:[NSView class]]) return;

	NSWindow* window = static_cast<NSView*>(handled).window;
	if (!window) return;

	/*
	 * Cheap enough to call on every layout, and it has to be: Qt rewrites the window's
	 * style mask whenever the size constraints change - switching view is enough - and
	 * takes the transparency with it. Nothing below is worth doing twice, so the state
	 * that Qt resets is what decides whether to bother.
	 */
	if (window.titlebarAppearsTransparent
		&& window.titleVisibility == NSWindowTitleHidden)
	{
		return;
	}

	const QColor frame = theme().frame;

	/*
	 * A transparent title bar draws no background of its own, so what shows through it
	 * is the window's - and that is the only way to colour one, since AppKit otherwise
	 * fills it with a system material that nothing set on the widgets below can reach.
	 *
	 * Growing the content up behind the title bar instead, so that the top bar paints
	 * the strip, does work and is not worth what it costs: it needs the safe area
	 * turned off to get Qt to lay out that far, the window buttons then sit over the
	 * top bar, and every time Qt rewrites the style mask the window grows by the
	 * height of the title bar and the strip reverts until it is put back. This way the
	 * title bar stays the platform's, which also keeps dragging and zooming working
	 * without any help from us.
	 */
	window.titlebarAppearsTransparent = YES;

	/*
	 * Named in the screen's own colour space rather than in sRGB, which means it
	 * reaches the framebuffer unconverted.
	 *
	 * Qt writes its pixels straight out without colour management, so an sRGB colour
	 * here does not land on the same values as the identical colour painted by the top
	 * bar just below it: measured on a wide-gamut display, #0c2d80 against #1e328f - a
	 * seam right across the top of the window. sRGB is the more correct way to name a
	 * colour, and matching what Qt actually does is what closes the seam.
	 */
	NSColorSpace* space = window.screen.colorSpace
		? window.screen.colorSpace : [NSColorSpace deviceRGBColorSpace];

	const CGFloat components[4] = {
		frame.redF(), frame.greenF(), frame.blueF(), 1.0
	};

	window.backgroundColor = [NSColor colorWithColorSpace:space
	                                          components:components
	                                               count:4];

	/*
	 * The dark appearance is for the window buttons rather than for the widgets: on
	 * a light one they are drawn in a grey that all but disappears against a strong
	 * background when the window is not in front.
	 */
	window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];

	/*
	 * The title itself is hidden because the top bar sits directly below and already
	 * names the view: drawn as well, it is the same thing said twice, in the same
	 * colour, one line apart.
	 */
	window.titleVisibility = NSWindowTitleHidden;
}

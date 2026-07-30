/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_THEME_H
#define HAVE_QUICKDC_QUI_THEME_H

#include <QColor>
#include <QPalette>
#include <QString>

namespace Qdc {

/**
 * Every colour and corner radius the interface uses.
 *
 * One struct rather than colours scattered through the widgets, so that a second
 * theme is a second set of values and not a second set of edits. Nothing below
 * reads QPalette for a decision it could make from here - the palette is derived
 * from this, not the other way round.
 *
 * The three surfaces are the shape of the whole layout, darkest to lightest:
 *
 *   rail     the navigation strip, darkest
 *   sidebar  the list of hubs and conversations beside it
 *   content  where the reading happens, and the lightest
 *
 * A theme is expected to keep that order. Anything painted on a surface takes its
 * ink from the matching text colour rather than guessing from the background.
 */
struct Theme {
	QString name;

	/**
	 * The application frame: the window background, the top bar and the rail.
	 *
	 * One colour for all three on purpose. They are the chrome around the content
	 * and should read as a single surface the content is inset into, rather than
	 * as three panels that happen to be the same sort of dark.
	 */
	QColor frame;

	/**
	 * Text drawn straight onto the frame, such as the top bar title.
	 *
	 * Distinct from railTextActive, which is the ink on the *selected* pill and so
	 * has to be dark - the pill itself is light.
	 */
	QColor frameText;

	/* The navigation rail, which sits on the frame. */
	QColor rail;
	QColor railText;
	QColor railTextActive;

	/** Behind the current destination. */
	QColor railActive;

	/** The wash under the pointer, which is blended over the rail. */
	QColor railHover;

	/* The secondary navigation: open hubs and conversations. */
	QColor sidebar;
	QColor sidebarText;
	QColor sidebarTextActive;
	QColor sidebarActive;
	QColor sidebarHover;

	/** A heading over the sidebar list. */
	QColor sidebarHeading;

	/**
	 * Behind a selected row, wherever one is: a rail destination, a hub in the
	 * sidebar, a row in a table.
	 *
	 * Light, so it carries selectionText rather than white — which is what lets the
	 * same pair work on the dark rail and on a white table alike.
	 */
	QColor selection;
	QColor selectionText;

	/** A user who is present. */
	QColor presence;

	/** An unread count, on the rail and in the sidebar. */
	QColor notification;

	/** Legible on 'notification', which is light rather than dark. */
	QColor notificationText;

	/* The content area. */
	QColor content;

	/** A row stripe, a header strip, a disabled field. */
	QColor contentAlt;

	QColor contentText;

	/** Timestamps, explanations, anything secondary. */
	QColor contentMuted;

	QColor border;

	/** Selection, links, the filled part of a progress bar. */
	QColor accent;
	QColor accentText;

	/** Unread counts and destructive actions. */
	QColor danger;

	/** A secure connection. */
	QColor success;

	/** A favourite. */
	QColor star;

	/* Corner radii. */
	int radiusSmall;
	int radiusMedium;
	int radiusLarge;

	/**
	 * Whether the content surface is dark.
	 *
	 * Asked by the chat delegate, which picks nickname colours that have to stay
	 * legible against it. Derived rather than declared so a theme cannot state one
	 * thing and set another.
	 */
	bool contentIsDark() const;
};

/** The scheme in force. */
const Theme& theme();

/**
 * Change it.
 *
 * Only takes effect for widgets built or repainted afterwards, so a live switch
 * needs applyTheme() and a repaint of everything. There is one theme today; this
 * exists so that adding a second is a change of data.
 */
void setTheme(const Theme& theme);

/** The default: a deep aubergine rail, a lighter sidebar, and white content. */
Theme defaultTheme();

/**
 * The palette a theme implies.
 *
 * Set application-wide so that widgets nobody has styled by hand still land on
 * the right colours, and so the item delegates - which are handed a palette
 * rather than a theme - agree with everything around them.
 */
QPalette palette(const Theme& theme);

/**
 * The stylesheet a theme implies.
 *
 * Carries what a palette cannot: corner radii, borders, padding, and the two dark
 * surfaces, which are addressed by object name. Widgets that paint themselves -
 * the rail buttons, the chat lines, the progress bars - read the theme directly
 * instead.
 */
QString styleSheet(const Theme& theme);

/** Set the palette and stylesheet on the application. */
void applyTheme(const Theme& theme);

}

#endif // HAVE_QUICKDC_QUI_THEME_H

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_THEME_H
#define HAVE_QUICKDC_QUI_THEME_H

#include <QColor>
#include <QList>
#include <QPalette>
#include <QString>
#include <QStringList>

class QJsonObject;
class QObject;

namespace Qdc {

/**
 * Every colour and corner radius the interface uses.
 *
 * One struct rather than colours scattered through the widgets, so that a second
 * theme is a second set of values and not a second set of edits. Nothing below
 * reads QPalette for a decision it could make from here - the palette is derived
 * from this, not the other way round.
 *
 * Each field name below is also the name a theme file sets and the name a
 * stylesheet writes as %(field), so adding a colour here teaches both at once.
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
 * Only changes what widgets built or repainted afterwards will read, so a live
 * switch means applyTheme() rather than this.
 */
void setTheme(const Theme& theme);

/** The default: a deep indigo frame, a lighter sidebar, and white content. */
Theme defaultTheme();

/**
 * Every scheme on offer, in the order a chooser should list them.
 *
 * The default first, then each theme file that was found - the ones the
 * application ships, then anything in themeDirectory(). A file naming a theme
 * that is already in the list replaces it rather than adding a second entry of
 * the same name, which is how the shipped schemes can be retuned from outside.
 *
 * Read from disk on every call rather than cached, because the only caller that
 * asks repeatedly is watching for the files to change.
 */
QList<Theme> availableThemes();

/** By name, or the default when there is no theme of that name. */
Theme themeByName(const QString& name);

/**
 * Where a theme file or a stylesheet override may be dropped: ~/.quickdc/themes.
 *
 * Alongside the core's own configuration, since that is where a user of this
 * application already keeps its files. Need not exist.
 */
QString themeDirectory();

/**
 * A theme read from JSON.
 *
 * Every key is optional and named exactly as the struct field is; @a base stands
 * in for anything the object does not mention, so a variant is the handful of
 * colours that differ from what it extends and nothing else. A colour is
 * "#rrggbb", "#aarrggbb", an SVG colour name, or "rgba(r,g,b,a)" with the alpha
 * between 0 and 1 - which is the form styleSheet() writes, so what a theme prints
 * is what it reads back.
 *
 * A key that is not a colour is reported and left at the base value rather than
 * refused: a typo in a theme file should cost one wrong colour, not an unstyled
 * application.
 *
 * @param problems if given, every complaint is appended here.
 */
Theme themeFromJson(const QJsonObject& object, const Theme& base,
                    QStringList* problems = nullptr);

/**
 * Re-apply the scheme in force whenever a theme file is written, for design work.
 *
 * Does nothing unless QDC_THEME_WATCH is set, so a shipped application neither
 * watches a directory nor restyles itself while somebody is using it.
 *
 * @param owner what the watcher belongs to, and so how long it lives.
 */
void watchThemes(QObject* owner);

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

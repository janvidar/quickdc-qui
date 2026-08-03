/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

Qdc::Theme& current()
{
	static Qdc::Theme theme = Qdc::defaultTheme();
	return theme;
}

/** Where the schemes the application ships are compiled in. */
const QString BUILT_IN_THEMES = QStringLiteral(":/themes");

/** The stylesheet template, under the same name in the resources and the folder. */
const QString SHEET = QStringLiteral("widgets.qss");

/** What a theme file calls the theme it layers itself onto. */
const QString BASE_KEY = QStringLiteral("base");

const QString NAME_KEY = QStringLiteral("name");

/**
 * Every colour in a Theme, under the name the struct gives it.
 *
 * The one list of them: it is both what a theme file may set and what a stylesheet
 * may write as %(name), so a new colour is a line here rather than an edit in three
 * places that are free to disagree with each other.
 */
struct ColourField {
	QLatin1StringView name;
	QColor Qdc::Theme::* member;
};

const ColourField COLOURS[] = {
	{ "frame"_L1,              &Qdc::Theme::frame },
	{ "frameText"_L1,          &Qdc::Theme::frameText },
	{ "rail"_L1,               &Qdc::Theme::rail },
	{ "railText"_L1,           &Qdc::Theme::railText },
	{ "railTextActive"_L1,     &Qdc::Theme::railTextActive },
	{ "railActive"_L1,         &Qdc::Theme::railActive },
	{ "railHover"_L1,          &Qdc::Theme::railHover },
	{ "sidebar"_L1,            &Qdc::Theme::sidebar },
	{ "sidebarText"_L1,        &Qdc::Theme::sidebarText },
	{ "sidebarTextActive"_L1,  &Qdc::Theme::sidebarTextActive },
	{ "sidebarActive"_L1,      &Qdc::Theme::sidebarActive },
	{ "sidebarHover"_L1,       &Qdc::Theme::sidebarHover },
	{ "sidebarHeading"_L1,     &Qdc::Theme::sidebarHeading },
	{ "selection"_L1,          &Qdc::Theme::selection },
	{ "selectionText"_L1,      &Qdc::Theme::selectionText },
	{ "presence"_L1,           &Qdc::Theme::presence },
	{ "notification"_L1,       &Qdc::Theme::notification },
	{ "notificationText"_L1,   &Qdc::Theme::notificationText },
	{ "content"_L1,            &Qdc::Theme::content },
	{ "contentAlt"_L1,         &Qdc::Theme::contentAlt },
	{ "contentText"_L1,        &Qdc::Theme::contentText },
	{ "contentMuted"_L1,       &Qdc::Theme::contentMuted },
	{ "border"_L1,             &Qdc::Theme::border },
	{ "accent"_L1,             &Qdc::Theme::accent },
	{ "accentText"_L1,         &Qdc::Theme::accentText },
	{ "danger"_L1,             &Qdc::Theme::danger },
	{ "success"_L1,            &Qdc::Theme::success },
	{ "star"_L1,               &Qdc::Theme::star }
};

struct RadiusField {
	QLatin1StringView name;
	int Qdc::Theme::* member;
};

const RadiusField RADII[] = {
	{ "radiusSmall"_L1,  &Qdc::Theme::radiusSmall },
	{ "radiusMedium"_L1, &Qdc::Theme::radiusMedium },
	{ "radiusLarge"_L1,  &Qdc::Theme::radiusLarge }
};

/** Whether a theme file naming this key meant anything by it. */
bool is_known_key(const QString& key)
{
	if (key == NAME_KEY || key == BASE_KEY) return true;

	const auto named = [&key](const auto& field) { return key == field.name; };

	return std::any_of(std::begin(COLOURS), std::end(COLOURS), named)
	    || std::any_of(std::begin(RADII), std::end(RADII), named);
}

/** "#rrggbb", or "rgba(r,g,b,a)" when the colour is not opaque. */
QString css(const QColor& colour)
{
	if (colour.alpha() == 255) return colour.name(QColor::HexRgb);

	return QStringLiteral("rgba(%1,%2,%3,%4)")
		.arg(colour.red()).arg(colour.green()).arg(colour.blue())
		.arg(colour.alphaF());
}

/**
 * A colour as a theme file may write one.
 *
 * The hex and SVG-name forms are Qt's own. The rgba() form is handled here because
 * it is what css() prints for a colour that is not opaque, and a theme should read
 * back what it writes.
 */
QColor parse_colour(const QString& text)
{
	static const QRegularExpression rgba(
		QStringLiteral(R"(^rgba\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*([0-9.]+)\s*\)$)"));

	const QString trimmed = text.trimmed();
	const QRegularExpressionMatch match = rgba.match(trimmed);

	if (!match.hasMatch()) return QColor::fromString(trimmed);

	QColor colour(match.captured(1).toInt(), match.captured(2).toInt(),
	              match.captured(3).toInt());

	/* Out of range in any channel, and Qt has already made it invalid for us. */
	if (!colour.isValid()) return {};

	colour.setAlphaF(qBound(0.0f, match.captured(4).toFloat(), 1.0f));

	return colour;
}

/** Every %(token) a stylesheet may use, and the colour or number it stands for. */
QHash<QString, QString> tokens(const Qdc::Theme& theme)
{
	QHash<QString, QString> map;

	for (const ColourField& field : COLOURS)
		map.insert(field.name, css(theme.*field.member));

	for (const RadiusField& field : RADII)
		map.insert(field.name, QString::number(theme.*field.member));

	/*
	 * Derived rather than declared: a hover that is a shade of the accent cannot
	 * drift away from the accent, and a theme has one fewer thing to get right.
	 */
	map.insert(QStringLiteral("accentHover"), css(theme.accent.darker(115)));

	return map;
}

/**
 * Substitute every %(token) in one pass.
 *
 * One pass rather than a replace() for each name, because a chain of those is
 * ordered by accident rather than by intent: %(frame) replaced before %(frameText)
 * leaves "Text" behind in the sheet, and everybody adding a token afterwards has to
 * know it. Here a name matches whole and the order of the map means nothing.
 *
 * An unknown token is dropped and named on the console. Left in place it would be a
 * syntax error, which Qt answers by discarding the whole rule without a word.
 */
QString expand(const QString& sheet, const QHash<QString, QString>& map)
{
	static const QRegularExpression token(QStringLiteral(R"(%\((\w+)\))"));

	QString expanded;
	expanded.reserve(sheet.size());

	qsizetype from = 0;

	QRegularExpressionMatchIterator matches = token.globalMatch(sheet);

	while (matches.hasNext())
	{
		const QRegularExpressionMatch match = matches.next();

		expanded += QStringView(sheet).sliced(from, match.capturedStart() - from);

		const QString name = match.captured(1);
		const auto found = map.constFind(name);

		if (found != map.constEnd())
			expanded += *found;
		else
			qWarning("theme: the stylesheet asks for %%(%s), which no theme sets",
			         qUtf8Printable(name));

		from = match.capturedEnd();
	}

	expanded += QStringView(sheet).sliced(from);

	return expanded;
}

/**
 * The stylesheet template: the folder's copy if there is one, else the built-in.
 *
 * Which is what lets padding, borders and whole rules be worked on without a
 * build - the colours are the easy half.
 */
QString sheet_template()
{
	const QString paths[] = {
		QDir(Qdc::themeDirectory()).filePath(SHEET),
		QDir(BUILT_IN_THEMES).filePath(SHEET)
	};

	for (const QString& path : paths)
	{
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

		return QString::fromUtf8(file.readAll());
	}

	/*
	 * Loud, because the interface is about to come up as plain Fusion widgets and
	 * the reason is not something a user could guess at.
	 */
	qWarning("theme: %s is missing; the interface will be unstyled",
	         qUtf8Printable(SHEET));

	return QString();
}

/**
 * Read one theme file, layered onto whatever it extends.
 *
 * Its "base" names another theme in @a known and defaults to the first of them,
 * which is the application's own. Resolved against what has been read before it
 * rather than against the whole set, so that a chain of files cannot close into a
 * cycle.
 */
std::optional<Qdc::Theme> read_theme(const QString& path, const QList<Qdc::Theme>& known)
{
	QFile file(path);

	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		qWarning("theme: %s could not be read", qUtf8Printable(path));
		return {};
	}

	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);

	if (!document.isObject())
	{
		qWarning("theme: %s is not a theme: %s", qUtf8Printable(path),
		         qUtf8Printable(error.errorString()));
		return {};
	}

	const QJsonObject object = document.object();

	Qdc::Theme base = known.isEmpty() ? Qdc::defaultTheme() : known.constFirst();

	const QString extends = object.value(BASE_KEY).toString();

	if (!extends.isEmpty())
	{
		const auto found = std::find_if(known.begin(), known.end(),
			[&extends](const Qdc::Theme& theme) { return theme.name == extends; });

		if (found != known.end())
			base = *found;
		else
			qWarning("theme: %s extends '%s', which is not a theme read before it",
			         qUtf8Printable(path), qUtf8Printable(extends));
	}

	QStringList problems;
	const Qdc::Theme theme = Qdc::themeFromJson(object, base, &problems);

	for (const QString& problem : problems)
		qWarning("theme: %s: %s", qUtf8Printable(path), qUtf8Printable(problem));

	return theme;
}

}

bool Qdc::Theme::contentIsDark() const
{
	return content.lightness() < 128;
}

const Qdc::Theme& Qdc::theme()
{
	return current();
}

void Qdc::setTheme(const Theme& theme)
{
	current() = theme;
}

QString Qdc::themeDirectory()
{
	return QDir::homePath() + QStringLiteral("/.quickdc/themes");
}

Qdc::Theme Qdc::defaultTheme()
{
	Theme theme;

	theme.name = QStringLiteral("Indigo");

	/*
	 * The frame colour, used for the window, the top bar and the rail alike so that
	 * the three meet without a seam.
	 */
	theme.frame             = QColor(0x1e, 0x32, 0x8f);
	theme.frameText         = QColor(0xff, 0xff, 0xff);

	theme.rail              = theme.frame;
	theme.railText          = QColor(0xc2, 0xcb, 0xeb);
	theme.railTextActive    = theme.frame;
	theme.railActive        = QColor(0xf0, 0xf3, 0xfc);
	theme.railHover         = QColor(0xff, 0xff, 0xff, 0x1f);

	/*
	 * A step lighter than the rail rather than darker: the surfaces brighten from
	 * the frame towards the content, so depth reads in one direction.
	 */
	theme.sidebar           = QColor(0x39, 0x4d, 0xb8);
	theme.sidebarText       = QColor(0xd3, 0xda, 0xf2);
	theme.sidebarTextActive = theme.frame;
	theme.sidebarActive     = QColor(0xf0, 0xf3, 0xfc);
	theme.sidebarHover      = QColor(0xff, 0xff, 0xff, 0x1a);
	theme.sidebarHeading    = QColor(0xa8, 0xb4, 0xe0);

	theme.selection         = QColor(0xf0, 0xf3, 0xfc);
	theme.selectionText     = theme.frame;

	theme.presence          = QColor(0xfc, 0xc8, 0x94);

	theme.notification      = QColor(0xa0, 0xb3, 0xf3);
	theme.notificationText  = theme.frame;

	theme.content           = QColor(0xff, 0xff, 0xff);

	/* Tinted towards the frame rather than a neutral grey, so that it belongs. */
	theme.contentAlt        = QColor(0xf7, 0xf8, 0xfc);
	theme.contentText       = QColor(0x1d, 0x1c, 0x1d);
	theme.contentMuted      = QColor(0x61, 0x60, 0x61);
	theme.border            = QColor(0xe0, 0xe4, 0xf0);

	theme.accent            = theme.frame;
	theme.accentText        = QColor(0xff, 0xff, 0xff);
	theme.danger            = QColor(0xe0, 0x1e, 0x5a);
	theme.success           = QColor(0x2b, 0xac, 0x76);
	theme.star              = QColor(0xe8, 0xa8, 0x1f);

	theme.radiusSmall       = 4;
	theme.radiusMedium      = 6;
	theme.radiusLarge       = 8;

	return theme;
}

Qdc::Theme Qdc::themeFromJson(const QJsonObject& object, const Theme& base,
                              QStringList* problems)
{
	Theme theme = base;

	/*
	 * Down to the name: a file that does not say what it is called is a set of
	 * changes to what it extends, and replaces it in the list rather than appearing
	 * beside it as a second scheme of the same name.
	 */
	if (object.value(NAME_KEY).isString()) theme.name = object.value(NAME_KEY).toString();

	for (const ColourField& field : COLOURS)
	{
		const QJsonValue value = object.value(field.name);
		if (value.isUndefined()) continue;

		const QColor colour = parse_colour(value.toString());

		if (!colour.isValid())
		{
			if (problems)
				*problems << QStringLiteral("'%1' is not a colour: %2")
					.arg(QString(field.name), value.toVariant().toString());

			continue;
		}

		theme.*field.member = colour;
	}

	for (const RadiusField& field : RADII)
	{
		const QJsonValue value = object.value(field.name);
		if (value.isUndefined()) continue;

		if (!value.isDouble())
		{
			if (problems)
				*problems << QStringLiteral("'%1' is not a number: %2")
					.arg(QString(field.name), value.toVariant().toString());

			continue;
		}

		theme.*field.member = value.toInt();
	}

	/*
	 * A key nobody reads, said out loud. A misspelt colour is otherwise the quietest
	 * failure there is: the file loads, the theme appears, and one thing is the wrong
	 * colour for reasons that are nowhere.
	 */
	if (problems)
	{
		const QStringList keys = object.keys();

		for (const QString& key : keys)
		{
			if (is_known_key(key)) continue;

			*problems << QStringLiteral("'%1' is not a theme setting").arg(key);
		}
	}

	return theme;
}

QList<Qdc::Theme> Qdc::availableThemes()
{
	QList<Theme> themes{ defaultTheme() };

	/*
	 * The resources first and the folder second, so that a file named for a scheme
	 * that ships is read after it and takes its place. Alphabetically within each,
	 * because a list of themes should not reorder itself between two runs.
	 */
	const QString directories[] = { BUILT_IN_THEMES, themeDirectory() };

	for (const QString& directory : directories)
	{
		const QDir folder(directory);
		const QStringList files =
			folder.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);

		for (const QString& name : files)
		{
			const std::optional<Theme> read = read_theme(folder.filePath(name), themes);
			if (!read) continue;

			const auto same = std::find_if(themes.begin(), themes.end(),
				[&read](const Theme& theme) { return theme.name == read->name; });

			if (same != themes.end()) *same = *read;
			else themes.append(*read);
		}
	}

	return themes;
}

Qdc::Theme Qdc::themeByName(const QString& name)
{
	const QList<Theme> themes = availableThemes();

	const auto found = std::find_if(themes.begin(), themes.end(),
		[&name](const Theme& theme) { return theme.name == name; });

	if (found != themes.end()) return *found;

	/*
	 * Not an error worth saying anything about: a theme file that was there when the
	 * choice was stored and is not there now is exactly how this happens.
	 */
	return themes.isEmpty() ? defaultTheme() : themes.constFirst();
}

QPalette Qdc::palette(const Theme& theme)
{
	QPalette palette;

	/*
	 * Described from the content surface, because that is where all the ordinary
	 * widgets live. The rail and the sidebar set their own.
	 */
	palette.setColor(QPalette::Window, theme.content);
	palette.setColor(QPalette::WindowText, theme.contentText);
	palette.setColor(QPalette::Base, theme.content);
	palette.setColor(QPalette::AlternateBase, theme.contentAlt);
	palette.setColor(QPalette::Text, theme.contentText);
	palette.setColor(QPalette::PlaceholderText, theme.contentMuted);

	palette.setColor(QPalette::Button, theme.contentAlt);
	palette.setColor(QPalette::ButtonText, theme.contentText);

	/*
	 * Light, with dark text on it. The same pair has to work on a white table and
	 * on the dark rail, and a light patch reads as selected against either.
	 */
	palette.setColor(QPalette::Highlight, theme.selection);
	palette.setColor(QPalette::HighlightedText, theme.selectionText);

	palette.setColor(QPalette::ToolTipBase, theme.content);
	palette.setColor(QPalette::ToolTipText, theme.contentText);

	palette.setColor(QPalette::Link, theme.accent);

	/* Greyed out, but still on the content surface rather than on the desktop. */
	palette.setColor(QPalette::Disabled, QPalette::WindowText, theme.contentMuted);
	palette.setColor(QPalette::Disabled, QPalette::Text, theme.contentMuted);
	palette.setColor(QPalette::Disabled, QPalette::ButtonText, theme.contentMuted);

	return palette;
}

QString Qdc::styleSheet(const Theme& theme)
{
	return expand(sheet_template(), tokens(theme));
}

void Qdc::applyTheme(const Theme& theme)
{
	setTheme(theme);

	if (!qApp) return;

	/*
	 * Fusion, on every platform, and before the palette and stylesheet.
	 *
	 * The native styles draw a good deal themselves and ignore much of a
	 * stylesheet while doing it - the macOS style paints tab bars, headers and
	 * buttons natively, so the tab strip stayed grey however it was styled. A
	 * theme that is meant to look the same everywhere needs a style that does what
	 * it is told, and Fusion is the one Qt ships for that.
	 *
	 * The cost is that the application no longer follows the platform's own look.
	 * That is the point: this is a scheme, not a native skin.
	 */
	QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

	QApplication::setPalette(palette(theme));
	qApp->setStyleSheet(styleSheet(theme));

	/*
	 * The widgets that paint themselves read the theme inside their paintEvent, and
	 * a new stylesheet says nothing to them. Told here rather than by each of them
	 * subscribing to something, so that a switch is one call and cannot be half
	 * done. Nothing to do at startup, where this runs before the first widget exists.
	 */
	const QList<QWidget*> widgets = QApplication::allWidgets();

	for (QWidget* widget : widgets) widget->update();
}

void Qdc::watchThemes(QObject* owner)
{
	if (!qEnvironmentVariableIsSet("QDC_THEME_WATCH")) return;

	const QString directory = themeDirectory();
	QDir().mkpath(directory);

	QFileSystemWatcher* watcher = new QFileSystemWatcher(owner);
	watcher->addPath(directory);

	/*
	 * The files as well as the folder they are in. A folder reports what appears in
	 * it and what leaves it, not a write to something already there - and a write is
	 * the whole point of this.
	 */
	const auto follow = [watcher, directory] {
		const QDir folder(directory);
		const QStringList files = folder.entryList(
			{QStringLiteral("*.json"), SHEET}, QDir::Files, QDir::Name);

		for (const QString& name : files)
		{
			const QString path = folder.filePath(name);
			if (!watcher->files().contains(path)) watcher->addPath(path);
		}
	};

	follow();

	/*
	 * Coalesced, and the watch renewed on the way through: an editor saves by
	 * writing a new file over the old one, which takes the watch down with the file
	 * it was placed on, and a save that arrives as three events should not restyle
	 * the window three times.
	 */
	QTimer* settle = new QTimer(owner);
	settle->setSingleShot(true);
	settle->setInterval(60);

	QObject::connect(settle, &QTimer::timeout, owner, [follow] {
		follow();
		applyTheme(themeByName(theme().name));
	});

	const auto touched = [settle] { settle->start(); };

	QObject::connect(watcher, &QFileSystemWatcher::directoryChanged, settle, touched);
	QObject::connect(watcher, &QFileSystemWatcher::fileChanged, settle, touched);

	qInfo("theme: watching %s; a saved theme or stylesheet is applied at once",
	      qUtf8Printable(directory));
}

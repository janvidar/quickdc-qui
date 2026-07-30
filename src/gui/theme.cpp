/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "theme.h"

#include <QApplication>
#include <QStyleFactory>

namespace {

Qdc::Theme& current()
{
	static Qdc::Theme theme = Qdc::defaultTheme();
	return theme;
}

/** "#rrggbb", or "rgba(r,g,b,a)" when the colour is not opaque. */
QString css(const QColor& colour)
{
	if (colour.alpha() == 255) return colour.name(QColor::HexRgb);

	return QStringLiteral("rgba(%1,%2,%3,%4)")
		.arg(colour.red()).arg(colour.green()).arg(colour.blue())
		.arg(colour.alphaF());
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
	/*
	 * Addressed by object name for the frame surfaces and by class for everything
	 * else. Qt applies a stylesheet to a widget and its children, so the rail and
	 * sidebar rules are written to include their descendants.
	 */
	return QStringLiteral(R"(
QWidget {
    color: %(text);
}

/* ---- the frame: window, top bar and rail, all one colour ---- */

QWidget#appFrame, QWidget#topBar, QWidget#navigationRail {
    background: %(frame);
    border: none;
}

QLabel#topBarTitle {
    color: %(frameText);
    font-weight: bold;
}

QLabel#topBarSubtitle {
    color: %(railText);
}

/* ---- the secondary navigation: hubs and conversations ---- */

QWidget#secondaryNav {
    background: %(sidebar);
    border: none;
}

QWidget#secondaryNav QLabel {
    color: %(sidebarText);
}

QTreeWidget#hubTree {
    background: transparent;
    border: none;
    color: %(sidebarText);
    outline: none;
    padding: 4px;
}

QTreeWidget#hubTree::item {
    padding: 5px 6px;
    border-radius: %(radiusSmall)px;
    color: %(sidebarText);
}

QTreeWidget#hubTree::item:hover {
    background: %(sidebarHover);
}

QTreeWidget#hubTree::item:selected {
    background: %(sidebarActive);
    color: %(sidebarTextActive);
}

/*
 * Outlined rather than filled: a solid button on the sidebar competes with the
 * selected hub, which is the one filled thing on that surface.
 */
QWidget#secondaryNav QPushButton {
    background: transparent;
    color: %(sidebarTextActive2);
    border: 1px solid %(sidebarTextActive2);
    border-radius: %(radiusMedium)px;
    padding: 7px 10px;
}

QWidget#secondaryNav QPushButton:hover {
    background: %(sidebarActive);
    color: %(sidebarTextActive);
    border-color: %(sidebarActive);
}

/* ---- content ---- */

/*
 * The page behind the views is a shade off white so that the panels on it read
 * as panels. The conversation is the exception and stays white, because that is
 * the surface being read rather than a card on a page.
 */
/*
 * The content is a card inset into the frame: rounded where it meets the corner
 * the top bar and the rail make between them, square against the window edges it
 * runs up to.
 */
QWidget#contentStack {
    background: %(contentAlt);
    border-top-left-radius: %(radiusLarge)px;
}

QScrollArea {
    background: transparent;
    border: none;
}

/* The page inside a scroll area: viewport, then its single child. */
QScrollArea > QWidget > QWidget {
    background: transparent;
}

QWidget#chatSurface, QWidget#composerSurface, QWidget#hubHeader {
    background: %(content);
}

/* The context sidebar is a panel against the conversation, not a field. */
QListView#userList {
    background: %(content);
    border: none;
    border-left: 1px solid %(border);
    border-radius: 0px;
}

QWidget#hubHeader {
    border-bottom: 1px solid %(border);
}

QListView, QTreeView, QTableView {
    background: %(content);
    border: 1px solid %(border);
    border-radius: %(radiusMedium)px;

    /*
     * Light, with dark text. A selection that persists should not shout; a menu
     * highlight, which follows the pointer and must be unmistakable, keeps the
     * strong accent instead.
     */
    selection-background-color: %(selection);
    selection-color: %(selectionText);
    outline: none;
}

/* Distinguishes a selected row from the row stripe, which is nearly as pale. */
QTableView::item:selected, QTreeView::item:selected, QListView::item:selected {
    background: %(selection);
    color: %(selectionText);
    font-weight: bold;
}

QListView::item, QTreeView::item, QTableView::item {
    padding: 3px 4px;
    border-radius: %(radiusSmall)px;
}

QListView#chatFeed {
    border: none;
    border-radius: 0px;
}

QHeaderView::section {
    background: %(contentAlt);
    color: %(muted);
    border: none;
    border-bottom: 1px solid %(border);
    padding: 6px 8px;
}

QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QComboBox {
    background: %(content);
    border: 1px solid %(border);
    border-radius: %(radiusMedium)px;
    padding: 5px 8px;
    selection-background-color: %(accent);
    selection-color: %(accentText);
}

QLineEdit:focus, QTextEdit:focus, QSpinBox:focus, QComboBox:focus {
    border-color: %(accent);
}

QLineEdit:disabled, QTextEdit:disabled, QSpinBox:disabled {
    background: %(contentAlt);
    color: %(muted);
}

QPushButton {
    background: %(contentAlt);
    border: 1px solid %(border);
    border-radius: %(radiusMedium)px;
    padding: 6px 14px;
}

QPushButton:hover  { background: %(border); }

/* The one action a panel is for. */
QPushButton#primaryButton, QToolButton#primaryButton {
    background: %(accent);
    color: %(accentText);
    border: 1px solid %(accent);
    border-radius: %(radiusMedium)px;
    padding: 6px 16px;
    font-weight: bold;
}

QPushButton#primaryButton:hover, QToolButton#primaryButton:hover {
    background: %(accentHover);
    border-color: %(accentHover);
}

QPushButton#primaryButton:disabled, QToolButton#primaryButton:disabled {
    background: %(border);
    border-color: %(border);
    color: %(muted);
}
QPushButton:default, QPushButton:focus {
    border-color: %(accent);
}
QPushButton:disabled { color: %(muted); }

QToolButton {
    border: none;
    border-radius: %(radiusSmall)px;
    padding: 4px 6px;
}

QToolButton:hover   { background: %(contentAlt); }
QToolButton:checked { background: %(border); }

QGroupBox {
    background: %(content);
    border: 1px solid %(border);
    border-radius: %(radiusLarge)px;
    margin-top: 14px;
    padding: 10px 12px 12px 12px;
    font-weight: bold;
}

QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 10px;
    padding: 0px 5px;
    color: %(muted);

    /* Masks the frame line, which otherwise runs behind the words. */
    background: %(contentAlt);
}

/*
 * The strip the tabs sit in belongs to the tab widget, not to the pane below it
 * and not to the tab bar, which is only as wide as its tabs. All three need the
 * content colour or the page tint shows through beside and behind them.
 */
QTabWidget {
    background: %(content);
}

QTabBar {
    background: %(content);
}

QTabWidget::pane {
    border: none;
    border-top: 1px solid %(border);
    background: %(content);
}

QTabBar::tab {
    background: transparent;
    color: %(muted);
    border: none;

    /* The selected tab is marked by a rule under it, so every tab reserves the
       same space and the row does not shift when the selection moves. */
    border-bottom: 2px solid transparent;
    padding: 7px 14px;
    margin-right: 2px;
}

QTabBar::tab:hover { color: %(text); }

QTabBar::tab:selected {
    color: %(accent);
    border-bottom-color: %(accent);
    font-weight: bold;
}

QSplitter::handle           { background: %(border); }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical   { height: 1px; }

/* Part of the frame, so the chrome closes the window on all four sides. */
QStatusBar {
    background: %(frame);
    border: none;
    color: %(railText);
}

QStatusBar QLabel {
    color: %(railText);
}

QScrollBar:vertical {
    background: transparent;
    width: 11px;
    margin: 2px;
}

QScrollBar::handle:vertical {
    background: %(border);
    border-radius: 5px;
    min-height: 28px;
}

QScrollBar::handle:vertical:hover { background: %(muted); }

QScrollBar:horizontal {
    background: transparent;
    height: 11px;
    margin: 2px;
}

QScrollBar::handle:horizontal {
    background: %(border);
    border-radius: 5px;
    min-width: 28px;
}

QScrollBar::add-line, QScrollBar::sub-line { height: 0px; width: 0px; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QMenu {
    background: %(content);
    border: 1px solid %(border);
    border-radius: %(radiusMedium)px;
    padding: 4px;
}

QMenu::item {
    padding: 6px 22px 6px 14px;
    border-radius: %(radiusSmall)px;
}

QMenu::item:selected  { background: %(accent); color: %(accentText); }
QMenu::separator      { height: 1px; background: %(border); margin: 4px 8px; }

QToolTip {
    background: %(content);
    color: %(text);
    border: 1px solid %(border);
    border-radius: %(radiusSmall)px;
    padding: 4px 6px;
}
)")
		.replace(QStringLiteral("%(frameText)"), css(theme.frameText))
		.replace(QStringLiteral("%(frame)"), css(theme.frame))
		.replace(QStringLiteral("%(railTextActive)"), css(theme.railTextActive))
		.replace(QStringLiteral("%(railText)"), css(theme.railText))
		.replace(QStringLiteral("%(rail)"), css(theme.rail))
		.replace(QStringLiteral("%(selectionText)"), css(theme.selectionText))
		.replace(QStringLiteral("%(selection)"), css(theme.selection))
		.replace(QStringLiteral("%(sidebarTextActive2)"), css(theme.sidebarText))
		.replace(QStringLiteral("%(sidebarTextActive)"), css(theme.sidebarTextActive))
		.replace(QStringLiteral("%(sidebarText)"), css(theme.sidebarText))
		.replace(QStringLiteral("%(sidebarActive)"), css(theme.sidebarActive))
		.replace(QStringLiteral("%(sidebarHover)"), css(theme.sidebarHover))
		.replace(QStringLiteral("%(sidebar)"), css(theme.sidebar))
		.replace(QStringLiteral("%(contentAlt)"), css(theme.contentAlt))
		.replace(QStringLiteral("%(content)"), css(theme.content))
		.replace(QStringLiteral("%(text)"), css(theme.contentText))
		.replace(QStringLiteral("%(muted)"), css(theme.contentMuted))
		.replace(QStringLiteral("%(border)"), css(theme.border))
		.replace(QStringLiteral("%(accentText)"), css(theme.accentText))
		.replace(QStringLiteral("%(accentHover)"), css(theme.accent.darker(115)))
		.replace(QStringLiteral("%(accent)"), css(theme.accent))
		.replace(QStringLiteral("%(radiusSmall)"), QString::number(theme.radiusSmall))
		.replace(QStringLiteral("%(radiusMedium)"), QString::number(theme.radiusMedium))
		.replace(QStringLiteral("%(radiusLarge)"), QString::number(theme.radiusLarge));
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
}

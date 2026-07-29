/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_VIEW_H
#define HAVE_QUICKDC_QUI_VIEW_H

#include <QString>
#include <QWidget>

namespace Qdc {

/**
 * One destination on the navigation rail.
 *
 * The router does not care what a view contains; it needs a name for the rail,
 * a glyph to draw on it, and a way to be told that the view has something
 * waiting. Everything else is the view's own business, including whatever
 * secondary navigation or context sidebar it wants inside itself - those are
 * laid out within the view rather than by the window, so that a view which has
 * no use for them does not have to say so.
 */
class View : public QWidget {
	Q_OBJECT

public:
	/** What the rail draws next to a destination. */
	enum class Glyph {
		Home,
		Hubs,
		Transfers,
		Settings
	};

	explicit View(QWidget* parent = nullptr);

	/** The rail label, and the window title while this view is current. */
	virtual QString title() const = 0;

	virtual Glyph glyph() const = 0;

	/**
	 * How many things are waiting here: unread messages, transfers in flight.
	 * Zero means the rail draws no badge at all.
	 */
	int badgeCount() const;

signals:
	/** Only on a real change, so a view may set the same count repeatedly. */
	void badgeCountChanged(int count);

protected:
	/**
	 * Negative counts are clamped to zero rather than rejected, because the
	 * obvious way to keep one of these is to add and subtract as things arrive
	 * and leave, and a view that loses count should show no badge rather than a
	 * nonsensical one.
	 */
	void setBadgeCount(int count);

private:
	int m_badgeCount = 0;
};

}

#endif // HAVE_QUICKDC_QUI_VIEW_H

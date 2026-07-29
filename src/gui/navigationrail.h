/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_NAVIGATIONRAIL_H
#define HAVE_QUICKDC_QUI_NAVIGATIONRAIL_H

#include "view.h"

#include <QList>
#include <QWidget>

class QBoxLayout;
class QButtonGroup;

namespace Qdc {

class RailButton;

/**
 * The primary navigation rail down the left edge.
 *
 * Holds one destination per top level view, and exactly one of them is current
 * at any time. Destinations added after addTrailingSpace() are pushed to the
 * bottom of the rail, which is where the plan puts Settings.
 *
 * Indices are in the order destinations were added, and the spacer does not take
 * one - so the bottom group continues the same numbering as the top.
 */
class NavigationRail final : public QWidget {
	Q_OBJECT

public:
	explicit NavigationRail(QWidget* parent = nullptr);

	/**
	 * @return the index of the new destination. The first one added becomes
	 *         current, because a rail with nothing selected has no matching
	 *         view to show.
	 */
	int addDestination(View::Glyph glyph, const QString& caption);

	/** Everything added after this sits at the bottom. Call it at most once. */
	void addTrailingSpace();

	/** Zero clears the badge. Out of range indices are ignored. */
	void setBadgeCount(int index, int count);
	int badgeCount(int index) const;

	int count() const;

	/** -1 before the first destination is added. */
	int currentIndex() const;

signals:
	/** Only on a real change, and not while the rail is being built. */
	void currentIndexChanged(int index);

public slots:
	/** Out of range indices are ignored rather than clearing the selection. */
	void setCurrentIndex(int index);

private:
	QBoxLayout* m_layout;

	/**
	 * Owns the exclusivity: a button group is what makes selecting one
	 * destination deselect the last, without this having to track it.
	 */
	QButtonGroup* m_group;

	QList<RailButton*> m_buttons;
	int m_currentIndex = -1;

	/** Where new destinations go, so the bottom group stays at the bottom. */
	int m_insertAt = 0;
};

}

#endif // HAVE_QUICKDC_QUI_NAVIGATIONRAIL_H

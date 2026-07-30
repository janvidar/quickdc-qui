/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_CORNERMASK_H
#define HAVE_QUICKDC_QUI_CORNERMASK_H

#include <QWidget>

namespace Qdc {

/**
 * Rounds off the corners of the panel it is laid over.
 *
 * The frame colour is painted into the four corners from above, rather than the
 * panel rounding its own background. A stylesheet radius only shows where nothing
 * covers it, and what reaches the corner differs from view to view - a sidebar in
 * one, a tab strip in another, a white conversation in a third - so the panel's
 * own rounding is hidden in exactly the views that have something in the corner.
 * Painting over the top is the one place that cannot be undercut.
 *
 * Laid over a sibling rather than wrapped around it: a child is painted after the
 * siblings below it, which is what puts these corners on top.
 */
class CornerMask final : public QWidget {
public:
	explicit CornerMask(QWidget* parent = nullptr);

	/** Lie over @a area, in the parent's coordinates, and stay above it. */
	void cover(const QRect& area);

protected:
	void paintEvent(QPaintEvent* event) override;
};

}

#endif // HAVE_QUICKDC_QUI_CORNERMASK_H

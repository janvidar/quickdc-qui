/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_RAILBUTTON_H
#define HAVE_QUICKDC_QUI_RAILBUTTON_H

#include "glyphs.h"

#include <QAbstractButton>

namespace Qdc {

/**
 * One destination on the navigation rail: a glyph, a caption under it, and a
 * badge over its corner when something is waiting.
 *
 * Drawn rather than assembled out of a QToolButton and a floating QLabel. The
 * badge has to sit on top of the glyph and outside its bounds, which a layout
 * will not do, and a checked tool button takes its selected look from the style
 * rather than from anything this can control.
 */
class RailButton final : public QAbstractButton {
	Q_OBJECT

public:
	RailButton(Glyph glyph, const QString& caption, QWidget* parent = nullptr);

	/** Zero draws no badge. Anything over 99 is drawn as "99+". */
	void setBadgeCount(int count);
	int badgeCount() const;

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

	/**
	 * The hover wash is painted here rather than left to the style, so the
	 * repaint has to be asked for by hand.
	 */
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	/** Where the badge goes, given the box the glyph was drawn in. */
	QRectF badgeRect(const QRectF& glyphBox, const QString& text) const;

	Glyph m_glyph;
	int m_badgeCount = 0;
};

}

#endif // HAVE_QUICKDC_QUI_RAILBUTTON_H

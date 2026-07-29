/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_PLACEHOLDERVIEW_H
#define HAVE_QUICKDC_QUI_PLACEHOLDERVIEW_H

#include "view.h"

class QLabel;

namespace Qdc {

/**
 * A view that states its own empty condition and nothing else.
 *
 * The shell needs four destinations before there is anything to put in three of
 * them, and an empty panel with no explanation reads as a bug. This gives each
 * one a heading and a sentence saying why it is empty, which is what the views
 * will show anyway once they have content and happen not to have any.
 *
 * Subclassed rather than used directly, so that a view gains behaviour without
 * the window having to be told which of them are still placeholders.
 */
class PlaceholderView : public View {
	Q_OBJECT

public:
	PlaceholderView(const QString& heading, const QString& detail,
	                QWidget* parent = nullptr);

	QString title() const override;

protected:
	/** Replace the sentence under the heading as the view learns more. */
	void setDetail(const QString& detail);

private:
	QString m_heading;
	QLabel* m_detail;
};

}

#endif // HAVE_QUICKDC_QUI_PLACEHOLDERVIEW_H

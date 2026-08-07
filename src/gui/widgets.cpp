/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "widgets.h"

#include <QLabel>

QLabel* Qdc::makeNote(const QString& text, QWidget* parent)
{
	QLabel* note = new QLabel(text, parent);
	note->setWordWrap(true);

	note->setObjectName(QStringLiteral("mutedText"));

	QFont smaller = note->font();
	smaller.setPointSize(qMax(1, smaller.pointSize() - 1));
	note->setFont(smaller);

	return note;
}

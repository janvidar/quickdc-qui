/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "placeholderview.h"

#include <QFont>
#include <QLabel>
#include <QVBoxLayout>

Qdc::PlaceholderView::PlaceholderView(const QString& heading, const QString& detail,
                                      QWidget* parent)
	: View(parent)
	, m_heading(heading)
	, m_detail(new QLabel(detail, this))
{
	QLabel* title = new QLabel(heading, this);

	QFont headingFont = title->font();
	headingFont.setPointSize(headingFont.pointSize() + 4);
	headingFont.setBold(true);
	title->setFont(headingFont);
	title->setAlignment(Qt::AlignCenter);

	m_detail->setAlignment(Qt::AlignCenter);
	m_detail->setWordWrap(true);

	QPalette muted = m_detail->palette();
	muted.setColor(QPalette::WindowText, muted.color(QPalette::PlaceholderText));
	m_detail->setPalette(muted);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->addStretch(1);
	layout->addWidget(title);
	layout->addSpacing(6);
	layout->addWidget(m_detail);
	layout->addStretch(2);
}

QString Qdc::PlaceholderView::title() const
{
	return m_heading;
}

void Qdc::PlaceholderView::setDetail(const QString& detail)
{
	m_detail->setText(detail);
}

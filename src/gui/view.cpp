/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "view.h"

Qdc::View::View(QWidget* parent)
	: QWidget(parent)
{
}

int Qdc::View::badgeCount() const
{
	return m_badgeCount;
}

void Qdc::View::setBadgeCount(int count)
{
	const int clamped = (count > 0) ? count : 0;
	if (clamped == m_badgeCount) return;

	m_badgeCount = clamped;
	emit badgeCountChanged(clamped);
}

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "navigationrail.h"

#include "railbutton.h"

#include <QButtonGroup>
#include <QVBoxLayout>

Qdc::NavigationRail::NavigationRail(QWidget* parent)
	: QWidget(parent)
	, m_layout(new QVBoxLayout(this))
	, m_group(new QButtonGroup(this))
{
	m_layout->setContentsMargins(0, 6, 0, 6);
	m_layout->setSpacing(2);

	m_group->setExclusive(true);

	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

	/*
	 * Named so the stylesheet can address it: the rail is the darkest surface in
	 * the window and does not follow the content palette.
	 */
	setObjectName(QStringLiteral("navigationRail"));

	/*
	 * The colour is the stylesheet's, which addresses this by that name. Set here as
	 * well and it would be a copy of the rail colour taken when the rail was built,
	 * which is a copy that cannot hear about a theme change and so survives one.
	 */
	setAutoFillBackground(true);

	setAccessibleName(tr("Navigation"));
}

int Qdc::NavigationRail::addDestination(Glyph glyph, const QString& caption)
{
	RailButton* button = new RailButton(glyph, caption, this);

	const int index = m_buttons.size();
	m_buttons.append(button);
	m_group->addButton(button, index);
	m_layout->insertWidget(m_insertAt, button);
	++m_insertAt;

	connect(button, &QAbstractButton::clicked, this, [this, index] {
		setCurrentIndex(index);
	});

	/*
	 * The first destination becomes current without a signal: nothing can be
	 * connected yet, and a rail with no selection has no view to show.
	 */
	if (m_currentIndex < 0)
	{
		m_currentIndex = index;
		button->setChecked(true);
	}

	return index;
}

void Qdc::NavigationRail::addTrailingSpace()
{
	m_layout->insertStretch(m_insertAt, 1);
	++m_insertAt;
}

void Qdc::NavigationRail::setBadgeCount(int index, int count)
{
	if (index < 0 || index >= m_buttons.size()) return;
	m_buttons.at(index)->setBadgeCount(count);
}

int Qdc::NavigationRail::badgeCount(int index) const
{
	if (index < 0 || index >= m_buttons.size()) return 0;
	return m_buttons.at(index)->badgeCount();
}

int Qdc::NavigationRail::count() const
{
	return static_cast<int>(m_buttons.size());
}

int Qdc::NavigationRail::currentIndex() const
{
	return m_currentIndex;
}

void Qdc::NavigationRail::setCurrentIndex(int index)
{
	if (index < 0 || index >= m_buttons.size()) return;

	/*
	 * The button is checked even when the index has not changed, because a click
	 * on the current destination must not be able to leave the rail looking
	 * unselected.
	 */
	m_buttons.at(index)->setChecked(true);

	if (index == m_currentIndex) return;

	m_currentIndex = index;
	emit currentIndexChanged(index);
}

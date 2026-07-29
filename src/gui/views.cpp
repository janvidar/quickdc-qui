/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "views.h"

Qdc::HomeView::HomeView(QWidget* parent)
	: PlaceholderView(tr("Home"),
	                  tr("Favourite hubs and recent activity will appear here."),
	                  parent)
{
}

Qdc::Glyph Qdc::HomeView::glyph() const
{
	return Glyph::Home;
}


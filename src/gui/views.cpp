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

Qdc::SettingsView::SettingsView(QWidget* parent)
	: PlaceholderView(tr("Settings"), tr("Identity, limits and shared folders."),
	                  parent)
{
}

Qdc::Glyph Qdc::SettingsView::glyph() const
{
	return Glyph::Settings;
}

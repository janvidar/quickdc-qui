/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_VIEWS_H
#define HAVE_QUICKDC_QUI_VIEWS_H

#include "placeholderview.h"

namespace Qdc {

/** Favourites, hub discovery and a transfer summary will live here. */
class HomeView final : public PlaceholderView {
	Q_OBJECT

public:
	explicit HomeView(QWidget* parent = nullptr);
	Glyph glyph() const override;
};

/** Identity, limits and directories. */
class SettingsView final : public PlaceholderView {
	Q_OBJECT

public:
	explicit SettingsView(QWidget* parent = nullptr);
	Glyph glyph() const override;
};

}

#endif // HAVE_QUICKDC_QUI_VIEWS_H

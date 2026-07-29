/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_VIEWS_H
#define HAVE_QUICKDC_QUI_VIEWS_H

#include "placeholderview.h"

#include <QSet>

namespace Qdc {

class TransferBridge;

/** Favourites, hub discovery and a transfer summary will live here. */
class HomeView final : public PlaceholderView {
	Q_OBJECT

public:
	explicit HomeView(QWidget* parent = nullptr);
	Glyph glyph() const override;
};

/** Hub chat and direct messages. */
class HubsView final : public PlaceholderView {
	Q_OBJECT

public:
	explicit HubsView(QWidget* parent = nullptr);
	Glyph glyph() const override;
};

/**
 * The transfer queue and what is moving right now.
 *
 * Nothing is listed yet, but the badge is real: the count is kept from the
 * bridge's start and stop events rather than polled, because those are the only
 * two moments it can change. Progress is what needs polling, and there is no
 * progress column here to poll for.
 */
class TransfersView final : public PlaceholderView {
	Q_OBJECT

public:
	/** @param transfers may be null, in which case the badge stays at zero. */
	explicit TransfersView(TransferBridge* transfers, QWidget* parent = nullptr);
	Glyph glyph() const override;

private:
	/** Recounted from the set, so a repeated or unmatched event cannot skew it. */
	void refresh();

	/**
	 * Identities of the transfers believed to be running. A set rather than a
	 * counter because the core can report a transfer finished that was never
	 * seen starting - a view built while one was already in flight - and
	 * decrementing on that would take the count below what is really there.
	 */
	QSet<quintptr> m_live;
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

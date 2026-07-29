/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "views.h"

#include <core/coretypes.h>
#include <core/transferbridge.h>

Qdc::HomeView::HomeView(QWidget* parent)
	: PlaceholderView(tr("Home"),
	                  tr("Favourite hubs and recent activity will appear here."),
	                  parent)
{
}

Qdc::View::Glyph Qdc::HomeView::glyph() const
{
	return Glyph::Home;
}

Qdc::HubsView::HubsView(QWidget* parent)
	: PlaceholderView(tr("Hubs"), tr("No hubs are open."), parent)
{
}

Qdc::View::Glyph Qdc::HubsView::glyph() const
{
	return Glyph::Hubs;
}

Qdc::TransfersView::TransfersView(TransferBridge* transfers, QWidget* parent)
	: PlaceholderView(tr("Transfers"), tr("Nothing is being transferred."), parent)
{
	if (!transfers) return;

	connect(transfers, &TransferBridge::transferStarted,
	        this, [this](const TransferInfo& transfer) {
		m_live.insert(transfer.id);
		refresh();
	});

	/*
	 * All three endings are the same to a count. They are distinct signals
	 * because a list will want to say which happened; a badge does not.
	 */
	const auto ended = [this](const TransferInfo& transfer) {
		m_live.remove(transfer.id);
		refresh();
	};

	connect(transfers, &TransferBridge::transferFinished, this, ended);
	connect(transfers, &TransferBridge::transferStopped, this, ended);
	connect(transfers, &TransferBridge::transferError, this, ended);

	/*
	 * Whatever was already running before this view existed. Without it the
	 * badge would stay wrong until every one of them ended.
	 */
	for (const TransferInfo& transfer : transfers->activeTransfers())
		m_live.insert(transfer.id);

	refresh();
}

Qdc::View::Glyph Qdc::TransfersView::glyph() const
{
	return Glyph::Transfers;
}

void Qdc::TransfersView::refresh()
{
	const int count = static_cast<int>(m_live.size());
	setBadgeCount(count);

	/* %n so a translation can decline the noun rather than be handed "1 transfers". */
	setDetail(count > 0
		? tr("%n transfer(s) in progress.", nullptr, count)
		: tr("Nothing is being transferred."));
}

Qdc::SettingsView::SettingsView(QWidget* parent)
	: PlaceholderView(tr("Settings"), tr("Identity, limits and shared folders."),
	                  parent)
{
}

Qdc::View::Glyph Qdc::SettingsView::glyph() const
{
	return Glyph::Settings;
}

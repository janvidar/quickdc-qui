/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include "updatebridge.h"

#include "corechannel.h"
#include "corethread.h"

#include <api/protocol.h>
#include <update/install.h>

Qdc::UpdateBridge::UpdateBridge(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
{
	if (m_thread) connect(m_thread->channel(), &CoreChannel::received, this, &UpdateBridge::onCoreEvent);
}

Qdc::UpdateBridge::~UpdateBridge() = default;

void Qdc::UpdateBridge::checkNow()
{
	if (m_thread) m_thread->submit(QuickDC::Api::Commands::CheckForUpdate{});
}

void Qdc::UpdateBridge::onCoreEvent(const QuickDC::Api::Event& event)
{
	using namespace QuickDC::Api::Events;

	if (const auto* ready = event.as<UpdateReady>())
	{
		m_version = QString::fromStdString(ready->version);
		m_package = QString::fromStdString(ready->path);
		emit this->ready(m_version);
	}
	else if (const auto* failed = event.as<UpdateFailed>())
	{
		emit this->failed(QString::fromStdString(failed->reason));
	}
}

bool Qdc::UpdateBridge::installOnQuit(QString* why)
{
	if (m_package.isEmpty())
	{
		if (why) *why = tr("No update is ready.");
		return false;
	}

	std::string reason;
	const bool installed = QuickDC::Update::install(m_package.toStdString(),
		QuickDC::Update::currentInstallation(), reason);
	if (!installed && why) *why = QString::fromStdString(reason);
	return installed;
}

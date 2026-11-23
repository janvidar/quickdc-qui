/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "corechannel.h"

Qdc::CoreChannel::CoreChannel(QObject* parent)
	: QObject(parent)
{
	qRegisterMetaType<QuickDC::Api::Event>();
}

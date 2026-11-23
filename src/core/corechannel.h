/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_CORECHANNEL_H
#define HAVE_QUICKDC_QUI_CORECHANNEL_H

#include <api/protocol.h>

#include <QMetaType>
#include <QObject>

namespace Qdc {

/**
 * The core's events, as a Qt signal.
 *
 * Its own object, and its own header that only the bridges include, so that the
 * protocol's types stay behind the bridge library and no view can name one.
 * event() is emitted on the core thread: a receiver on any other thread is
 * queued a copy, and one that is destroyed is disconnected by Qt.
 */
class CoreChannel final : public QObject {
	Q_OBJECT

public:
	explicit CoreChannel(QObject* parent = nullptr);

signals:
	void event(const QuickDC::Api::Event& event);
};

}

Q_DECLARE_METATYPE(QuickDC::Api::Event)

#endif // HAVE_QUICKDC_QUI_CORECHANNEL_H

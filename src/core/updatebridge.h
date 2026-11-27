/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef QDC_CORE_UPDATEBRIDGE_H
#define QDC_CORE_UPDATEBRIDGE_H

#include <QObject>
#include <QString>

namespace QuickDC::Api { struct Event; }

namespace Qdc {

class CoreThread;

/**
 * What the core's updater has made ready, kept until the program quits and then
 * installed: an update is never applied under a running session.
 */
class UpdateBridge : public QObject
{
	Q_OBJECT

public:
	explicit UpdateBridge(CoreThread* thread, QObject* parent = nullptr);
	~UpdateBridge() override;

	/** The version downloaded and checked, or empty for none. */
	QString readyVersion() const { return m_version; }

	/** Ask the feed now. */
	void checkNow();

	/**
	 * Put the ready update in place of this program. Call once the core has
	 * stopped. False, with 'why' filled in where it is given, if there was
	 * nothing to install or it could not be.
	 */
	bool installOnQuit(QString* why = nullptr);

signals:
	void ready(const QString& version);
	void failed(const QString& reason);

private:
	void onCoreEvent(const QuickDC::Api::Event& event);

	CoreThread* m_thread;
	QString m_version;
	QString m_package;
};

}

#endif // QDC_CORE_UPDATEBRIDGE_H

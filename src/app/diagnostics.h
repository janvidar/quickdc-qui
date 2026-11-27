/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef QDC_APP_DIAGNOSTICS_H
#define QDC_APP_DIAGNOSTICS_H

#include <QObject>
#include <QString>

namespace Qdc {

class ConfigBridge;

/**
 * Crash reports and the count of use (docs/distribution.md, "Crash reporting and
 * diagnostics"), sent only where the person agreed to it.
 *
 * A crash is written by a handler process of its own, so a process too broken to
 * report anything still leaves its minidump; it goes out the next time there is a
 * way to send it. Built without QUICKDC_QUI_CRASH_REPORTS, all of this does nothing.
 */
class Diagnostics : public QObject
{
	Q_OBJECT

public:
	Diagnostics(ConfigBridge* config, QString version, QString channel, QObject* parent = nullptr);
	~Diagnostics() override;

	/** Whether this build can report at all. */
	static bool available();

	/** Ask once, at the first start of a build that can report. Defaults to no. */
	void askOnce(QWidget* parent);

	/** Start or stop as the setting stands; stopping deletes what was not sent. */
	void apply();

	/** For --test-crash: report this run whatever the setting, and say where dumps are kept. */
	void startForTest();

	/** A failure worth knowing about that did not crash anything. */
	void report(const QString& message);

	/** Where minidumps wait to be sent. */
	static QString databasePath();

private:
	void start();
	void stop(bool purge);

	ConfigBridge* m_config;
	QString m_version;
	QString m_channel;
	bool m_running = false;
};

}

#endif // QDC_APP_DIAGNOSTICS_H

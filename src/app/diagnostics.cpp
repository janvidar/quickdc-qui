/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include "diagnostics.h"

#include <core/configbridge.h>
#include <core/uisettings.h>

#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <QUuid>

#include <cstdio>

#ifdef QUICKDC_QUI_CRASH_REPORTS
#include <sentry.h>
#endif

#ifndef QUICKDC_QUI_CRASH_DSN
#define QUICKDC_QUI_CRASH_DSN ""
#endif

Qdc::Diagnostics::Diagnostics(ConfigBridge* config, QString version, QString channel, QObject* parent)
	: QObject(parent)
	, m_config(config)
	, m_version(std::move(version))
	, m_channel(std::move(channel))
{
	if (!m_config) return;
	connect(m_config, &ConfigBridge::changed, this, [this](ConfigStore, const QString& group, const QString& key) {
		if (group == QLatin1String(Settings::DiagnosticsEnabled.group)
		    && key == QLatin1String(Settings::DiagnosticsEnabled.key))
			apply();
	});
}

Qdc::Diagnostics::~Diagnostics()
{
	stop(false);
}

bool Qdc::Diagnostics::available()
{
#ifdef QUICKDC_QUI_CRASH_REPORTS
	return true;
#else
	return false;
#endif
}

QString Qdc::Diagnostics::databasePath()
{
	return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/crashes");
}

void Qdc::Diagnostics::askOnce(QWidget* parent)
{
	if (!available() || !m_config || m_config->read(Settings::DiagnosticsAsked)) return;

	QMessageBox question(QMessageBox::Question, QCoreApplication::applicationName(),
		tr("Send crash reports to the QuickDC developers?"),
		QMessageBox::Yes | QMessageBox::No, parent);
	question.setInformativeText(tr("A report says which version crashed and where. Nothing you share, "
		"say or search for is sent on purpose, though a crash report holds what the program had in "
		"memory. A daily count of use goes with it. You can change your mind under Settings."));
	question.setDefaultButton(QMessageBox::No);

	const bool agreed = question.exec() == QMessageBox::Yes;
	m_config->write(Settings::DiagnosticsAsked, true);
	m_config->write(Settings::DiagnosticsEnabled, agreed);
	apply();
}

void Qdc::Diagnostics::apply()
{
	if (!m_config) return;
	const bool wanted = available() && m_config->read(Settings::DiagnosticsEnabled);
	if (wanted && !m_running) start();
	else if (!wanted && m_running) stop(true);
	else if (!wanted) QDir(databasePath()).removeRecursively();
}

void Qdc::Diagnostics::startForTest()
{
	if (!available()) return;
	if (!m_running) start();
	std::fprintf(stderr, "QuickDC: crash reports are kept in %s\n", qPrintable(databasePath()));
}

#ifdef QUICKDC_QUI_CRASH_REPORTS

void Qdc::Diagnostics::start()
{
	/* The install ID is made at the first start that reports, and is the only thing that tells installs apart. */
	QString id = m_config ? m_config->read(Settings::DiagnosticsInstallId) : QString();
	if (id.isEmpty())
	{
		id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		if (m_config) m_config->write(Settings::DiagnosticsInstallId, id);
	}

	QDir().mkpath(databasePath());

#ifdef Q_OS_WIN
	const QString handler = QCoreApplication::applicationDirPath() + QStringLiteral("/crashpad_handler.exe");
#else
	const QString handler = QCoreApplication::applicationDirPath() + QStringLiteral("/crashpad_handler");
#endif

	sentry_options_t* options = sentry_options_new();
	sentry_options_set_dsn(options, QUICKDC_QUI_CRASH_DSN);
	sentry_options_set_database_path(options, QDir::toNativeSeparators(databasePath()).toUtf8().constData());
	sentry_options_set_handler_path(options, QDir::toNativeSeparators(handler).toUtf8().constData());
	sentry_options_set_release(options, ("quickdc@" + m_version).toUtf8().constData());
	sentry_options_set_environment(options, m_channel.isEmpty() ? "local" : m_channel.toUtf8().constData());
	/*
	 * sentry-native adds no user name or address of its own on a desktop. The
	 * address a report arrives from is the server's to keep or not, and the
	 * project there is set not to keep it.
	 */

	if (sentry_init(options) != 0)
	{
		std::fprintf(stderr, "QuickDC: crash reporting could not be started\n");
		return;
	}

	sentry_value_t user = sentry_value_new_object();
	sentry_value_set_by_key(user, "id", sentry_value_new_string(id.toUtf8().constData()));
	sentry_set_user(user);
	m_running = true;
}

void Qdc::Diagnostics::stop(bool purge)
{
	if (m_running)
	{
		sentry_close();
		m_running = false;
	}
	if (purge) QDir(databasePath()).removeRecursively();
}

void Qdc::Diagnostics::report(const QString& message)
{
	if (!m_running) return;
	sentry_capture_event(sentry_value_new_message_event(SENTRY_LEVEL_WARNING, "quickdc",
		message.toUtf8().constData()));
}

#else

void Qdc::Diagnostics::start() { }
void Qdc::Diagnostics::stop(bool purge) { if (purge) QDir(databasePath()).removeRecursively(); }
void Qdc::Diagnostics::report(const QString&) { }

#endif

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/configbridge.h>
#include <core/corethread.h>
#include <core/updatebridge.h>

#include "diagnostics.h"
#include <gui/mainwindow.h>
#include <gui/theme.h>

#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QSocketNotifier>
#include <QTimer>

#include <csignal>
#include <cstdio>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

#ifndef _WIN32
/**
 * A self-pipe, so that a signal can be turned into an ordinary event.
 *
 * [0] is read by the event loop, [1] is written by the handler.
 */
int signal_pipe[2] = {-1, -1};

/**
 * Nothing here but a write.
 *
 * A signal can arrive anywhere, including part way through an allocation, so a
 * handler that called into Qt or the core would be free to deadlock or corrupt
 * whatever it interrupted. Poking a descriptor is async-signal-safe; the event
 * loop does the actual work.
 */
void on_terminate(int)
{
	const char byte = 1;

	/* Nothing useful to do about a failure here, and nothing safe to say. */
	const ssize_t written = ::write(signal_pipe[1], &byte, 1);
	(void) written;
}

/**
 * Ask the event loop to quit when the process is asked to stop.
 *
 * Without this, a SIGTERM - from a logout, a kill, or a session manager - takes
 * the process down where it stands, skipping the window's teardown and the flush
 * of the download queue and hash cache with it. The reference command line client
 * installs the same handlers for the same reason.
 *
 * @return false if the plumbing could not be set up, in which case the signals
 *         keep their default behaviour and an abrupt exit is still possible.
 */
bool install_signal_handlers(QObject* owner)
{
	if (::pipe(signal_pipe) != 0) return false;

	/*
	 * Non-blocking, so a storm of signals filling the pipe cannot wedge the
	 * handler - a full pipe already means a quit is pending.
	 */
	for (const int end : {0, 1})
	{
		const int flags = ::fcntl(signal_pipe[end], F_GETFL, 0);
		if (flags != -1) ::fcntl(signal_pipe[end], F_SETFL, flags | O_NONBLOCK);
	}

	QSocketNotifier* notifier =
		new QSocketNotifier(signal_pipe[0], QSocketNotifier::Read, owner);

	QObject::connect(notifier, &QSocketNotifier::activated, qApp, [] {
		char discard[16];
		while (::read(signal_pipe[0], discard, sizeof(discard)) > 0)
		{
			/* The bytes say only that a signal arrived. */
		}

		QCoreApplication::quit();
	});

	::signal(SIGINT, on_terminate);
	::signal(SIGTERM, on_terminate);
	::signal(SIGHUP, on_terminate);

	return true;
}
#else
/* Windows has no such signals; Qt turns the end of a session into an ordinary quit. */
bool install_signal_handlers(QObject*)
{
	return false;
}
#endif

}

int main(int argc, char** argv)
{
	QApplication application(argc, argv);

	QApplication::setApplicationName(QStringLiteral("QuickDC"));
	QApplication::setApplicationDisplayName(QStringLiteral("QuickDC"));
	QApplication::setOrganizationName(QStringLiteral("QuickDC"));
	QApplication::setOrganizationDomain(QStringLiteral("extatic.org"));
	QApplication::setApplicationVersion(QStringLiteral(QUICKDC_QUI_VERSION_STRING));
	/* For the settings page, which offers the switch only where it does something. */
	application.setProperty("quickdcCrashReports", Qdc::Diagnostics::available());

	/*
	 * The application id, not the name of the executable. An installed desktop
	 * entry is named after the former, and this is what a Wayland compositor
	 * matches a window against to find it - so a mismatch costs the window its
	 * icon and its association with whatever launched it.
	 */
	QApplication::setDesktopFileName(QStringLiteral(QUICKDC_QUI_APP_ID));

	/*
	 * X11 wants the icon on the window itself rather than looked up from the
	 * entry, and a themed lookup is the only way to reach the one the package
	 * installed: every other figure this application shows, it paints. Empty
	 * where nothing is installed, which is what an uninstalled build had anyway.
	 */
	QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral(QUICKDC_QUI_APP_ID)));

	/*
	 * Before any widget is built, so nothing is created against the platform
	 * palette and restyled afterwards. The default rather than the stored choice,
	 * which cannot be read until the core is up - and this one has a message box to
	 * style if the core does not come up at all.
	 */
	Qdc::applyTheme(Qdc::defaultTheme());

	/*
	 * Declared before the window so that it outlives it. The window's bridges
	 * unregister themselves from the core as they go, which means the core has
	 * to still be there when the window is destroyed - and destruction runs in
	 * reverse order of declaration.
	 */
	Qdc::CoreThread core;

	/*
	 * Blocks: the core reads the configuration, scans the shares and opens a
	 * listening socket before it is usable. Failing here rather than starting a
	 * window that cannot do anything, because there is nothing a user could do
	 * about it from inside one.
	 */
	if (!core.startAndWait())
	{
		QMessageBox::critical(nullptr, QApplication::applicationName(),
			QApplication::translate("main", "QuickDC could not start.\n\n%1")
				.arg(core.failureReason()));
		return 1;
	}

	install_signal_handlers(&application);

	/*
	 * The chosen scheme, now that there is a configuration to ask - and still before
	 * the window, so it is built against the theme it will be shown in rather than
	 * restyled a moment later.
	 *
	 * The core stores this and ignores it, which is what the interface's own settings
	 * want: one configuration file rather than a second one for a single key.
	 */
	{
		Qdc::ConfigBridge config(&core);

		const QString chosen = config.read(Qdc::Settings::ThemeName);
		Qdc::applyTheme(Qdc::themeByName(chosen.isEmpty() ? Qdc::defaultTheme().name : chosen));
	}

	/* Does nothing unless QDC_THEME_WATCH is set; see watchThemes(). */
	Qdc::watchThemes(&application);

	int status = 0;

	/* Collects what the updater makes ready during the session, for the quit to install. */
	Qdc::UpdateBridge updates(&core);
	bool updatesWanted = true;

	Qdc::ConfigBridge diagnosticsConfig(&core);
	Qdc::Diagnostics diagnostics(&diagnosticsConfig, QStringLiteral(QUICKDC_QUI_VERSION_STRING),
		QStringLiteral(QUICKDC_QUI_CHANNEL));
	QObject::connect(&updates, &Qdc::UpdateBridge::failed, &diagnostics, [&diagnostics](const QString& why) {
		diagnostics.report(QStringLiteral("An update failed: ") + why);
	});

	/*
	 * Hidden: reports this run whatever was chosen, and crashes a few seconds in, to
	 * see a report through from the crash to the server.
	 */
	if (QCoreApplication::arguments().contains(QStringLiteral("--test-crash")))
	{
		diagnostics.startForTest();
		QTimer::singleShot(3000, [] {
			std::fprintf(stderr, "QuickDC: crashing, as --test-crash asked\n");
			volatile int* nowhere = nullptr;
			*nowhere = 1;
		});
	}
	else
	{
		diagnostics.askOnce(nullptr);
		diagnostics.apply();
	}

	/*
	 * The window is scoped so that it is destroyed before the core is stopped.
	 *
	 * Its bridges unregister themselves from the core as they go, and each of them
	 * does so by blocking on a job on the core thread - which only runs while the
	 * core is up. Shutting down first left every one of those jobs refused, so the
	 * sessions and listeners were never unregistered at all.
	 */
	{
		Qdc::MainWindow window(&core);
		window.show();

		status = QApplication::exec();
	}

	/* Read while the core is up: turned off during the session means not installed at its end. */
	updatesWanted = Qdc::ConfigBridge(&core).read(Qdc::Settings::UpdatesEnabled);

	/*
	 * Explicit rather than left to the destructor, so that flushing the download
	 * queue and the hash cache happens here, where a failure could still be
	 * reported, rather than silently on the way out of scope.
	 */
	core.shutdown();

	/* After the core, so nothing of this version is still writing while it is replaced. */
	if (updatesWanted && !updates.readyVersion().isEmpty())
	{
		QString why;
		if (!updates.installOnQuit(&why))
			std::fprintf(stderr, "QuickDC: the update to %s was not installed: %s\n",
				qPrintable(updates.readyVersion()), qPrintable(why));
	}

	return status;
}

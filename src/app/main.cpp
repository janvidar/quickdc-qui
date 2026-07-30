/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/corethread.h>
#include <gui/mainwindow.h>

#include <QApplication>
#include <QMessageBox>
#include <QSocketNotifier>

#include <csignal>
#include <fcntl.h>
#include <unistd.h>

namespace {

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

}

int main(int argc, char** argv)
{
	QApplication application(argc, argv);

	QApplication::setApplicationName(QStringLiteral("QuickDC"));
	QApplication::setApplicationDisplayName(QStringLiteral("QuickDC"));
	QApplication::setOrganizationName(QStringLiteral("QuickDC"));
	QApplication::setDesktopFileName(QStringLiteral("quickdc-qui"));

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

	int status = 0;

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

	/*
	 * Explicit rather than left to the destructor, so that flushing the download
	 * queue and the hash cache happens here, where a failure could still be
	 * reported, rather than silently on the way out of scope.
	 */
	core.shutdown();

	return status;
}

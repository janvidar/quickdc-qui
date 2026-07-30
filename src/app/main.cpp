/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/corethread.h>
#include <gui/mainwindow.h>

#include <QApplication>
#include <QMessageBox>

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

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

	Qdc::MainWindow window(&core);
	window.show();

	const int status = QApplication::exec();

	/*
	 * Explicit, and before the window goes out of scope, so that the queue and
	 * the hash cache are flushed while there is still something to report a
	 * failure to. The destructor would do it, but silently and later.
	 */
	core.shutdown();

	return status;
}

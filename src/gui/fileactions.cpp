/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "fileactions.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

bool Qdc::FileActions::exists(const QString& path)
{
	return !path.isEmpty() && QFileInfo::exists(path);
}

bool Qdc::FileActions::open(const QString& path)
{
	if (!exists(path)) return false;
	return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

bool Qdc::FileActions::remove(const QString& path)
{
	if (!exists(path)) return false;
	return QFile::remove(path);
}

bool Qdc::FileActions::reveal(const QString& path)
{
	if (!exists(path)) return false;

	const QFileInfo info(path);

#if defined(Q_OS_MACOS)
	/*
	 * -R asks the Finder to reveal the file rather than open it, which is exactly
	 * the wanted behaviour and has no portable equivalent.
	 */
	if (QProcess::startDetached(QStringLiteral("open"),
			{QStringLiteral("-R"), info.absoluteFilePath()}))
	{
		return true;
	}
#elif defined(Q_OS_WIN)
	if (QProcess::startDetached(QStringLiteral("explorer"),
			{QStringLiteral("/select,") + QDir::toNativeSeparators(
				info.absoluteFilePath())}))
	{
		return true;
	}
#elif defined(Q_OS_UNIX)
	/*
	 * The freedesktop interface every current file manager implements. Tried
	 * first because it selects the file; a desktop without it falls through to
	 * opening the folder.
	 */
	const bool shown = QProcess::startDetached(QStringLiteral("dbus-send"), {
		QStringLiteral("--session"),
		QStringLiteral("--print-reply"),
		QStringLiteral("--dest=org.freedesktop.FileManager1"),
		QStringLiteral("--type=method_call"),
		QStringLiteral("/org/freedesktop/FileManager1"),
		QStringLiteral("org.freedesktop.FileManager1.ShowItems"),
		QStringLiteral("array:string:")
			+ QUrl::fromLocalFile(info.absoluteFilePath()).toString(),
		QStringLiteral("string:")
	});

	if (shown) return true;
#endif

	/*
	 * The folder, with nothing selected. Not as good, and the only thing that
	 * works everywhere.
	 */
	return QDesktopServices::openUrl(
		QUrl::fromLocalFile(info.absolutePath()));
}

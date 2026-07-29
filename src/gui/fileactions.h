/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_FILEACTIONS_H
#define HAVE_QUICKDC_QUI_FILEACTIONS_H

#include <QString>

namespace Qdc {

/**
 * Handing a finished download over to the desktop.
 *
 * Opening a file is the same everywhere and QDesktopServices does it. Revealing
 * one in its folder is not: there is no portable call for "select this file in
 * the file manager", so each platform gets what it actually supports and the
 * fallback is to open the containing directory - which is worse, because the file
 * is not highlighted, but is never wrong.
 */
namespace FileActions {

/** @return false if the file is gone, or nothing would open it. */
bool open(const QString& path);

/**
 * Show the file in the desktop's file manager, selected if that is possible.
 * @return false if the file is gone.
 */
bool reveal(const QString& path);

/** Delete the file. @return false if it is not there, or could not be removed. */
bool remove(const QString& path);

bool exists(const QString& path);

}

}

#endif // HAVE_QUICKDC_QUI_FILEACTIONS_H

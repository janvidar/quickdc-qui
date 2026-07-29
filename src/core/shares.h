/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_SHARES_H
#define HAVE_QUICKDC_QUI_SHARES_H

#include <QList>
#include <QObject>
#include <QString>

namespace Qdc {

class ConfigBridge;

/** One shared folder: what it is called on the hub, and where it really is. */
struct SharedFolder {
	/**
	 * The name other users see. The core calls this the "fake" name, and it is
	 * what appears at the top level of a file list rather than the real path -
	 * which would otherwise tell everyone the layout of the disk.
	 */
	QString name;

	QString path;
};

/**
 * The folders offered to other users.
 *
 * Stored as the core reads them, in `[Shares]`:
 *
 *     Total shares=1
 *     Share 0=Music:/home/user/Music
 *
 * The colon is the separator, so it is the *first* one that splits the pair - a
 * Windows path contains one and the virtual name must not.
 *
 * Changes here are written to the configuration and take effect when the core next
 * starts. Share::Manager reads this list once in initialize() and keeps
 * addShare() and removeShare() protected, so there is nothing to call at runtime;
 * see TODO.md for what making it live would involve.
 */
class Shares final : public QObject {
	Q_OBJECT

public:
	explicit Shares(ConfigBridge* config, QObject* parent = nullptr);

	QList<SharedFolder> folders() const;
	int count() const;

	/**
	 * Offer a folder.
	 *
	 * @param name what to call it; the folder's own base name is used if this is
	 *        empty. A colon is stripped, since it is the stored separator.
	 * @return false if the path is empty or already shared.
	 */
	bool add(const QString& path, const QString& name = QString());

	bool removeAt(int index);

	/** Rename what a folder is called on the hub. The path is unchanged. */
	bool renameAt(int index, const QString& name);

	/** Whether this path, or a parent of it, is already offered. */
	bool contains(const QString& path) const;

	/** Replace the whole list, in order. */
	void setFolders(const QList<SharedFolder>& folders);

signals:
	void changed();

private:
	void store();

	ConfigBridge* m_config;
	QList<SharedFolder> m_folders;

	/** How many numbered keys are in the file; see Completed for the reasoning. */
	int m_storedCount = 0;
};

}

#endif // HAVE_QUICKDC_QUI_SHARES_H

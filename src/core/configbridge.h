/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_CONFIGBRIDGE_H
#define HAVE_QUICKDC_QUI_CONFIGBRIDGE_H

#include "coretypes.h"

#include <QObject>
#include <QString>

namespace Qdc {

class CoreThread;

/**
 * The configuration, as a settings form sees it.
 *
 * Every call below is one blocking job on the core thread, and that granularity
 * is the point rather than an accident. QuickDC::Preferences has a mutable
 * current group: reading a key means selecting a group and then reading, and if
 * those are two jobs something in between can select a third group and the read
 * comes back from the wrong section. Selecting and reading in the same job is
 * what makes a group argument on every call safe.
 *
 * The settings the core actually acts on are listed in
 * quickdc/core/api/config.h. A key outside that list will be stored and read
 * back faithfully and then ignored by everything.
 */
class ConfigBridge final : public QObject {
	Q_OBJECT

public:
	explicit ConfigBridge(CoreThread* thread, QObject* parent = nullptr);

	/** @param store which of the Core's two preference files to read. */
	QString getString(ConfigStore store, const QString& group, const QString& key,
	                  const QString& defaultValue = QString()) const;
	int getNumber(ConfigStore store, const QString& group, const QString& key,
	              int defaultValue = 0) const;
	bool getBool(ConfigStore store, const QString& group, const QString& key,
	             bool defaultValue = false) const;

	/** Whether the key is present, as distinct from present and empty. */
	bool contains(ConfigStore store, const QString& group, const QString& key) const;

	void setString(ConfigStore store, const QString& group, const QString& key,
	               const QString& value);
	void setNumber(ConfigStore store, const QString& group, const QString& key,
	               int value);
	void setBool(ConfigStore store, const QString& group, const QString& key,
	             bool value);
	void remove(ConfigStore store, const QString& group, const QString& key);

	/**
	 * Write the store out.
	 *
	 * Nothing above this layer is told when the core writes of its own accord,
	 * so a form that has just changed something should call this rather than
	 * assume it will be picked up.
	 *
	 * @return false if the file could not be written, or the core is not running.
	 */
	bool save(ConfigStore store);

	/**
	 * Tell the core that the configuration changed.
	 *
	 * Setting a key changes what is stored, not what is running: the hubs
	 * republish a nickname or description only when they are asked to. This is
	 * the ask. Harmless if nothing changed.
	 */
	void notifyChanged();

private:
	CoreThread* m_thread;
};

}

#endif // HAVE_QUICKDC_QUI_CONFIGBRIDGE_H

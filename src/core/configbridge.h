/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_CONFIGBRIDGE_H
#define HAVE_QUICKDC_QUI_CONFIGBRIDGE_H

#include "coretypes.h"
#include "relay.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QVariantHash>
#include <QPair>
#include <QString>
#include <QStringList>

#include <memory>

class QTimer;

namespace Qdc {

class CoreThread;

/**
 * The configuration, as a settings form sees it.
 *
 * A copy of both preference files, held on the thread that draws. Reading a
 * setting is a lookup in that copy and costs nothing, which is the whole point:
 * every read used to be a blocking job on the core thread, and the core thread is
 * busy for as long as its current turn takes - writing transfer chunks to disk,
 * rewriting the download queue, compressing the share's file list, answering
 * other people's searches out of the whole share index. A page of fourteen
 * settings opened while that was going on waited out fourteen of those turns.
 *
 * Writing goes both ways at once: the copy is changed here, so the interface
 * shows the new value immediately, and the same change is posted to the core,
 * which applies it in its own time. Nothing waits for that either.
 *
 * The settings the core actually acts on are listed in
 * quickdc/core/api/config.h. A key outside that list will be stored and read
 * back faithfully and then ignored by everything.
 */
class ConfigBridge final : public QObject {
	Q_OBJECT

public:
	explicit ConfigBridge(CoreThread* thread, QObject* parent = nullptr);
	~ConfigBridge() override;

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
	 * Rewrite a group: 'removeKeys' cleared, then 'values' written.
	 *
	 * The keys to remove have to be named because the store cannot be asked which
	 * keys a caller wrote last time; a caller keeping a numbered list knows how
	 * long the previous one was.
	 */
	void replaceGroup(ConfigStore store, const QString& group,
	                  const QStringList& removeKeys,
	                  const QList<QPair<QString, QString>>& values);

	/**
	 * Read several of a group's settings at once.
	 *
	 * A convenience rather than an optimisation now that a read costs nothing -
	 * kept because a form that names its keys and their defaults in one table is
	 * easier to read than fourteen separate calls.
	 *
	 * The type of each default decides how the value is read: a string default
	 * reads a string, a number a number, a bool a bool.
	 *
	 * @param wanted the keys to read, each with the default to use when absent.
	 * @return the values, under the same keys.
	 */
	QVariantHash readGroup(ConfigStore store, const QString& group,
	                       const QVariantHash& wanted) const;

	/**
	 * Write the store out, before long.
	 *
	 * Returns at once, and the file is written a moment later - which also folds a
	 * burst of saves into one. Somebody who has just changed a setting should call
	 * this rather than assume the core will pick it up, and watch saveFailed() if
	 * they want to be told when it could not be done.
	 */
	void save(ConfigStore store);

	/**
	 * Tell the core that the configuration changed.
	 *
	 * Setting a key changes what is stored, not what is running: the hubs
	 * republish a nickname or description only when they are asked to. This is
	 * the ask. Harmless if nothing changed.
	 */
	void notifyChanged();

signals:
	/** A store was written. */
	void saved(Qdc::ConfigStore store);

	/** A store could not be written, and the settings on disk are the old ones. */
	void saveFailed(Qdc::ConfigStore store);

	/**
	 * A setting changed.
	 *
	 * Every write reports itself, so anything holding a setting in hand can keep
	 * it current without asking again and without waiting for a save.
	 */
	void changed(Qdc::ConfigStore store, const QString& group, const QString& key);

private:
	typedef QHash<QString, QString> Section;
	typedef QHash<QString, Section> Store;

	/** Fill the copy from the core. The one thing here that waits, and only once. */
	void seed();

	void flush();

	Store& mirror(ConfigStore store);
	const Store& mirror(ConfigStore store) const;

	/** The stored text, or null where there is no such group or key. */
	const QString* find(ConfigStore store, const QString& group,
	                    const QString& key) const;

	/** Change the copy, post the same change to the core, and report it. */
	void write(ConfigStore store, const QString& group, const QString& key,
	           const QString& value);

	/**
	 * Guarded, because the core thread is not this object's to outlive: both are
	 * often children of the same parent, and Qt destroys them in the order they
	 * were made, which puts the thread first.
	 */
	QPointer<CoreThread> m_thread;

	std::shared_ptr<Relay> m_relay;

	Store m_application;
	Store m_session;

	QTimer* m_saveTimer;
	bool m_saveApplication = false;
	bool m_saveSession = false;
};

}

#endif // HAVE_QUICKDC_QUI_CONFIGBRIDGE_H

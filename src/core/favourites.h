/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_FAVOURITES_H
#define HAVE_QUICKDC_QUI_FAVOURITES_H

#include <QObject>
#include <QStringList>

namespace Qdc {

class ConfigBridge;

/**
 * The hubs worth keeping.
 *
 * The core has no notion of a favourite - nothing reads one, nothing connects to
 * one - so this is entirely a user interface concept stored in the core's own
 * configuration for want of anywhere better. It goes in as
 *
 *     [Favourites]
 *     Total=2
 *     Hub 0=adcs://hub.example.org:412
 *     Hub 1=dchub://other.example.org:411
 *
 * which is the shape the core already uses for [Shares], so a hand-edited file
 * stays predictable. Nothing in the core will act on any of it.
 *
 * Held in memory and written through, because the list is short and every caller
 * wants the whole thing.
 */
class Favourites final : public QObject {
	Q_OBJECT

public:
	/** Loads immediately; the list is a handful of strings. */
	explicit Favourites(ConfigBridge* config, QObject* parent = nullptr);

	/** In the order stored, which is the order they were added. */
	QStringList urls() const;

	bool contains(const QString& url) const;

	/**
	 * Adding one that is already there does nothing, so a caller need not check.
	 * @return true if the list changed.
	 */
	bool add(const QString& url);
	bool remove(const QString& url);

	/** @return the state after the toggle: true if it is now a favourite. */
	bool toggle(const QString& url);

signals:
	/** Any change, already persisted. */
	void changed();

private:
	/**
	 * Rewrite the whole group.
	 *
	 * Every key is removed before the current list is written, because the group
	 * is numbered - shortening the list otherwise leaves the tail entries behind
	 * and Total no longer agrees with what is there.
	 */
	void store();

	ConfigBridge* m_config;
	QStringList m_urls;
};

}

#endif // HAVE_QUICKDC_QUI_FAVOURITES_H

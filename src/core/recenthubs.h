/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_RECENTHUBS_H
#define HAVE_QUICKDC_QUI_RECENTHUBS_H

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

namespace Qdc {

class ConfigBridge;

/** One hub that has been connected to, and when. */
struct RecentHub {
	/** Normalised, so that the same hub reached by two spellings is one entry. */
	QString url;

	/** What the hub called itself, if it said so before the session ended. */
	QString name;

	QDateTime visited;
};

/**
 * The hubs that have been visited.
 *
 * Distinct from the favourites, and the distinction is the point: a favourite is
 * something somebody chose to keep, and this is a record of where they have been -
 * which is what makes it useful for getting back to a hub nobody thought to
 * bookmark, and what makes it something to be able to delete.
 *
 * Recorded when a hub is opened rather than when the login succeeds, because a hub
 * that refused the connection is one somebody may well want to find again to see
 * why.
 *
 * The core has no notion of any of this, so it is stored in its configuration for
 * want of anywhere better - the same arrangement as Favourites and Completed:
 *
 *     [Recent hubs]
 *     Total=1
 *     Hub 0=1769000000|adcs://hub.example.org:412|The Example Hub
 *
 * one line per hub: when it was last visited, its address, and its name. The name
 * is last and is not escaped, because a hub may call itself anything at all and
 * nothing else on the line may contain the separator.
 *
 * Newest first, and bounded: this is a shortcut back to somewhere, not a history.
 */
class RecentHubs final : public QObject {
	Q_OBJECT

public:
	explicit RecentHubs(ConfigBridge* config, QObject* parent = nullptr);

	/** Newest first. */
	QList<RecentHub> entries() const;

	int count() const;

	bool contains(const QString& url) const;

	/**
	 * Record a visit, moving the hub to the front.
	 *
	 * @param name what the hub calls itself, if that is known yet. An empty name
	 *        leaves whatever was remembered before in place rather than clearing
	 *        it: the name arrives from the hub some time after the connection, so
	 *        an entry recorded on the way in has none.
	 */
	void note(const QString& url, const QString& name = QString());

	/**
	 * Remember what a hub calls itself, without counting as a visit.
	 *
	 * The name arrives once the hub has been spoken to, which may be a good while
	 * after the connection, and moving the entry then would report the visit as
	 * having happened at that moment.
	 *
	 * @return false if the hub is not in the list.
	 */
	bool setName(const QString& url, const QString& name);

	/** Forget one. @return false if it was not there. */
	bool remove(const QString& url);

	void clear();

	/** How many are kept before the oldest is dropped. */
	static int limit();

signals:
	/** Any change, already persisted. */
	void changed();

private:
	void store();

	ConfigBridge* m_config;
	QList<RecentHub> m_entries;

	/** How many numbered keys are in the file; see Completed for the reasoning. */
	int m_storedCount = 0;
};

}

#endif // HAVE_QUICKDC_QUI_RECENTHUBS_H

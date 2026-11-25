/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_RECENTHUBS_H
#define HAVE_QUICKDC_QUI_RECENTHUBS_H

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

namespace QuickDC { namespace Api { struct Event; } }

namespace Qdc {

class CoreThread;

/** One hub that has been opened, and when. */
struct RecentHub {
	/** Canonical, so that the same hub reached by two spellings is one entry. */
	QString url;

	/** What the hub called itself, if it has said. */
	QString name;

	QDateTime visited;
};

/**
 * The hubs that have been visited, as the core says.
 *
 * Distinct from the favourites, and the distinction is the point: a favourite is
 * something somebody chose to keep, and this is a record of where they have been.
 * The core records a visit when a hub is opened, keeps the name the hub gives
 * itself, orders and bounds the list and stores it; this holds what it last
 * announced and asks it to forget.
 */
class RecentHubs final : public QObject {
	Q_OBJECT

public:
	explicit RecentHubs(CoreThread* thread, QObject* parent = nullptr);

	/** Newest first. */
	QList<RecentHub> entries() const;

	int count() const;

	bool contains(const QString& url) const;

	void remove(const QString& url);
	void clear();

	/** How many the core keeps before the oldest is dropped. */
	static int limit();

	/** Take what the core announced; public so that a test can stand in for it. */
	void take(const QuickDC::Api::Event& event);

signals:
	/** The core said the list changed. */
	void changed();

private:

	CoreThread* m_thread;
	QList<RecentHub> m_entries;
};

}

#endif // HAVE_QUICKDC_QUI_RECENTHUBS_H

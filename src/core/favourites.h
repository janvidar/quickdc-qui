/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_FAVOURITES_H
#define HAVE_QUICKDC_QUI_FAVOURITES_H

#include <QObject>
#include <QStringList>

namespace QuickDC { namespace Api { struct Event; } }

namespace Qdc {

class CoreThread;

/**
 * The hubs worth keeping, as the core says they are.
 *
 * The core keeps the list - canonical addresses, in the order they were added -
 * and stores it; this holds the last list it announced, answers questions about
 * it without asking, and asks the core for changes, which show once it has said
 * so through changed().
 */
class Favourites final : public QObject {
	Q_OBJECT

public:
	explicit Favourites(CoreThread* thread, QObject* parent = nullptr);

	/** In the order stored, which is the order they were added. Canonical. */
	QStringList urls() const;

	/** However @a url is spelled. */
	bool contains(const QString& url) const;

	void add(const QString& url);
	void remove(const QString& url);

	/** Add it if it is not kept, and let it go if it is. */
	void toggle(const QString& url);

	/** Take what the core announced; public so that a test can stand in for it. */
	void take(const QuickDC::Api::Event& event);

signals:
	/** The core said the list changed. */
	void changed();

private:

	CoreThread* m_thread;
	QStringList m_urls;
};

}

#endif // HAVE_QUICKDC_QUI_FAVOURITES_H

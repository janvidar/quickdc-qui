/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_COMPLETED_H
#define HAVE_QUICKDC_QUI_COMPLETED_H

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

namespace QuickDC { namespace Api { struct Event; } }

namespace Qdc {

class CoreThread;

/** One finished download, as remembered. */
struct CompletedDownload {
	QString filename;
	QString tth;
	quint64 size = 0;
	QDateTime finished;
};

/**
 * What has finished downloading, as the core says.
 *
 * The core records each download as it is verified, orders and bounds the list
 * and stores it; this holds what it last announced and asks it to forget.
 */
class Completed final : public QObject {
	Q_OBJECT

public:
	explicit Completed(CoreThread* thread, QObject* parent = nullptr);

	/** Newest first. */
	QList<CompletedDownload> entries() const;

	int count() const;

	/** Forget one, by the filename it went to. Does not touch the file. */
	void remove(const QString& filename);

	/** remove(), and the core deletes the file too. */
	void erase(const QString& filename);

	void clear();

	/** How many the core keeps before the oldest are dropped. */
	static int limit();

	/** Take what the core announced; public so that a test can stand in for it. */
	void take(const QuickDC::Api::Event& event);

signals:
	/** The core said the list changed. */
	void changed();

private:

	CoreThread* m_thread;
	QList<CompletedDownload> m_entries;
};

}

#endif // HAVE_QUICKDC_QUI_COMPLETED_H

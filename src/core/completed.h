/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_COMPLETED_H
#define HAVE_QUICKDC_QUI_COMPLETED_H

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

namespace Qdc {

class ConfigBridge;

/** One finished download, as remembered. */
struct CompletedDownload {
	QString filename;
	QString tth;
	quint64 size = 0;
	QDateTime finished;
};

/**
 * What has finished downloading.
 *
 * The core keeps no history: a QueueFile is deleted the moment its download is
 * verified, and nothing anywhere records that it existed. So the list is kept
 * here, appended to whenever TransferBridge reports a completion, and stored in
 * the core's configuration for want of anywhere better - the same arrangement as
 * Favourites, and equally invisible to the core.
 *
 * Stored as
 *
 *     [Completed]
 *     Total=1
 *     Entry 0=1769000000|734003200|MWXFLX…|/home/user/downloads/film.mkv
 *
 * one line per download: when it finished, how big it was, its hash, and where it
 * went. The path is last and is not escaped, because a path may contain the
 * separator and nothing else may - so the line is split into at most four parts
 * and whatever remains is the path.
 *
 * Newest first, and bounded: a history that grew without limit would eventually
 * be slower to load than it is worth.
 */
class Completed final : public QObject {
	Q_OBJECT

public:
	explicit Completed(ConfigBridge* config, QObject* parent = nullptr);

	/** Newest first. */
	QList<CompletedDownload> entries() const;

	int count() const;

	/**
	 * Remember a download. A repeat of the newest entry is ignored, so a
	 * completion reported twice does not appear twice.
	 */
	void add(const CompletedDownload& entry);

	/** Forget one, by the filename it went to. Does not touch the file. */
	bool remove(const QString& filename);

	void clear();

	/** How many are kept before the oldest are dropped. */
	static int limit();

signals:
	void changed();

private:
	void store();

	ConfigBridge* m_config;
	QList<CompletedDownload> m_entries;

	/**
	 * How many numbered keys are in the file.
	 *
	 * Tracked rather than read back, so that rewriting the group does not need a
	 * round trip just to find out which keys to clear.
	 */
	int m_storedCount = 0;
};

}

#endif // HAVE_QUICKDC_QUI_COMPLETED_H

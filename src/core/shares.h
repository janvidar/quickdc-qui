/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_SHARES_H
#define HAVE_QUICKDC_QUI_SHARES_H

#include <QList>
#include <QObject>
#include <QString>

#include "coretypes.h"

#include <cstdint>

namespace QuickDC { namespace Api { struct Event; namespace Events { struct ShareFolders; } } }

namespace Qdc {

class CoreThread;

/** One shared folder: what it is called on the hub, and where it really is. */
struct SharedFolder {
	/** The name other users see, at the top of a file list in place of the path. */
	QString name;

	QString path;
};

/**
 * The folders offered to other users, as the core says they are.
 *
 * The core keeps the list, decides what may join it and what it is called, stores
 * it and shares a change at once; this holds the last list it announced and asks
 * it for changes. What is asked for here shows once the core has said so, through
 * changed(), and a change it would not make is told through refused() to the
 * Shares that asked.
 */
class Shares final : public QObject {
	Q_OBJECT

public:
	explicit Shares(CoreThread* thread, QObject* parent = nullptr);

	QList<SharedFolder> folders() const;
	int count() const;

	/** @param name what to call it; empty for the folder's own name. */
	void add(const QString& path, const QString& name = QString());

	void removeAt(int index);

	/** Rename what a folder is called on the hub. The path is unchanged. */
	void renameAt(int index, const QString& name);

signals:
	void changed();

	/** A change asked for here was not made. */
	void refused(const QString& path, Qdc::ShareRefusal why);

private:
	void onCoreEvent(const QuickDC::Api::Event& event);
	void adopt(const QuickDC::Api::Events::ShareFolders& told);
	std::uint64_t nextRequest();

	CoreThread* m_thread;
	QList<SharedFolder> m_folders;

	/** Asked for here and not yet answered. */
	QList<std::uint64_t> m_asked;
};

}

#endif // HAVE_QUICKDC_QUI_SHARES_H

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_CORECHANNEL_H
#define HAVE_QUICKDC_QUI_CORECHANNEL_H

#include "coretypes.h"

#include <api/protocol.h>

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <mutex>
#include <optional>

namespace Qdc {

/**
 * The core's events, as a Qt signal, and the settings as the core last said.
 *
 * Its own object, and its own header that only the bridges include, so that the
 * protocol's types stay behind the bridge library and no view can name one.
 * received() is emitted on the core thread: a receiver on any other thread is
 * queued a copy, and one that is destroyed is disconnected by Qt.
 *
 * The settings are kept here rather than in each ConfigBridge so that there is
 * one copy, seeded from a snapshot the core takes as it starts and kept current
 * from the core's SettingChanged events - so a value the core writes itself
 * reaches every reader, and nothing has to wait on the core to read one. This
 * object is the first receiver of its own signal, so by the time any bridge
 * hears of a change the copy already holds it.
 */
class CoreChannel final : public QObject {
	Q_OBJECT

public:
	explicit CoreChannel(QObject* parent = nullptr);

	/** Hand over the stores as they stood. Any thread; the core's, in practice. */
	void offerSnapshot(QuickDC::Api::SettingsSnapshot snapshot);

	/** The value, or null. This object's thread only, as is everything below. */
	const QString* setting(ConfigStore store, const QString& group, const QString& key);

	/**
	 * Write here at once, ahead of the core, and return the number to send the
	 * write under. Until SettingsWritten names it, changes the core reports to
	 * these keys are older than this write and are not applied over it.
	 */
	std::uint64_t writeAhead(ConfigStore store, const QString& group,
	                         const QStringList& removeKeys,
	                         const QList<QPair<QString, QString>>& values);

signals:
	void received(const QuickDC::Api::Event& event);

	/** A setting changed in the core, and not by a write made here. */
	void settingChanged(Qdc::ConfigStore store, const QString& group, const QString& key);

private:
	typedef QHash<QString, QString> Section;

	struct Store {
		QHash<QString, Section> groups;
		std::uint64_t revision = 0;

		/** "group\nkey" to the write that holds it and has not yet been carried out. */
		QHash<QString, std::uint64_t> pending;
	};

	void adoptSnapshot();
	void absorb(const QuickDC::Api::Event& event);
	Store& of(ConfigStore store);

	Store m_application;
	Store m_session;
	std::uint64_t m_nextWrite = 0;

	std::mutex m_offered;
	std::optional<QuickDC::Api::SettingsSnapshot> m_snapshot;
};

}

Q_DECLARE_METATYPE(QuickDC::Api::Event)

#endif // HAVE_QUICKDC_QUI_CORECHANNEL_H

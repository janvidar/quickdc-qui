/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_CORETYPES_H
#define HAVE_QUICKDC_QUI_CORETYPES_H

#include <QDateTime>
#include <QObject>
#include <QString>

namespace Qdc {

Q_NAMESPACE

/**
 * The network side of a hub connection; QuickDC::HubListener::StatusNetwork.
 *
 * Mirrored rather than reused so that nothing above this layer includes a core
 * header. Q_ENUM_NS is what makes the mirror worth having: QMetaEnum can turn
 * these back into their own names, which is most of what a status line needs.
 */
enum class NetStatus {
	Invalid,
	Lookup,
	Connecting,
	Connected,
	Disconnected,
	TlsConnected,
	TlsDisconnected,
	ErrorTimeout,
	ErrorConnectionRefused,
	ErrorHostNotFound,
	ErrorIO,
	TlsError
};
Q_ENUM_NS(NetStatus)

/** Where a hub login has got to; QuickDC::HubListener::StatusHub. */
enum class HubStatus {
	None,
	Login,
	Password,
	LoggedIn,
	ErrorLoginRefused,
	ErrorFull,
	ErrorDisabled,
	ErrorNickNotAccepted,
	ErrorNickTaken,
	ErrorCidTaken,
	ErrorWrongPassword,
	Kicked,
	BannedTemporarily,
	BannedPermanently,
	Redirected
};
Q_ENUM_NS(HubStatus)

/** QuickDC::QueueFile::Priority. */
enum class Priority {
	Paused,
	Low,
	Normal,
	High
};
Q_ENUM_NS(Priority)

/** Which of the Core's two preference stores a setting belongs to. */
enum class ConfigStore {
	/** ~/.quickdc/quickdc.conf; the settings listed in api/config.h. */
	Application,

	/** ~/.quickdc/session.conf; one group per hub URL. */
	Session
};
Q_ENUM_NS(ConfigStore)

/**
 * A copy of a QuickDC::User, taken while the core still owns it.
 *
 * The core hands a listener a User* that it frees as soon as the call returns -
 * EventUserLeave is the clearest case, where the object is deleted immediately
 * afterwards - so a deep copy is the only thing that can safely outlive the
 * callback, let alone cross to another thread.
 */
struct UserInfo {
	QString id;
	QString nick;
	QString description;
	QString email;
	QString speed;
	QString userAgent;
	QString addressIPv4;
	QString addressIPv6;

	/** The certificate this user announced for peer connections; may be empty. */
	QString keyprint;

	quint64 sharedBytes = 0;
	quint32 sharedFiles = 0;

	/** Upload slots the user advertises. Not called 'slots', which Qt defines. */
	quint32 slotCount = 0;

	bool op = false;
};

/** A chat line, public or private, detached from the core's buffers. */
struct ChatMessage {
	/** The hub it arrived on, in canonical "scheme://host:port" form. */
	QString hubUrl;

	/** Empty when the hub itself generated the line; see fromHub. */
	UserInfo from;

	/**
	 * The name the hub gave for a message of its own, which is not a user and
	 * has no UserInfo. Empty for anything a real user said.
	 */
	QString fromHub;

	QString text;

	/**
	 * Which conversation this belongs to: the other party for a private
	 * message, empty for public chat. This is the core's userContextID, and it
	 * is what decides the tab a message lands in.
	 */
	QString context;

	/** Stamped on the core thread as the message arrived. */
	QDateTime received;

	/** A "/me" style line, to be rendered as narration rather than speech. */
	bool action = false;

	bool privateMessage = false;
};

/**
 * One transfer in flight.
 *
 * 'id' is the address of the core's Transfer object carried as an integer,
 * because matching an update against a row already on screen is all it is for.
 * It must not be turned back into a pointer: the transfer it names may well
 * have been deleted by the time another thread reads this.
 */
struct TransferInfo {
	quintptr id = 0;
	QString filename;

	/** Bytes moved of 'length', both counted within this transfer's range. */
	quint64 transferred = 0;
	quint64 length = 0;

	quint64 bytesPerSecond = 0;

	/** Wire bytes over file bytes; 1.0 unless the stream is compressed. */
	double ratio = 1.0;

	bool upload = false;
	bool finished = false;
};

/** A row of the download queue; QuickDC::QueueStatus. */
struct QueueEntry {
	QString filename;
	QString tth;
	quint64 size = 0;
	quint64 done = 0;
	quint32 sources = 0;
	quint32 activeSources = 0;
	Priority priority = Priority::Normal;
	bool complete = false;
};

}

#endif // HAVE_QUICKDC_QUI_CORETYPES_H

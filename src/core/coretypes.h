/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_CORETYPES_H
#define HAVE_QUICKDC_QUI_CORETYPES_H

#include <QDateTime>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <vector>

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

	/** Nothing was reachable: no route, or the network is down. */
	ErrorHostUnreachable,

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

	/**
	 * How the download queue names this user: the client ID on ADC, the nickname
	 * on NMDC.
	 *
	 * This is what a file source records, and so what a browse or a download has
	 * to be asked for by. Distinct from 'id', which is the session identifier and
	 * only means anything while they stay on this hub.
	 */
	QString sourceId;

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

	/**
	 * The text is markdown and should be rendered as such.
	 *
	 * Carried by ADC's RT1 flag. Without it the text is literal, which is what lets
	 * somebody type *asterisks* and have them arrive as asterisks - so this is not a
	 * hint to be guessed at when it is absent.
	 */
	bool richText = false;

	bool privateMessage = false;
};

/**
 * One transfer in flight: one file, rather than one piece of one.
 *
 * The core moves a large file in several chunks at once, each of them a Transfer
 * of its own covering its own byte range, and often from several sources.
 * TransferBridge folds those into one of these per file - see
 * TransferBridge::foldByFile() - because a file is what somebody watching a
 * download is watching, not the ranges it happens to be arriving in.
 *
 * 'id' is a key for matching an update against a row already on screen, and
 * nothing else. For a folded row it stands for the file and the direction. For a
 * single transfer as the core reported it, it is the address of the core's
 * Transfer object carried as an integer, which must not be turned back into a
 * pointer: the transfer it names may well have been deleted by the time another
 * thread reads this.
 */
struct TransferInfo {
	quintptr id = 0;

	/**
	 * A download names the file it will become, rather than the ".part" beside
	 * it that the bytes are actually going into.
	 */
	QString filename;

	/**
	 * Bytes moved of 'length'.
	 *
	 * A download counts the whole file, however much of it the chunks in flight
	 * happen to cover, because that is the figure the queue keeps and the one
	 * worth watching. An upload counts what its chunks cover.
	 */
	quint64 transferred = 0;
	quint64 length = 0;

	/** Added up across the chunks. */
	quint64 bytesPerSecond = 0;

	/** Wire bytes over file bytes; 1.0 unless the stream is compressed. */
	double ratio = 1.0;

	/**
	 * How many of the core's transfers are moving this file right now.
	 *
	 * Zero while it is between chunks: each one is a request of its own, and for
	 * the moment between an answered request and the next there is nothing moving
	 * even though the file is still on its way.
	 */
	quint32 chunks = 1;

	/**
	 * How long there has been nothing moving, in milliseconds.
	 *
	 * Zero for anything with a chunk in flight. A row is given up on once this
	 * passes the grace TransferBridge::foldByFile() allows it.
	 */
	int idleMs = 0;

	bool upload = false;
	bool finished = false;
};

/** One file in someone else's share; Share::RemoteFile. */
struct RemoteFileInfo {
	QString name;

	/** The TTH root in base32, or empty if the list carried none. */
	QString tth;

	quint64 size = 0;
};

/**
 * One directory in someone else's share; Share::RemoteDir.
 *
 * The whole tree is copied out of the core's decoded form, which for a large
 * share is not cheap - a hundred thousand files is tens of megabytes of QString.
 * It is done anyway because the core frees its tree with the RemoteFileList that
 * owns it, and holding that open across the thread boundary would mean the user
 * interface pinning core objects it must not touch.
 *
 * std::vector rather than QList for the children: the member is declared while
 * the type is still incomplete, which std::vector is required to support and
 * QList is not.
 */
struct RemoteDirInfo {
	QString name;

	/** Recursive totals, as the core computed them while decoding. */
	quint64 size = 0;
	quint64 fileCount = 0;

	std::vector<RemoteDirInfo> dirs;
	std::vector<RemoteFileInfo> files;
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

	/**
	 * Whether this file is one of the transfers in flight.
	 *
	 * Not the core's doing: 'activeSources' counts requests, and a file arriving in
	 * chunks has none of those for the moment between one chunk and the next. This
	 * follows the rows TransferBridge::foldByFile() settled on instead, so that a
	 * file counted as transferring stays counted as one for as long as it is
	 * shown that way. TransferBridge::markTransferring() sets it.
	 */
	bool transferring = false;
};

}

/*
 * So that these can travel in a QVariant, which is how an item model hands a row
 * to its delegate. Declared here rather than at each point of use, because a
 * type registered in one translation unit and not another is a linker puzzle
 * nobody should have to solve twice.
 */
Q_DECLARE_METATYPE(Qdc::UserInfo)
Q_DECLARE_METATYPE(Qdc::ChatMessage)
Q_DECLARE_METATYPE(Qdc::TransferInfo)
Q_DECLARE_METATYPE(Qdc::QueueEntry)
Q_DECLARE_METATYPE(Qdc::RemoteFileInfo)
Q_DECLARE_METATYPE(Qdc::RemoteDirInfo)

#endif // HAVE_QUICKDC_QUI_CORETYPES_H

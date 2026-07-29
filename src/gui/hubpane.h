/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_HUBPANE_H
#define HAVE_QUICKDC_QUI_HUBPANE_H

#include "chatpane.h"

class QLabel;
class QListView;

namespace Qdc {

class FilelistBridge;
class FilelistView;
class UserModel;

/**
 * A hub conversation: public chat, the user list beside it, and what is known
 * about the hub.
 *
 * Owns the hub's user list, because nothing else needs it and it has to be kept
 * from the join, update and leave events whether anyone is looking or not - a
 * list rebuilt on becoming visible would be empty until the hub next said
 * something.
 */
class HubPane final : public ChatPane {
	Q_OBJECT

public:
	explicit HubPane(HubBridge* hub, QWidget* parent = nullptr);

	QString paneTitle() const override;

	/** The user list, so the composer's mention completion can follow it. */
	UserModel* users() const;

	void setFavourite(bool favourite);

signals:
	void favouriteToggled(const QString& hubUrl, bool favourite);

	/** A user was chosen from the list; open a conversation with them. */
	void directMessageRequested(const Qdc::UserInfo& user);

protected:
	void sendMessage(const QString& message) override;

private:
	/** Rebuild the Info tab from what the hub has told us. */
	void refreshInfo();

	/** Follow the connection state into the header and the composer. */
	void applyNetStatus(NetStatus status);

	UserModel* m_users;
	QListView* m_userList;
	QLabel* m_info;
};

/**
 * A private conversation, and a way to look at what the other party shares.
 *
 * The Files tab is a browse rather than a live listing: the queue fetches the
 * other party's file list, which takes as long as it takes and may not arrive at
 * all, so the tab has an explicit "fetch" step rather than loading on being
 * opened.
 */
class DmPane final : public ChatPane {
	Q_OBJECT

public:
	/**
	 * @param user who this conversation is with. UserInfo::id is the routing
	 *        context, UserInfo::sourceId is what a browse has to be asked for by.
	 * @param filelists may be null, in which case the Files tab says browsing is
	 *        unavailable rather than offering a button that cannot work.
	 */
	DmPane(HubBridge* hub, const UserInfo& user, FilelistBridge* filelists,
	       QWidget* parent = nullptr);

	QString paneTitle() const override;

	/** The id this conversation is keyed by; ChatMessage::context matches it. */
	QString context() const;

	/** The details, once the hub has sent more than a nickname. */
	void setUser(const UserInfo& user);

signals:
	/** A file was chosen in the Files tab. */
	void downloadRequested(const QString& name, quint64 size, const QString& tth);

protected:
	void sendMessage(const QString& message) override;

private:
	UserInfo m_user;
	FilelistView* m_files;
};

}

#endif // HAVE_QUICKDC_QUI_HUBPANE_H

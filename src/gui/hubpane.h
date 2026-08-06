/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_HUBPANE_H
#define HAVE_QUICKDC_QUI_HUBPANE_H

#include "chatpane.h"

class QLabel;
class QLineEdit;
class QListView;
class QPushButton;

namespace Qdc {

class ConfigBridge;
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
	/**
	 * @param config where this hub's own identity is read and written. May be null,
	 *        in which case the Info tab shows what is known and offers no form.
	 */
	explicit HubPane(HubBridge* hub, ConfigBridge* config = nullptr,
	                 QWidget* parent = nullptr);

	QString paneTitle() const override;

	/** The user list, so the composer's mention completion can follow it. */
	UserModel* users() const;

	void setFavourite(bool favourite);

signals:
	void favouriteToggled(const QString& hubUrl, bool favourite);

	/** A user was chosen from the list; open a conversation with them. */
	void directMessageRequested(const Qdc::UserInfo& user);

	/**
	 * Somebody wants to see what a user shares.
	 *
	 * Distinct from directMessageRequested() only in where it should land: browsing
	 * happens in the conversation with that user, on its Files tab, so whoever
	 * handles this opens the conversation and shows that tab.
	 */
	void filesRequested(const Qdc::UserInfo& user);

protected:
	void sendMessage(const QString& message, bool richText) override;

private:
	/** Rebuild the Info tab from what the hub has told us. */
	void refreshInfo();

	/** What can be done with the user under the pointer. */
	void showUserMenu(const QPoint& where);

	/**
	 * The part of the Info tab that is about who we are on this hub.
	 *
	 * The core keeps a nickname, description, e-mail and password per hub in
	 * session.conf, under a group named by the hub's address, and falls back to the
	 * global identity for anything not set there - so this form is four overrides
	 * rather than four settings.
	 */
	QWidget* buildSessionForm();

	/** Read this hub's group again, discarding anything typed. */
	void loadSession();

	/**
	 * Write the four fields, and offer to reconnect.
	 *
	 * A session reads its identity once, when it is built, so a change reaches the
	 * hub on the next connection and not before. Reconnecting is therefore part of
	 * saving rather than something to be discovered afterwards.
	 */
	void saveSession();

	/** Enable Save once something has been typed. */
	void markSessionEdited();

	/** Follow the connection state into the header, the band and the composer. */
	void applyNetStatus(NetStatus status);

	/** Say across the top why this hub cannot be used, or say nothing. */
	void applyBand();

	/** Let the composer send only once the hub has actually let us in. */
	void applySendable();

	/**
	 * The last of each that was reported.
	 *
	 * Followed from the signals rather than read back off the bridge, because
	 * sendability needs both at once and each signal carries only one of them.
	 */
	NetStatus m_netStatus = NetStatus::Invalid;
	HubStatus m_hubStatus = HubStatus::None;

	ConfigBridge* m_config;

	UserModel* m_users;
	QListView* m_userList;
	QLabel* m_info;

	/* This hub's own identity. Empty means "use the one from Settings". */
	QLineEdit* m_nickname = nullptr;
	QLineEdit* m_description = nullptr;
	QLineEdit* m_email = nullptr;
	QLineEdit* m_password = nullptr;
	QPushButton* m_saveSession = nullptr;
	QLabel* m_sessionStatus = nullptr;

	/** True while loadSession() is filling the fields; see markSessionEdited(). */
	bool m_loadingSession = false;
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

	/**
	 * Whether this conversation is with @a user.
	 *
	 * By client ID before session id, because that is what survives them leaving and
	 * coming back: an ADC session id lasts only as long as they stay on the hub, and
	 * a nickname is theirs to change. On NMDC there is no client ID and the nickname
	 * does both jobs, so the two comparisons are the same one.
	 */
	bool isWith(const UserInfo& user) const;

	/**
	 * Bring the Files tab forward.
	 *
	 * For somebody who asked to browse rather than to talk: the conversation is
	 * where a browse happens, and opening it on the messages would leave them to
	 * find the tab themselves.
	 */
	void showFiles();

signals:
	/** A file was chosen in the Files tab. */
	void downloadRequested(const QString& name, quint64 size, const QString& tth);

	/**
	 * This conversation is now keyed by a different id.
	 *
	 * Emitted when the other party comes back under a new session - a new nickname
	 * on the same client ID is a new session id on ADC - so that whoever files these
	 * conversations by id can move this one rather than opening a second.
	 */
	void contextChanged(const QString& from, const QString& to);

protected:
	void sendMessage(const QString& message, bool richText) override;

private:
	/** As HubPane: a private message goes through the hub and waits on its login. */
	void applySendable();

	/** As HubPane: the same band, for the same hub. */
	void applyBand();

	/** They left, or they are back; drives the composer, the band and a notice. */
	void setPresent(bool present, const QString& nickname);

	NetStatus m_netStatus = NetStatus::Invalid;
	HubStatus m_hubStatus = HubStatus::None;

	/**
	 * Whether they are on the hub as far as we have been told.
	 *
	 * True to begin with: a conversation is opened either from somebody in the user
	 * list or from a message they just sent, and both mean they are there.
	 */
	bool m_present = true;

	UserInfo m_user;
	FilelistView* m_files;
};

}

#endif // HAVE_QUICKDC_QUI_HUBPANE_H

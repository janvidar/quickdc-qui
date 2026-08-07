/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_HUBHEADER_H
#define HAVE_QUICKDC_QUI_HUBHEADER_H

#include <core/coretypes.h>

#include <QWidget>

class QLabel;
class QToolButton;

namespace Qdc {

/**
 * The strip above a conversation: what it is, and what can be done to it.
 *
 * Carries the favourite star, the TLS indicator, a button for the hub's own
 * settings, the user count button that reveals the user list, and a button to
 * leave. A direct message has no user list, no settings of its own and cannot be
 * favourited, so each of those can be hidden - and in the star's place it shows the
 * other party's presence instead, drawn exactly as the user list draws them.
 *
 * The padlock reports what the connection last said, not a live check: it follows
 * HubBridge::secureChanged() and HubBridge::netStatusChanged() and nothing more.
 */
class HubHeader final : public QWidget {
	Q_OBJECT

public:
	explicit HubHeader(QWidget* parent = nullptr);

	/**
	 * The hub's own name, or its address until it has sent one; the other party's
	 * nickname in a direct message.
	 *
	 * @param tooltip what to say on hover, when that is not the title itself - the
	 *        address, for a hub whose name has taken the line.
	 */
	void setTitle(const QString& title, const QString& tooltip = QString());

	/**
	 * The line under the title: what the hub says about itself, or the other
	 * party's description. Optional.
	 */
	void setSubtitle(const QString& subtitle);

	/**
	 * The two lines for a hub, worked out from what it has said about itself.
	 *
	 * The name over the description, and the address until it has said either. The
	 * address also takes the second line when there is a name but no description,
	 * which is not padding: without it the strip is one line tall before a hub logs
	 * in, two afterwards and one again on a hub that never describes itself, and the
	 * conversation below moves each time. It also keeps the address on screen for
	 * the many hubs that say nothing about themselves.
	 */
	void setHubIdentity(const QString& name, const QString& description,
	                    const QString& address);

	/** For tests; what is drawn is otherwise this widget's own business. */
	QString title() const;
	QString subtitle() const;

	void setFavourite(bool favourite);
	bool isFavourite() const;

	void setSecure(bool secure);

	/**
	 * The figure that stands where a hub's star does, for a conversation.
	 *
	 * Same shape and same ink as the row for that person in the user list; see
	 * presence_icon().
	 */
	void setPresence(bool away, bool op);

	/** Also drives the tooltip, and the accessible name. */
	void setUserCount(int count);

	void setNetStatus(NetStatus status);

	/** A direct message has no star, no user list and no settings of its own. */
	void setFavouriteVisible(bool visible);
	void setUserListVisible(bool visible);
	void setSettingsVisible(bool visible);

	/** Show the presence figure where the star would be; see setPresence(). */
	void setPresenceVisible(bool visible);

	/** Whether the settings button is pressed in. Does not report the change. */
	void setSettingsShown(bool shown);

	/** Mark the settings button, because something behind it is unsaved. */
	void setSettingsUnsaved(bool unsaved);

	/** Whether the user list button is currently pressed in. */
	void setUserListShown(bool shown);

signals:
	void favouriteToggled(bool favourite);

	/** The user count button was pressed; the caller decides what that means. */
	void userListToggled(bool shown);

	/** The settings button was pressed. */
	void settingsToggled(bool shown);

	void closeRequested();

protected:
	/** The star and the padlock are pixmaps, so a new scheme has to repaint them. */
	void changeEvent(QEvent* event) override;

private:
	void refreshStar();
	void refreshLock();
	void refreshPresence();
	void refreshSettings();

	QLabel* m_title;
	QLabel* m_subtitle;
	QToolButton* m_star;

	/** The star's place in a conversation, where there is nothing to favourite. */
	QLabel* m_presence;

	QLabel* m_lock;
	QToolButton* m_settings;
	QToolButton* m_users;
	QToolButton* m_close;

	bool m_favourite = false;
	bool m_secure = false;
	bool m_away = false;
	bool m_op = false;
	bool m_settingsUnsaved = false;
	int m_userCount = 0;

	/** What the padlock is about: nothing at all until the hub is connected. */
	NetStatus m_status = NetStatus::Invalid;
};

}

#endif // HAVE_QUICKDC_QUI_HUBHEADER_H

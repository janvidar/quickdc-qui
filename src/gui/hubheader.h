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
 * Carries the favourite star, the TLS indicator, the user count button that
 * reveals the user list, and a button to leave. A direct message has no user list
 * and cannot be favourited, so both of those can be hidden.
 *
 * The padlock reports what the connection last said, not a live check: the core
 * offers no way to ask a hub whether it is secure, so this follows
 * HubBridge::secureChanged() and nothing more.
 */
class HubHeader final : public QWidget {
	Q_OBJECT

public:
	explicit HubHeader(QWidget* parent = nullptr);

	/** The hub address or the other party's nickname. */
	void setTitle(const QString& title);

	/** The hub's own name or topic, shown under the title. Optional. */
	void setSubtitle(const QString& subtitle);

	void setFavourite(bool favourite);
	bool isFavourite() const;

	void setSecure(bool secure);

	/** Also drives the tooltip, and the accessible name. */
	void setUserCount(int count);

	void setNetStatus(NetStatus status);

	/** A direct message has neither a star nor a user list. */
	void setFavouriteVisible(bool visible);
	void setUserListVisible(bool visible);

	/** Whether the user list button is currently pressed in. */
	void setUserListShown(bool shown);

signals:
	void favouriteToggled(bool favourite);

	/** The user count button was pressed; the caller decides what that means. */
	void userListToggled(bool shown);

	void closeRequested();

private:
	void refreshStar();
	void refreshLock();

	QLabel* m_title;
	QLabel* m_subtitle;
	QToolButton* m_star;
	QLabel* m_lock;
	QToolButton* m_users;
	QToolButton* m_close;

	bool m_favourite = false;
	bool m_secure = false;
	int m_userCount = 0;
};

}

#endif // HAVE_QUICKDC_QUI_HUBHEADER_H

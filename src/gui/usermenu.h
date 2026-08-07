/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_USERMENU_H
#define HAVE_QUICKDC_QUI_USERMENU_H

#include <core/coretypes.h>

class QPoint;
class QWidget;

namespace Qdc {

/** What was picked, and the caller has to carry out. */
enum class UserChoice {
	None,
	Talk,
	Browse
};

/** Which entries make sense here, and why the ones that do not are refused. */
struct UserMenuOptions {
	/** False inside the conversation itself, where there is nothing to open. */
	bool offerTalk = true;
	bool offerBrowse = true;

	/**
	 * Why talking and browsing are refused - that they have left the hub, most
	 * often. Empty when they can be done.
	 */
	QString unavailable;
};

/**
 * Open the menu about @a user at @a globalPos, and say what has to happen next.
 *
 * One function rather than three copies, because the same menu is wanted in three
 * places: a name in the feed, a row in the user list, and a conversation in the
 * side navigation. What differs between them is which entries apply, which is what
 * @a options says.
 *
 * Copying the nickname is done here rather than reported back, because it needs
 * nothing the caller has that this does not.
 */
UserChoice showUserMenu(QWidget* parent, const UserInfo& user,
                        const QPoint& globalPos,
                        const UserMenuOptions& options = {});

}

#endif // HAVE_QUICKDC_QUI_USERMENU_H

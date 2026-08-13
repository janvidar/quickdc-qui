/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "usermenu.h"

#include "richtext.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMenu>

Qdc::UserChoice Qdc::showUserMenu(QWidget* parent, const UserInfo& user,
                                  const QPoint& globalPos,
                                  const UserMenuOptions& options)
{
	QMenu menu(parent);

	/*
	 * The nickname across the top, disabled. Which user a menu is about is otherwise
	 * only implied by where it opened, and a menu that opened over the wrong row is
	 * exactly the mistake worth making impossible to miss.
	 */
	const QString who_shown = RichText::displayText(user.nick);

	QAction* who = menu.addAction(who_shown.isEmpty()
		? QObject::tr("This user") : who_shown);

	who->setEnabled(false);
	menu.addSeparator();

	QAction* talk = options.offerTalk
		? menu.addAction(QObject::tr("Send a private message")) : nullptr;

	QAction* files = options.offerBrowse
		? menu.addAction(QObject::tr("Browse files")) : nullptr;

	if (talk || files) menu.addSeparator();

	QAction* copy = menu.addAction(QObject::tr("Copy nickname"));

	/* A private message is routed by the session id, and a browse by the source id. */
	const bool reachable = options.unavailable.isEmpty();

	/*
	 * The reason carries a nickname the caller has already reduced, and a tooltip is
	 * drawn as markup whenever Qt guesses it might be, so it is handed over as markup
	 * to stop it being read as any; see RichText::literalHtml().
	 */
	const QString unavailable = RichText::literalHtml(options.unavailable);

	if (talk)
	{
		talk->setEnabled(reachable && !user.id.isEmpty());
		if (!reachable) talk->setToolTip(unavailable);
	}

	if (files)
	{
		files->setEnabled(reachable && !user.id.isEmpty()
		                  && !user.sourceId.isEmpty());

		if (!reachable)
			files->setToolTip(unavailable);
		else if (user.sourceId.isEmpty())
			files->setToolTip(QObject::tr("The hub has not said enough about them to ask yet."));
	}

	copy->setEnabled(!user.nick.isEmpty());

	QAction* picked = menu.exec(globalPos);

	if (talk && picked == talk) return UserChoice::Talk;
	if (files && picked == files) return UserChoice::Browse;

	if (picked == copy)
	{
		if (QClipboard* board = QGuiApplication::clipboard())
			board->setText(user.nick);
	}

	return UserChoice::None;
}

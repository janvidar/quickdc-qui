/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_CHATPANE_H
#define HAVE_QUICKDC_QUI_CHATPANE_H

#include <core/coretypes.h>

#include "richtext.h"

#include <functional>

#include <QWidget>

class QSplitter;
class QTabWidget;

namespace Qdc {

class ChatFeed;
class ChatModel;
class Composer;
class ConnectionBar;
class HubBridge;
class HubHeader;

/**
 * What a hub conversation and a private one have in common: a header, a feed of
 * lines, and somewhere to type.
 *
 * Lines arrive through post() rather than by subscribing to the hub, because a
 * hub's messages are split across several panes - the public ones belong here, a
 * private one belongs to whichever conversation it names - and only the view that
 * owns all the panes can decide which. Everything genuinely hub-wide, like the
 * user list and the connection state, a subclass subscribes to itself.
 *
 * Unread counting is per pane: a line posted while the pane is not the visible
 * one raises the count, and showing the pane clears it.
 */
class ChatPane : public QWidget {
	Q_OBJECT

public:
	explicit ChatPane(HubBridge* hub, QWidget* parent = nullptr);

	HubBridge* hub() const;
	HubHeader* header() const;

	/** What the secondary navigation should call this conversation. */
	virtual QString paneTitle() const = 0;

	int unreadCount() const;
	void clearUnread();

	/**
	 * Show a line here.
	 *
	 * Raises the unread count unless this pane is the one on screen. Whether the
	 * line belongs here at all is the caller's judgement.
	 */
	void post(const ChatMessage& message);

	/** A note from the client rather than from the hub: an error, a status. */
	void postNotice(const QString& text);

	/**
	 * Put back what was said before this session, oldest first.
	 *
	 * Not post(): nothing here is new, so none of it is unread. Somebody
	 * returning to a conversation they have already read should not be told
	 * there are four thousand things waiting for them in it.
	 */
	void restore(const QList<ChatMessage>& messages);

	/** Put a chosen file into the message being written; see Composer. */
	void insertAttachment(const RichText::Attachment& file);

	/** Who to ask whether a picture is here; see ChatDelegate::setMediaResolver(). */
	void setMediaResolver(std::function<RichText::LocalMedia(const QString& tth)> media);

	/** Something finished downloading, so the messages may draw differently now. */
	void mediaChanged();

signals:
	void unreadChanged(int count);

	/** The header's leave button was pressed. */
	void closeRequested();

	/** A link in a message was clicked; see ChatFeed::linkActivated(). */
	void linkActivated(const QUrl& url);

	/**
	 * Somebody pressed the attach button in this conversation.
	 *
	 * Which file, and what has to happen to it before it can be named in a message, is
	 * decided by whoever owns the connections - see HubsView.
	 */
	void attachRequested();

	/**
	 * A picture was pasted into this conversation's box.
	 *
	 * Carried up for the same reason as attachRequested(): writing it down, hashing it
	 * and offering it are not a text box's business.
	 */
	void imagePasted(const QByteArray& data, const QString& suffix);

	/** Files were dropped into this conversation's box; see Composer::filesDropped(). */
	void filesDropped(const QStringList& paths);

protected:
	/**
	 * Where the composer's text goes. Called with non-empty, trimmed text.
	 *
	 * @param richText the text is markdown, and the message must say so - otherwise
	 *                 the far end shows the markers.
	 */
	virtual void sendMessage(const QString& message, bool richText) = 0;

	/**
	 * Somebody's name in the feed was right-clicked; see ChatFeed.
	 *
	 * What can be done about a person differs between a hub and a conversation
	 * already open with one, so the pane answers rather than the feed.
	 */
	virtual void showUserMenu(const UserInfo& user, const QPoint& globalPos) = 0;

	QTabWidget* tabs() const;
	ChatModel* chatModel() const;
	ChatFeed* feed() const;
	Composer* composer() const;

	/**
	 * The band across the top of the pages, saying why this cannot be used.
	 *
	 * Built here because both kinds of conversation depend on the same hub and go
	 * the same way when it does, but driven by the subclass: the hub-wide state is
	 * what a subclass subscribes to, per the note above.
	 */
	ConnectionBar* connectionBar() const;

	/**
	 * Put a panel beside the conversation - the context sidebar of the layout.
	 *
	 * The Messages page is a splitter from the outset with the conversation as its
	 * only child, so a subclass adds to it rather than taking it apart. Doing it
	 * the other way round meant reparenting a live tab page, which left the tab
	 * widget holding the wrong widget.
	 *
	 * @param width the sidebar's initial width.
	 */
	void addSidebar(QWidget* sidebar, int width);

	void showEvent(QShowEvent* event) override;

private:
	HubBridge* m_hub;
	HubHeader* m_header;
	QTabWidget* m_tabs;
	ConnectionBar* m_connection;
	ChatModel* m_model;
	ChatFeed* m_feed;
	Composer* m_composer;

	/** The Messages page: the conversation, and room for a sidebar. */
	QSplitter* m_messages;

	int m_unread = 0;
};

}

#endif // HAVE_QUICKDC_QUI_CHATPANE_H

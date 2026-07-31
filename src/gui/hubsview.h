/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_HUBSVIEW_H
#define HAVE_QUICKDC_QUI_HUBSVIEW_H

#include "view.h"

#include <core/coretypes.h>

#include <QHash>
#include <QList>

#include <functional>
#include <optional>

class QPushButton;
class QSplitter;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace Qdc {

class ChatPane;
class Composer;
class ConfigBridge;
class Completed;
class CoreThread;
class MediaBridge;
class DmPane;
class Favourites;
class FilelistBridge;
class HubBridge;
class HubPane;
class TransferBridge;

/**
 * Hub chat and direct messages.
 *
 * Two panels: a tree of what is open on the left, and the current conversation on
 * the right. Each hub is a top level row with its direct messages beneath it, so
 * collapsing a hub hides its conversations without closing them.
 *
 * This view also routes chat. A hub bridge reports every line the hub sends,
 * public and private together, and only something that can see all the panes can
 * decide which one a line belongs to - a private message may even be the first
 * sight of a conversation, and opens one.
 */
class HubsView final : public View {
	Q_OBJECT

public:
	/**
	 * @param favourites may be null, in which case the star still toggles but
	 *        nothing is remembered.
	 */
	/**
	 * @param transfers where a file chosen in a browser is queued. May be null,
	 *        and then choosing one does nothing.
	 */
	/**
	 * @param completed what has already been downloaded, so a file that is here
	 *        already is opened rather than queued again. May be null.
	 */
	HubsView(CoreThread* thread, Favourites* favourites,
	         FilelistBridge* filelists, TransferBridge* transfers,
	         ConfigBridge* config, Completed* completed = nullptr,
	         QWidget* parent = nullptr);
	~HubsView() override;

	QString title() const override;
	Glyph glyph() const override;

	/**
	 * Open a hub, or bring the one already open to the front.
	 *
	 * @return false only if the address was unusable.
	 */
	bool openHub(const QString& url);

	/** Close a hub, its conversations, and its connection. */
	void closeHub(const QString& url);

	/** In the order they were opened. */
	QStringList openHubs() const;

	/**
	 * How a hub password is asked for: given the hub's address, return the
	 * password, or nothing if the user declines.
	 *
	 * A seam rather than a hardcoded dialog, so that the asking can be driven by a
	 * test and so a caller may present it differently. The default is a modal
	 * prompt.
	 */
	using PasswordPrompt =
		std::function<std::optional<QString>(const QString& hubUrl)>;

	void setPasswordPrompt(PasswordPrompt prompt);

	int paneCount() const;

	/** Which conversation is showing, or null when none is. */
	ChatPane* currentPane() const;

public slots:
	/** Ask for an address and open it. */
	void promptForHub();

private:
	/**
	 * One hub and everything hanging off it.
	 *
	 * The bridge, the pane and the tree row have the same lifetime, which is why
	 * they are kept together rather than in parallel containers that could
	 * disagree.
	 */
	struct Entry {
		HubBridge* bridge = nullptr;
		HubPane* pane = nullptr;
		QTreeWidgetItem* item = nullptr;

		/** Conversations on this hub, by the context they are keyed on. */
		QHash<QString, DmPane*> dms;

		/**
		 * A password is being asked for.
		 *
		 * A hub may ask more than once - the core sends an empty attempt first and
		 * reports the refusal - and each request arrives as a separate queued
		 * signal, so without this a second dialog would open behind the first.
		 */
		bool prompting = false;
	};

	Entry* entryFor(const QString& url);
	Entry* entryFor(HubBridge* bridge);

	/** Where a line belongs, opening a conversation if it is a new one. */
	void route(Entry& entry, const ChatMessage& message);

	DmPane* openDm(Entry& entry, const UserInfo& user);

	/** Ask for this hub's password and send it, or say why login will fail. */
	void promptForPassword(Entry& entry);

	/** Add a pane to the stack and a row to the tree, and wire its unread count. */
	void adopt(ChatPane* pane, QTreeWidgetItem* item);

	/**
	 * Follow a link somebody clicked in a message.
	 *
	 * A hub address is opened here, in a pane of its own, rather than handed to the
	 * desktop - which would at best start a second copy of QuickDC. Everything else goes
	 * out to the browser.
	 */
	void followLink(const QUrl& url);

	/**
	 * Ask for a file, offer it, and put it in the message that asked.
	 *
	 * The pane is remembered until the hashing finishes, which may be a while for a
	 * large file, and the link goes into that conversation even if another is on screen
	 * by then.
	 */
	void attachFile(ChatPane* pane);

	/**
	 * Hash a chosen file, offer it, and put a link to it in the conversation that asked.
	 *
	 * The pane is held weakly: hashing a large file takes a while, and the conversation
	 * may be closed before it finishes.
	 */
	void offerFile(const QPointer<ChatPane>& pane, const QString& path,
	               bool asMedia);

	/**
	 * Where a file with this hash is on this machine, or empty.
	 *
	 * Somewhere it may be: offered from here, or downloaded earlier. A file that has
	 * since been moved or deleted counts as not here, so the answer can be opened
	 * without checking again.
	 */
	QString localFileFor(const QString& tth) const;

	void showPane(ChatPane* pane);

	/** Sum every pane's unread count into the rail badge. */
	void refreshBadge();

	/** Redraw a row's label, which carries the unread count. */
	void refreshItem(QTreeWidgetItem* item, ChatPane* pane);

	void onTreeSelectionChanged();

	/**
	 * Where a download should land: [Downloads] Directory, or the core's own
	 * default when that is unset. Read each time rather than cached, so a change
	 * in the settings takes effect without restarting.
	 */
	QString downloadDirectory() const;

	CoreThread* m_thread;
	Favourites* m_favourites;
	FilelistBridge* m_filelists;
	TransferBridge* m_transfers;
	ConfigBridge* m_config;
	Completed* m_completed;

	/** Hashes and offers a file somebody attached to a message. Made here, because
	    attaching is not a per hub thing even though it is asked for in a conversation. */
	MediaBridge* m_media = nullptr;

	QSplitter* m_split;
	QTreeWidget* m_tree;
	QStackedWidget* m_stack;
	QWidget* m_empty;
	QPushButton* m_connect;

	QList<Entry*> m_entries;

	PasswordPrompt m_passwordPrompt;

	/** The pane a tree row stands for, so a selection can be resolved. */
	QHash<QTreeWidgetItem*, ChatPane*> m_panes;

	/** And the other way, for closing a pane whose row has to go with it. */
	QHash<ChatPane*, QTreeWidgetItem*> m_items;
};

}

#endif // HAVE_QUICKDC_QUI_HUBSVIEW_H

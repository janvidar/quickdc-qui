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
class RecentHubs;
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
	/**
	 * @param recent where a hub being opened is recorded, so that it can be found
	 *        again without having been bookmarked. May be null, and then nothing is
	 *        remembered.
	 */
	HubsView(CoreThread* thread, Favourites* favourites,
	         FilelistBridge* filelists, TransferBridge* transfers,
	         ConfigBridge* config, Completed* completed = nullptr,
	         RecentHubs* recent = nullptr, QWidget* parent = nullptr);
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
	 * Open again what was open when this last exited, hubs and conversations
	 * both, and connect to each hub.
	 *
	 * The transcripts come back with them, so a conversation carries on rather
	 * than starting over. Does nothing without a ConfigBridge to have recorded it.
	 *
	 * @return whether anything was opened, so that a caller can bring this view
	 *         forward - somebody who left a conversation on screen should find it
	 *         there, not have to go looking for it behind the home page.
	 */
	bool restoreSession();

	/**
	 * Write down what is open, for restoreSession() to put back.
	 *
	 * Called as conversations open and close rather than only on the way out, so
	 * that a session which ends in a crash still comes back.
	 */
	void saveSession();

	/**
	 * The conversations open on a hub, by the id their transcripts are filed
	 * under. In the order they were opened.
	 */
	QStringList openConversations(const QString& hubUrl) const;

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

signals:
	/**
	 * Somebody on one of these hubs answered a search.
	 *
	 * Forwarded rather than the hub bridges being handed out. A search is run from
	 * somewhere else entirely, but replies arrive on the hub they came in on, and
	 * this owns the hubs - so this is where they can be collected without a
	 * HubBridge escaping into a view that has no business holding one.
	 */
	void searchResultsReceived(const QVector<Qdc::SearchHit>& hits);

protected:
	/**
	 * Redraw the tree's glyphs when the colours change.
	 *
	 * A row's icon is a pixmap painted in the theme's ink, and unlike everything
	 * painted from a paintEvent it is not asked for again when the palette moves -
	 * so it would keep the colours of the theme before.
	 */
	void changeEvent(QEvent* event) override;

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

	/**
	 * Open again the conversations recorded against this hub, each with what was
	 * said in it, and each marked as being with somebody not here yet.
	 */
	void restoreConversations(Entry& entry);

	/** The conversations open on a hub, in the order their rows are listed. */
	QList<DmPane*> conversationsOf(const Entry& entry) const;

	/**
	 * A conversation already open with this person, whatever id it is filed under.
	 *
	 * For somebody who left and came back: their session id is new, so the map says
	 * nothing, but the conversation is the same one. Null when there is none.
	 */
	DmPane* dmFor(Entry& entry, const UserInfo& user) const;

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
	 * Write a pasted picture down and offer it, as if it had been chosen from disk.
	 *
	 * A file is what can be offered - the hash names a file, and a peer asks for one -
	 * so the clipboard's bytes have to land somewhere first. They go beside the other
	 * things QuickDC keeps, under a name that says where they came from, and stay there
	 * for as long as the link in the message might be followed.
	 */
	void offerPastedImage(ChatPane* pane, const QByteArray& data, const QString& suffix);

	/**
	 * Offer files that were dropped into a conversation.
	 *
	 * A picture is offered to be shown and everything else to be fetched, decided by
	 * whether this machine can decode it - which is the same question the picture button
	 * asks of what it will accept.
	 */
	void offerDroppedFiles(ChatPane* pane, const QStringList& paths);

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

	/**
	 * Fetch the pictures in an incoming message, where that has been asked for.
	 *
	 * Off unless somebody turned it on, because fetching means asking a stranger's
	 * client for a file the moment their message arrives - so it is a decision per hub,
	 * with a default for hubs nothing has been said about. A picture bigger than the
	 * configured limit is left as a chip to click.
	 */
	void fetchMediaIn(const ChatMessage& message);

	/** Whether pictures are fetched without asking on the hub at 'hubUrl'. */
	bool fetchesMediaOn(const QString& hubUrl) const;

	/**
	 * Offer the choices that belong to one hub rather than to all of them.
	 *
	 * Reached by asking a hub in the list for its menu. Only one thing is decided here
	 * so far - whether pictures sent on this hub are fetched without asking - and it can
	 * either follow the general setting or answer for itself.
	 */
	void showHubMenu(const QPoint& at);

	/**
	 * The same choices a conversation's other party offers anywhere else.
	 *
	 * A conversation in this list names a person, so right-clicking it asks about
	 * them rather than about the hub they are on - which is what the row above is
	 * for.
	 */
	void showDmMenu(QTreeWidgetItem* item, const QPoint& globalPos);

	/** The largest picture that may be fetched without asking, in bytes. */
	quint64 mediaSizeLimit() const;

	/**
	 * Read the settings that every arriving message would otherwise consult.
	 *
	 * Held in hand because a configuration read is a blocking trip to the core thread:
	 * asking on the message path stopped the interface for as long as the core's current
	 * turn took, once per message. Called when something writes the settings, not when
	 * they are wanted.
	 */
	void reloadMediaPolicy();

	/** Remember what one hub has decided for itself, if anything. */
	void loadHubMediaPolicy(const QString& hubUrl);

	/**
	 * Throw away pasted pictures nobody is likely to ask for any more.
	 *
	 * A pasted picture is kept because a link to it in an old message may still be
	 * followed, so the folder only ever grows. There is no way to know when the last
	 * message naming one has scrolled out of somebody's history - nothing keeps
	 * scrollback across restarts - so age is the only measure available.
	 *
	 * The core drops the matching entry by itself the next time it starts: an attachment
	 * whose file has gone cannot be confirmed, so it is forgotten.
	 */
	void sweepPastedMedia();

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
	RecentHubs* m_recent;

	/**
	 * True while restoreSession() is opening what it read.
	 *
	 * Opening a hub records what is open, and doing that halfway through a
	 * restore would write a list shorter than the one still being read from.
	 */
	bool m_restoring = false;

	/** Hashes and offers a file somebody attached to a message. Made here, because
	    attaching is not a per hub thing even though it is asked for in a conversation. */
	MediaBridge* m_media = nullptr;

	/* The settings an arriving message needs, kept in hand; see reloadMediaPolicy(). */
	bool m_fetchMedia = false;
	quint64 m_mediaLimit = 0;
	int m_mediaKeepDays = 0;
	QString m_downloadDir;

	/** Hubs that have decided for themselves, by address. Absent means no opinion. */
	QHash<QString, bool> m_hubFetchMedia;

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

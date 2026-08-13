/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_HUBSVIEW_H
#define HAVE_QUICKDC_QUI_HUBSVIEW_H

#include "view.h"

#include "richtext.h"

#include <core/coretypes.h>

#include <QHash>
#include <QList>
#include <QSet>

#include <functional>
#include <optional>

class QMenu;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace Qdc {

class BoardBridge;
class BoardPane;
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
 * Hub chat, direct messages and bulletin boards.
 *
 * Two panels: a tree of what is open on the left, and the current conversation on
 * the right. Each hub is a top level row with its direct messages and its boards
 * beneath it, so collapsing a hub hides them without closing them.
 *
 * This view also routes chat. A hub bridge reports every line the hub sends,
 * public and private together, and only something that can see all the panes can
 * decide which one a line belongs to - a private message may even be the first
 * sight of a conversation, and opens one.
 *
 * A board is routed the same way and for the same reason, with one rule of its own:
 * a hub describing a board puts a row in the tree and subscribes to nothing. A
 * subscription is a standing instruction to send data, so it is taken out when
 * somebody opens the board and not when the hub mentions it.
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

	/** The boards open on a hub, in the order their rows are listed. */
	QStringList openBoards(const QString& hubUrl) const;

	/**
	 * What this client holds of the boards it has been told about, built the first
	 * time a hub is opened.
	 *
	 * Owned here rather than handed in, as the media bridge is, because a board's
	 * rows hang off the hubs in this tree and nothing else in the window has any use
	 * for one. Every answer it gives arrives as a signal, so a caller watches it
	 * rather than asking it.
	 */
	BoardBridge* bulletins();

	/**
	 * Where the index and the documents are kept; "~/.quickdc" unless this says
	 * otherwise.
	 *
	 * A seam, and only a test has any use for it: the user's own directory is not
	 * somewhere a test may leave a board behind. Has to be called before a hub is
	 * opened, since that is what builds the bridge.
	 */
	void setBoardDirectory(const QString& directory);

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

		/** Boards on this hub, by the name the hub gave them. */
		QHash<QString, BoardPane*> boards;

		/**
		 * The boards somebody has opened here, which is what asks for a subscription.
		 *
		 * The wish rather than the state, and it outlives a connection - a
		 * subscription ends with the session and has to be asked for again, and this
		 * is what remembers which ones to ask for. A board whose row was closed is
		 * not in here: leaving a board is how somebody says they have finished with
		 * it.
		 */
		QSet<QString> joined;

		/**
		 * The boards a subscription has been asked for on this connection.
		 *
		 * Dropped when the hub says it has no boards, because that cancels every
		 * subscription on the connection and is the only thing that says so - and
		 * dropped again when it says it has them, since a session that has just
		 * begun holds none either.
		 */
		QSet<QString> subscribed;

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

	/**
	 * What a descriptor says: a board that is new here, one that has changed, or one
	 * that is gone.
	 *
	 * Nothing is subscribed to and nothing is fetched. A descriptor says what there
	 * is to subscribe to and no more, and a hub sends one for every board a session
	 * may see - so acting on it would take out a subscription per board on every
	 * login, for boards nobody has looked at.
	 */
	void describeBoard(Entry& entry, const BoardInfo& board);

	/** A row and a pane for a board, and nothing said to the hub. */
	BoardPane* openBoard(Entry& entry, const QString& board);

	/**
	 * Open again the boards recorded against this hub, with what is cached in each.
	 *
	 * The rows come back and the subscriptions do not: what was open says where
	 * somebody was reading, and reading is what asks the hub to go on sending.
	 */
	void restoreBoards(Entry& entry);

	/**
	 * Ask the hub for a board's entries, from the cursor the cache holds.
	 *
	 * Through the board bridge rather than the hub, because the cursor is on disk
	 * and the disk belongs to the core thread - and because resuming means from the
	 * highest timestamp held and never from one past it: timestamps are seconds and
	 * are not unique, so a post accepted in the same second as the last one received
	 * would fall behind a cursor advanced past it and reach nobody.
	 *
	 * Asked whether or not the hub has answered for its boards yet. The command is
	 * dropped where it has none, and the feature arriving is what asks again.
	 */
	void subscribeBoard(Entry& entry, const QString& board);

	/** Show a board, which is also what asks the hub to start sending it. */
	void showBoard(BoardPane* pane);

	/**
	 * Take a board's row and its pane away.
	 *
	 * What a descriptor with the removal flag leaves behind, and what leaving a
	 * board does. The documents are not touched: they are named by hash, they were
	 * verified against it, and a board being gone from a hub says nothing about them.
	 */
	void closeBoard(Entry& entry, const QString& board);

	/** The hub a board pane belongs to, or null. */
	Entry* entryForBoard(BoardPane* pane);

	/** The pane for one board on one hub, by the key its entries arrive under. */
	BoardPane* boardPaneFor(const QString& hubKey, const QString& board) const;

	/**
	 * The key a board pane's entries and its read state are filed under.
	 *
	 * Empty for a pane whose hub has no bridge, which is also the answer that keeps a
	 * mark from being written under a hub that does not exist.
	 */
	QString hubKeyOf(BoardPane* pane) const;

	/** Which panes hold an entry for this hash, wherever it was indexed. */
	QList<BoardPane*> panesHolding(const QString& tth) const;

	/**
	 * Note that a post has been opened in the reading pane.
	 *
	 * Read means read: opened by somebody, not fetched. A post whose body could not
	 * be had was still read, and one fetched ahead of a reader was not.
	 *
	 * Handed to the bridge, which writes it down where it survives a restart. A board
	 * nobody has opened for a week has to still say so when the client comes back, so
	 * nothing here keeps a read set of its own.
	 */
	void markRead(BoardPane* pane, const QString& tth);

	/**
	 * Read a post whose document is already on this machine, at once and locally.
	 *
	 * A body is never fetched without somebody asking, and that is a rule about what a
	 * request tells other people: it goes to whoever holds the document, which at first
	 * is only its author, so a fetch nobody asked for is a census of a post's readers.
	 * Reading a file off this disk is visible to nobody, so a post that is here simply
	 * appears when it is opened - including the one this user wrote and the hub has
	 * handed back, which nothing should have to be pressed to see.
	 *
	 * A post whose document is not here is left exactly as it was, waiting to be asked
	 * for. Asked of the bridge out of memory, because this happens while the pane is
	 * drawing the post.
	 */
	void readHeldPost(BoardPane* pane, const QString& hubKey, const QString& tth);

	/** Posts held on a board that nobody has opened. */
	int unreadOn(BoardPane* pane) const;

	/**
	 * The choices that belong to one board rather than to the hub it is on.
	 *
	 * Reached by asking a board row for its menu. A board is not a conversation and
	 * not a hub, so it offers neither of their menus: what can be decided here is
	 * what has been read, what to read again, and whether to go on receiving it.
	 */
	void showBoardMenu(QTreeWidgetItem* item, const QPoint& globalPos);

	/**
	 * Compose a post document, offer it, and submit it once it has a hash.
	 *
	 * In that order, and it cannot be any other: the hash covers the header and the
	 * body together, so it is not known until the last byte is written - which is
	 * also why a post names its parent rather than the other way about. The document
	 * is offered by its hash before the hub is told about it, because until somebody
	 * has read it the author is its only source and an author who submits a post and
	 * disconnects has published nothing.
	 */
	void submitPost(Entry& entry, BoardPane* pane, const QString& parent,
	                const QString& subject, const QString& body, bool richText);

	/**
	 * Keep the document of a post composed here, now that its entry has come back.
	 *
	 * The store is what a reader is answered from, and a post nobody could read back
	 * would be an odd thing to have written. Handed a copy, because adopting a
	 * document takes the file it was given and this client is still offering the
	 * original.
	 */
	void keepComposed(const QString& hubKey, const BoardEntry& entry);

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
	 *
	 * A conversation and a board both ask for this and neither is the other's kind of
	 * widget, so the pane is taken as what they have in common and what to do with the
	 * answer is decided when it arrives.
	 */
	void attachFile(QWidget* pane);

	/**
	 * Write a pasted picture down and offer it, as if it had been chosen from disk.
	 *
	 * A file is what can be offered - the hash names a file, and a peer asks for one -
	 * so the clipboard's bytes have to land somewhere first. They go beside the other
	 * things QuickDC keeps, under a name that says where they came from, and stay there
	 * for as long as the link in the message might be followed.
	 */
	void offerPastedImage(QWidget* pane, const QByteArray& data, const QString& suffix);

	/**
	 * Offer files that were dropped into a conversation.
	 *
	 * A picture is offered to be shown and everything else to be fetched, decided by
	 * whether this machine can decode it - which is the same question the picture button
	 * asks of what it will accept.
	 */
	void offerDroppedFiles(QWidget* pane, const QStringList& paths);

	/**
	 * Hash a chosen file, offer it, and put a link to it in whatever asked.
	 *
	 * The pane is held weakly: hashing a large file takes a while, and the conversation
	 * or the board may be closed before it finishes.
	 */
	void offerFile(const QPointer<QWidget>& pane, const QString& path,
	               bool asMedia);

	/**
	 * Say something to whoever asked for a file, wherever they asked from.
	 *
	 * A conversation has somewhere to put a line like this and a board has not - a
	 * board's bands are for what the hub said, and a file on this machine that could
	 * not be hashed is nothing the hub has any part in.
	 */
	void tellPane(QWidget* pane, const QString& text);

	/**
	 * Where a file with this hash is on this machine, or empty.
	 *
	 * Somewhere it may be: offered from here, or downloaded earlier. A file that has
	 * since been moved or deleted counts as not here, so the answer can be opened
	 * without checking again.
	 */
	QString localFileFor(const QString& tth) const;

	/**
	 * The same as a picture, where the file is one and is here.
	 *
	 * What a conversation and a board both ask while they are drawing, and the reason
	 * neither of them can answer it: it is a question about what this client holds - what
	 * was offered from here and what has finished downloading - rather than about
	 * anything the message or the document said.
	 */
	RichText::LocalMedia localMediaFor(const QString& tth) const;

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

	/** The same for the boards: read once when something writes the settings. */
	void reloadBoardPolicy();

	/** Hand one hub's board settings to the bridge that acts on them. */
	void applyBoardPolicy(const QString& hubUrl);

	/** Whether the rest of a thread is fetched when a post on this hub is opened. */
	bool prefetchesOn(const QString& hubUrl) const;

	/**
	 * Whether posts read on this hub are answered for by hash.
	 *
	 * What makes a board durable - a popular post ends up held by everyone who has
	 * read it and outlives its author - and a statement to anybody who asks for one:
	 * *I have read this*. So a hub nobody has bookmarked answers for nothing until
	 * somebody says otherwise on that hub. Visiting a hub once is not joining it, and
	 * the extension asks for the difference to be kept.
	 */
	bool servesOn(const QString& hubUrl) const;

	/**
	 * A per hub choice with three answers: follow the general setting, always, or
	 * never.
	 *
	 * Written into the hub's own group in the session store, where an absent value is
	 * how a hub says it has no opinion - so "follow the general setting" removes the
	 * key rather than writing false.
	 */
	void addHubChoice(QMenu& menu, const QString& title, const QString& hubUrl,
	                  const QString& key, const std::function<void()>& applied);

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

	/**
	 * The same for a board row.
	 *
	 * Its own rather than a branch in the one above, because the two share the label
	 * and nothing else: a board has no connection of its own to hang a padlock on,
	 * and what it counts is posts nobody has opened rather than lines nobody has
	 * seen.
	 */
	void refreshBoardItem(QTreeWidgetItem* item, BoardPane* pane);

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

	/**
	 * The boards, the documents, and the cursor each subscription resumes from.
	 *
	 * Built on the first hub being opened rather than in the constructor: it makes
	 * directories and reads them on the core thread, and a window with no hub in it
	 * has nothing to keep.
	 */
	BoardBridge* m_bulletins = nullptr;

	/** Where that bridge keeps what it holds; see setBoardDirectory(). */
	QString m_boardDirectory;

	/* The board settings, kept in hand for the same reason the media ones are. */
	bool m_prefetchPosts = false;
	bool m_servePosts = true;
	quint64 m_postKeepSeconds = 0;
	quint64 m_postCacheBytes = 0;

	/** Hubs that have decided either of those for themselves. */
	QHash<QString, bool> m_hubPrefetch;
	QHash<QString, bool> m_hubServe;

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

	/**
	 * The same two for the board rows, beside the maps above rather than in them.
	 *
	 * Everything that reads m_panes takes what it finds for a conversation - it sums
	 * unread lines, asks for a pane title, casts to a conversation, tells each one
	 * that a download finished - and a board is none of those things. Widening the
	 * map would have every one of those readers ask what kind of pane it had before
	 * doing anything with it.
	 */
	QHash<QTreeWidgetItem*, BoardPane*> m_boards;
	QHash<BoardPane*, QTreeWidgetItem*> m_boardItems;

	/**
	 * Documents composed here whose entry has not come back, by hash.
	 *
	 * There is no success message in this protocol - the entry is the acknowledgement
	 * - so this is what recognises one's own post arriving, and the file is what is
	 * being offered to whoever reads it first.
	 */
	QHash<QString, QString> m_composed;
};

}

#endif // HAVE_QUICKDC_QUI_HUBSVIEW_H

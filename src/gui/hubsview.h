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
 * A board is drawn from what the core says: it follows every board a hub describes,
 * and each one it follows has a row under its hub. What is fetched, offered and
 * kept of a board's posts is the core's to decide.
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
	bool openHub(const QString& url, bool remember = true);

	/** Close a hub, its conversations, and its connection. */
	void closeHub(const QString& url);

	/**
	 * Stop following one board on a hub, as somebody choosing to leave it does.
	 *
	 * The core is told, the row goes, and the core leaves the board for as long as the
	 * connection lasts, so the next descriptor does not put it straight back.
	 *
	 * Distinct from the board being removed by the hub, which takes the same row away
	 * and is not somebody saying they have finished with it: one that goes and comes
	 * back comes back.
	 */
	void leaveBoard(const QString& url, const QString& board);

	/** In the order they were opened. */
	QStringList openHubs() const;

	/**
	 * Open again what was open when this last exited, hubs and conversations
	 * both, and connect to each hub.
	 *
	 * The transcripts come back with them, so a conversation carries on rather
	 * than starting over. Does nothing without a ConfigBridge to have recorded it.
	 *
	 * The hub this program runs itself is opened first and is not counted among them,
	 * its row being there because the setting says so - which is not by itself a
	 * reason to bring this view forward over whatever the window is showing.
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
	 *
	 * All the hubs but the one this program runs itself. That row is there because the
	 * setting says so, so recording it here would have it opened again at the next
	 * start whether or not the setting still said so - and then closed a moment later,
	 * which reads as a hub that failed. What is open *on* it is still written down,
	 * under its own address like any other hub's.
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
	 * What the core says of the boards, built the first time it is wanted. Owned here
	 * because a board's rows hang off the hubs in this tree and nothing else in the
	 * window has any use for one.
	 */
	BoardBridge* bulletins();

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
	 * What the core said of a board on this hub: a row for one its hub described that
	 * has none, and what its hub says it is for one that has.
	 */
	void boardState(Entry& entry, const BoardInfo& board, bool described, bool left);

	/** A row and a pane for a board, and nothing said to the hub. */
	BoardPane* openBoard(Entry& entry, const QString& board);

	/** Open again the boards recorded against this hub, with what is held of each. */
	void restoreBoards(Entry& entry);

	/** Show a board, which takes it up again where it had been left. */
	void showBoard(BoardPane* pane);

	/**
	 * Take a board's row and its pane away.
	 *
	 * What a board its hub says is gone leaves behind, and what leaving a board
	 * does. The documents are not touched: they are named by hash, they were
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

	/**
	 * This installation's own client id, trimmed, or empty before there is one.
	 *
	 * Read rather than held, because the identity page can change it while a board is
	 * open, and a stale copy would decide which posts are this user's own.
	 */
	QString ownCid() const;

	/** Which panes hold an entry for this hash, wherever it was indexed. */
	QList<BoardPane*> panesHolding(const QString& tth) const;

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

	/** Have the core write a post and submit it; a refusal is said in a message box. */
	void submitPost(Entry& entry, BoardPane* pane, const QString& parent,
	                const QString& subject, const QString& body, bool richText);

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
	 * Hand a pasted picture to the core, which keeps it among its media and offers it,
	 * and put it in the message once it is offered.
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

	/** Put 'path' in the message once the core says it is offered, or say why not. */
	void awaitOffer(const QPointer<QWidget>& pane, const QString& path, bool asMedia);

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
	 * Offer the choices that belong to one hub rather than to all of them.
	 *
	 * Reached by asking a hub in the list for its menu: whether pictures sent on this hub
	 * are fetched without asking, and what it does with boards, each of which can
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

	/** Read the download directory when something writes it, not when it is wanted. */
	void reloadDownloadDirectory();

	/**
	 * Open, close or move the row for the hub this program runs itself.
	 *
	 * That row is there because the setting says so rather than because anybody
	 * opened it, so this is what decides it: called when the window comes up, and
	 * again whenever the switch or the port the hub answers on is written - that port
	 * living with the client's own network settings because it is the same port.
	 *
	 * The address follows the client's own listening port, so a change to that port
	 * moves the hub rather than leaving a row pointing at nothing. The old row is
	 * closed even when there is a new one to open: the two are the same hub answering
	 * elsewhere, and a list with both in it would say this computer runs two.
	 * 'm_localHubUrl' is cleared before the old row is closed, so that what closeHub()
	 * writes down is already the truth, and set before the new hub is opened, because
	 * opening one draws its row - and a row drawn before it was set would be drawn as
	 * somebody else's hub. What was open on the hub is put back here, because
	 * restoreSession() does that for the hubs it opens and does not open this one.
	 */
	void reloadLocalHub();

	/**
	 * Where the built-in hub answers: this client's own port, on the loopback.
	 *
	 * The loopback rather than this machine's address on the network, and plaintext
	 * rather than TLS: the connection never leaves the computer, so there is nothing
	 * on the way to protect it from and no certificate anybody could check.
	 */
	QString localHubUrl() const;

	/**
	 * A per hub choice with three answers: follow the general setting, always, or
	 * never.
	 *
	 * Ticked from the hub's own group in the session store, 'key' there, and handed
	 * to 'decide' to have the core write: the answer, or none for following the
	 * general setting.
	 */
	void addHubChoice(QMenu& menu, const QString& title, const QString& hubUrl,
	                  const QString& key, const std::function<void(std::optional<bool>)>& decide);

	void showPane(ChatPane* pane);

	/** Sum every pane's unread count into the rail badge. */
	void refreshBadge();

	/**
	 * Redraw a row's label, which carries the unread count.
	 *
	 * The hub this program runs itself is drawn as a machine rather than as the speech
	 * bubbles every other hub gets, and said in the tooltip as well, because a shape
	 * says which row and only words say why. Which row that is comes from the address
	 * rather than from anything the row carries, so that the mark follows the setting;
	 * see 'm_localHubUrl'.
	 */
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

	/**
	 * True while restoreSession() is opening what it read.
	 *
	 * Opening a hub records what is open, and doing that halfway through a
	 * restore would write a list shorter than the one still being read from.
	 */
	bool m_restoring = false;

	/** Hashes and offers a file somebody attached to a message. Made here, because
	    attaching is not a per hub thing even though it is asked for in a
	    conversation - and at construction, because it also answers "is this picture
	    already ours", which is asked long before anything is attached. */
	MediaBridge* m_media = nullptr;

	/* See reloadDownloadDirectory(). */
	QString m_downloadDir;

	/** Where the core said a picture a message showed is, by hash. */
	QHash<QString, QString> m_mediaHere;

	/** What the core says of the boards; built when first wanted. */
	BoardBridge* m_bulletins = nullptr;

	/**
	 * The address of the hub this program runs, while it is running one.
	 *
	 * Empty when the built-in hub is switched off, which is also what says that no row
	 * in the tree is that hub - a row is marked by comparing against this rather than
	 * by carrying anything of its own, so that the mark follows the setting and cannot
	 * be left behind on a row after the hub it named has gone.
	 */
	QString m_localHubUrl;

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
};

}

#endif // HAVE_QUICKDC_QUI_HUBSVIEW_H

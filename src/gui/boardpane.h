/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_BOARDPANE_H
#define HAVE_QUICKDC_QUI_BOARDPANE_H

#include <core/coretypes.h>

#include "richtext.h"

#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTextBrowser;
class QTimer;
class QTreeView;
class QUrl;

namespace Qdc {

class BoardModel;
class Composer;

/**
 * One bulletin board: the threads on the left, the post being read on the right.
 *
 * Nothing here talks to a hub. Entries, descriptors, bodies and refusals arrive through
 * the slots below, and everything a reader asks for leaves as a signal - so the pane can
 * be driven through its own interface, which is also how each of the six states a post
 * can be in is put on the screen by a test without a hub, a socket or a thread.
 *
 * The pane's one job is to be honest about how much is known. An index entry is the
 * hub's description of a document that may not exist, and until the bytes have been
 * fetched and hashed the subject on the screen is *the hub's* subject line: so nothing
 * is fetched merely because an entry arrived, and a post that has not been read is not
 * drawn as though it had been - there are no words on the screen and something to press
 * instead. What a state changes is what is there to read, and never a word beside the
 * post about how far it has been checked.
 *
 * Three further rules are the specification's and are visible in what this draws:
 *
 * - The button says "Withdraw" and never "Delete". What it does is take the entry out of
 *   this hub's index and ask other clients to drop their copies; it cannot promise the
 *   document is gone, because whoever has read it holds it and goes on serving it. A
 *   button offering to delete a post makes a promise this extension cannot keep, which
 *   is why the word is withdraw throughout - and the confirmation still says what
 *   happens rather than leaving the word to carry it.
 * - Nothing is published until its entry comes back. There is no success message in this
 *   protocol - the entry is the acknowledgement - so a submitted post is described as
 *   handed to the hub and nothing more, and what was typed is kept in case it is refused.
 * - A CID is never presented as a verified identity and a nickname is never used to
 *   attribute a post. A post is attributed to the hub's claim, always, and the author
 *   the document claims is drawn nowhere at all - including where the two disagree,
 *   which is shown as nothing: the honest case of a disagreement is a post relayed to a
 *   second hub by somebody who did not write it, it is indistinguishable from a forgery,
 *   and neither hiding such a post nor asking a reader to adjudicate it is something
 *   this machine can do anything with.
 */
class BoardPane final : public QWidget {
	Q_OBJECT

public:
	explicit BoardPane(QWidget* parent = nullptr);

	/** The index behind the thread list. */
	BoardModel* threads() const;

	/** The descriptor last given, whose permissions decide what is offered. */
	const Qdc::BoardInfo& boardInfo() const;

	QString board() const;

	/** The root of the post the reading pane is showing, or empty. */
	QString reading() const;

	/**
	 * Put a chosen file into the post being written, as a link to its hash.
	 *
	 * The answer to attachRequested(), exactly as in a conversation - and warned about
	 * here, where it is not in a conversation: an attachment to a chat message is fetched
	 * within minutes by the people present, while an attachment to a post may be asked
	 * for years later by somebody who has just read it.
	 */
	void insertAttachment(const RichText::Attachment& file);

	/**
	 * Who to ask whether a picture a body names is on this machine.
	 *
	 * Only a file that is already here is ever drawn, exactly as in a conversation:
	 * nothing is fetched because a post mentions it, and a picture nobody holds stays a
	 * chip to click. Left unset every attachment is a chip, which is also what a post
	 * whose attachments can no longer be had must look like - the body still renders and
	 * the reference is still shown.
	 */
	void setMediaResolver(std::function<RichText::LocalMedia(const QString& tth)> media);

public slots:
	/**
	 * Something finished downloading, so the post being read may draw differently now.
	 *
	 * A conversation needs this because a picture arrives minutes after the message; a
	 * post needs it more, because an attachment to a post may be fetched years after it
	 * was written - and until it is asked for again, the post on the screen is the only
	 * place a reader would see it appear.
	 */
	void mediaChanged();

	/**
	 * What the hub says this board is.
	 *
	 * The permissions are what to offer a reader and not a boundary: the hub re-checks
	 * every operation as it arrives, and one that this appeared to allow may still come
	 * back refused - which is what reportRefusal() is for.
	 */
	void setBoard(const Qdc::BoardInfo& board);

	void addEntry(const Qdc::BoardEntry& entry);
	void addEntries(const QVector<Qdc::BoardEntry>& entries);

	/**
	 * A document that was fetched, hashed and read.
	 *
	 * The state on the post is the caller's finding, with one exception: where the
	 * document's author disagrees with the index entry's, the post is drawn as disputed
	 * whatever it arrived as. The pane holds both halves and can see the disagreement
	 * itself, and drawing it as plainly verified would attribute the post to the
	 * document's claim - which is the one thing that must not be done with it.
	 */
	void setPost(const Qdc::BoardPost& post);

	/**
	 * Nobody who has the document could be reached.
	 *
	 * Not an error and not an empty post, and never a reason to drop the entry: the
	 * document may be on a machine that is switched off today, so the entry is kept with
	 * its subject, its author and its date, and can be asked for again.
	 */
	void setPostUnavailable(const QString& tth, const QString& reason);

	/**
	 * The document is here and it is not the post the entry named.
	 *
	 * The bytes did not hash to the name, or they did and the header did not parse.
	 * Nothing about the post can be shown after that - not even the hub's subject line,
	 * which is a description of a document that has now been read and was something
	 * else - so the post is taken off the board altogether; see BoardModel::listable(),
	 * which is the one place that decision is made.
	 */
	void setPostUnverifiable(const QString& tth);

	/**
	 * The hub refused something.
	 *
	 * This extension has no token, so a refusal is matched to what it refuses by the
	 * hash or the board it names. It is reported in the pane either way: a refusal that
	 * names nothing recognisable is still the hub declining to do what somebody asked,
	 * and swallowing it would leave them watching for an entry that will never arrive.
	 *
	 * @param code the status code, drawn as itself so that a reader can look it up.
	 * @param description the hub's own words, which are a stranger's text.
	 */
	void reportRefusal(int code, const QString& description,
	                   const QString& tth = QString(), const QString& board = QString());

	/**
	 * This view of the board has a gap that cannot be closed.
	 *
	 * What a hub that will not replay from far enough back leaves behind. The board is
	 * still usable and is still shown; it is not complete, and only saying so keeps it
	 * from looking as though it is.
	 */
	void setGap(bool gapped);

	/**
	 * Whose posts on this board are this user's own.
	 *
	 * The client id this installation posts under. A board carries the client id the
	 * hub accepted each post from, so with this the pane can tell which posts are the
	 * user's - which is what the difference between withdrawing one's own post and
	 * withdrawing anybody's is about. Empty until there is one, and a post is nobody's
	 * own while it is.
	 */
	void setOwnCid(const QString& cid);

	/** Show a post in the reading pane, selecting its row if it has one. */
	void read(const QString& tth);

	/** Forget the board: every entry, and whatever was being read. */
	void clear();

signals:
	/**
	 * Somebody asked to read a post, so its document has to be fetched.
	 *
	 * Never emitted on an entry arriving. A body is fetched on a reader's action
	 * because fetching one tells whoever serves it that it has been read, and on a
	 * board that census is systematic rather than incidental.
	 */
	void bodyRequested(const QString& tth);

	/** Somebody asked for the parent of an orphaned reply, by its hash. */
	void parentRequested(const QString& tth);

	/**
	 * The reading pane is showing a different post, by whatever route.
	 *
	 * What a client has to hear to know a post has been read, and it is a change of what
	 * is on the screen rather than a click: a selection, a call to read(), and the board
	 * being cleared all reach it, while choosing the same post again does not. Empty
	 * where nothing is being shown any more.
	 *
	 * Read means read. This says a post was opened and says nothing about whether its
	 * document was ever fetched - see bodyRequested(), which is the other half and is a
	 * request that goes out on the network where this stays here.
	 */
	void readingChanged(const QString& tth);

	/**
	 * A new thread, and a reply to a post.
	 *
	 * The subject and the body as they were typed, unreduced: they are about to become
	 * the bytes of a document and the hash of those bytes is the post's name, so nothing
	 * on the way out may be tidied. @a richText says the body is the markdown subset and
	 * that the document must say so.
	 */
	void threadSubmitted(const QString& subject, const QString& body, bool richText);
	void replySubmitted(const QString& parent, const QString& subject,
	                    const QString& body, bool richText);

	/**
	 * Take a post out of the hub's index.
	 *
	 * The extension's withdrawal, which is what the Delete button asks for: the name is
	 * the operation's and the button's is the reader's. See the class note for what the
	 * confirmation may say about it.
	 */
	void withdrawRequested(const QString& tth);

	/**
	 * Read the whole board again from the beginning rather than from the cursor.
	 *
	 * Nothing in the pane emits this. It is the only way a hub that reordered or
	 * removed entries in violation of the extension becomes visible at all - what
	 * arrives is reconciled against what is held rather than replacing it - and it is
	 * driven automatically now, by connecting to the hub, rather than by a button
	 * asking a reader to keep the board honest themselves. Kept because the pane is
	 * where a client would say so if there were ever a reason to ask from here.
	 */
	void rereadRequested();

	/** A link in a body was activated; the pane follows nothing itself. */
	void linkActivated(const QUrl& url);

	/**
	 * The composer's own three, carried up as a conversation carries them.
	 *
	 * Choosing a file, hashing it and offering it to the hub belong to whoever owns the
	 * connection, and the answer comes back through insertAttachment(). Forwarded rather
	 * than left unconnected: a control that does nothing when it is pressed is worse than
	 * one that is not there.
	 */
	void attachRequested();
	void imagePasted(const QByteArray& data, const QString& suffix);
	void filesDropped(const QStringList& paths);

private:
	/** Which of the two things the composer is being used for, or neither. */
	enum class Compose {
		None,
		Thread,
		Reply
	};

	QWidget* buildBoardHeader();
	QWidget* buildThreadList();
	QWidget* buildReader();
	QWidget* buildComposer();

	/** The title, the description and what may be done here. */
	void refreshBoardHeader();

	/**
	 * Draw the post being read, as whichever of the six states it is in.
	 *
	 * The one function where the honesty of this pane lives, so it decides everything
	 * from one state rather than leaving pieces of the last post on the screen.
	 */
	void refreshReading();

	/** Which buttons make sense for the post being read, and for this board's permissions. */
	void refreshActions();

	/** Put the selection back on the post being read, after the model rebuilt itself. */
	void restoreSelection();

	void selectionChanged();

	/** The fetch button: ask for the body, or ask again after it could not be had. */
	void fetch();

	void requestParent();

	/**
	 * Ask before deleting, and put the question where the buttons were.
	 *
	 * In the pane rather than in a dialog over it, so that the post being deleted stays
	 * on the screen behind the question - and so that the question goes away by itself
	 * when the reader moves to another post, which a modal box could not do.
	 */
	void askToDelete();
	void confirmDelete();

	/** Put the buttons back, whether the question was answered or abandoned. */
	void dropTheQuestion();

	void showCompose(Compose mode);

	/**
	 * What a reply opens with: the parent's subject prefixed once, and its words quoted.
	 *
	 * Neither goes through RichText::displayText(). That is a reduction for drawing a
	 * stranger's text safely, and this text is on its way into a document whose hash is
	 * its name - so a replacement character or an ellipsis put in here would be in the
	 * post somebody sends, under a name every reader checks the bytes against.
	 */
	void prefillReply();

	/** The composer's send button, for a new thread or for a reply. */
	void submit(const QString& message, bool richText);

	/** What the reading pane should show about @a tth, tombstone included. */
	Qdc::PostState stateOfReading() const;

	BoardModel* m_threads;

	Qdc::BoardInfo m_board;

	/** The post being read, and the document for it once one has arrived. */
	QString m_reading;
	Qdc::BoardPost m_post;

	/** This user's own client id; see setOwnCid(). */
	QString m_ownCid;

	/** Whether a picture a body names is here; see setMediaResolver(). */
	std::function<RichText::LocalMedia(const QString& tth)> m_media;

	/** Whether m_post is the document of m_reading, rather than a leftover. */
	bool m_hasPost = false;

	/**
	 * Which state the reading area was last drawn as.
	 *
	 * Kept so that a state arriving for the post being read redraws it, and so that the
	 * rest of what moves in the list - a row appearing, a subject, a read mark - does
	 * not. Meaningless while nothing is being read.
	 */
	Qdc::PostState m_drawn = Qdc::PostState::Announced;

	QSplitter* m_split;

	QLabel* m_title;
	QLabel* m_description;
	QLabel* m_permissions;

	/**
	 * The two bands across the top: a gap that cannot be closed, and a refusal.
	 *
	 * Each is a label inside a widget, because a stylesheet colours the band and the ink
	 * on it separately. They borrow the names the theme already colours for a
	 * conversation that cannot be used: a board with a hole in it and an operation the
	 * hub declined are the same kind of statement, and a name of their own would need a
	 * rule that no theme in this tree has.
	 */
	QWidget* m_gapBand;
	QLabel* m_gap;
	QWidget* m_refusalBand;
	QLabel* m_refusal;

	/** The way out of a refusal, for somebody who has read it and wants it gone. */
	QPushButton* m_refusalClose;

	/**
	 * What takes a refusal away by itself.
	 *
	 * A refusal is something that happened once, not a state the board is in - unlike
	 * the gap, which stays until the hub will replay far enough back to close it. A band
	 * that waited to be dismissed would still be there an hour later, saying something
	 * about a post nobody is looking at any more.
	 */
	QTimer* m_refusalTimer;

	/** Take the refusal off the screen, however it came to be time to. */
	void clearRefusal();

	QTreeView* m_list;
	QPushButton* m_newPost;

	/**
	 * The area beside the thread list, which is either a post or a post being written.
	 *
	 * One at a time and never both: a reader who has asked to write something is
	 * writing it, and a composer standing open under every post they read would take a
	 * third of the pane from the thing the pane is for. Cancelling puts the post that
	 * was being read back on the screen.
	 */
	QStackedWidget* m_area;

	QWidget* m_reader;

	QLabel* m_subject;

	/** Who the post is from and when the hub took it; see attribution(). */
	QLabel* m_author;

	QTextBrowser* m_body;

	/** Holds the post at the top of the pane when there are no words under it. */
	QWidget* m_filler;

	/**
	 * Show the words, or the space that stands in for them.
	 *
	 * The two go together: exactly one of them carries the stretch that decides where
	 * the leftover height in the pane goes, so setting one without the other is how the
	 * subject and its byline end up drifting apart down an empty pane.
	 */
	void showBody(bool showing);

	/** The row under the post, and the question that takes its place. */
	QWidget* m_actions;
	QPushButton* m_fetch;
	QPushButton* m_parent;
	QPushButton* m_reply;
	QPushButton* m_delete;

	QWidget* m_question;
	QPushButton* m_deleteConfirm;

	QWidget* m_composeBox;
	QLineEdit* m_composeSubject;
	Composer* m_composer;
	QLabel* m_composeNote;
	QPushButton* m_composeCancel;

	Compose m_compose = Compose::None;

	/**
	 * Which post a reply being written is a reply to.
	 *
	 * Taken when the reply is begun rather than read off the selection when it is sent:
	 * the list goes on arriving and a reader may well click something else while typing,
	 * and a reply that quietly changed its parent would be answering a post its author
	 * never saw.
	 */
	QString m_replyTo;
};

}

#endif // HAVE_QUICKDC_QUI_BOARDPANE_H

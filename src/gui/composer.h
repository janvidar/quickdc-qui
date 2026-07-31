/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_COMPOSER_H
#define HAVE_QUICKDC_QUI_COMPOSER_H

#include "richtext.h"

#include <QList>
#include <QStringList>
#include <QTextEdit>
#include <QWidget>

class QCompleter;
class QStringListModel;
class QToolButton;

namespace Qdc {

/**
 * The text box part of the composer.
 *
 * Split out from Composer because three behaviours have to be intercepted before
 * QTextEdit sees them: Return sends rather than breaking the line, typing after an '@'
 * offers the people who are here, and a paste has to be stripped back to the formatting
 * a chat line can actually carry.
 */
class ComposerEdit final : public QTextEdit {
	Q_OBJECT

public:
	explicit ComposerEdit(QWidget* parent = nullptr);

	/** Who can be mentioned. Replaces the previous set. */
	void setNicknames(const QStringList& nicknames);

	QSize sizeHint() const override;

	/**
	 * The same as sizeHint().
	 *
	 * QAbstractScrollArea's own minimum is several lines tall and wins over a
	 * smaller hint, which left an empty composer taking a quarter of the pane.
	 */
	QSize minimumSizeHint() const override;

signals:
	/** Return was pressed with something to send. */
	void sendRequested();

protected:
	void keyPressEvent(QKeyEvent* event) override;

	/**
	 * Keep only the formatting a message can carry.
	 *
	 * Pasting from a browser otherwise drags in fonts, sizes, colours, headings and
	 * images, none of which survives being sent and all of which would sit in the box
	 * looking as though it would. What is kept is bold, italic and inline code.
	 */
	void insertFromMimeData(const QMimeData* source) override;

private:
	/**
	 * The word being typed after an '@', or empty if the cursor is not in one.
	 *
	 * A mention runs from an '@' at a word boundary to the cursor. Requiring the
	 * boundary is what stops an email address turning into a completion prompt.
	 */
	QString mentionPrefix() const;

	/** Replace the mention under the cursor with 'nickname'. */
	void completeMention(const QString& nickname);

	QCompleter* m_completer;
	QStringListModel* m_nicknames;
};

/**
 * Where a message is written: a formatting bar, a text box, and a send button.
 *
 * The box holds real rich text. On the way out the document is serialized to the
 * markdown subset in richtext.h and the message is flagged as markup, so the far end
 * knows to render it rather than show the markers. A message with no formatting in it is
 * sent as its own plain text and carries no flag at all - which is what lets somebody
 * type *asterisks* and have them arrive as asterisks.
 *
 * Formatting is only offered where the protocol can carry that flag; see
 * setRichTextEnabled().
 */
class Composer final : public QWidget {
	Q_OBJECT

public:
	explicit Composer(QWidget* parent = nullptr);

	/** Who can be mentioned; normally the hub's user list. */
	void setNicknames(const QStringList& nicknames);

	void setPlaceholderText(const QString& text);

	/** Disable everything, with a reason shown in place of the placeholder. */
	void setSendable(bool sendable, const QString& reason = QString());
	bool isSendable() const;

	/**
	 * Whether formatting may be applied at all.
	 *
	 * False on a hub that cannot flag a message as markdown - NMDC has nowhere to put
	 * the flag, so formatting there would be sent as markers the far end shows
	 * literally. The controls are disabled rather than quietly dropping what they did,
	 * and any formatting already in the box is flattened.
	 */
	void setRichTextEnabled(bool enabled);
	bool isRichTextEnabled() const;

	/**
	 * Put a file somebody chose into the message, as a link to its hash.
	 *
	 * The document holds an ordinary anchor whose target is the magnet URI, so it
	 * serializes as a markdown link and needs no special case on the way out. Only the
	 * name is shown: a hash is not something anybody reads.
	 */
	void insertAttachment(const RichText::Attachment& file);

	/** Whether the last thing asked for was a picture to show; see attachRequested(). */
	bool wantsMedia() const;

	void clear();
	void focusInput();

signals:
	/**
	 * Never emitted with empty or whitespace-only text.
	 *
	 * @param richText the text is markdown and the message should say so.
	 */
	void sendRequested(const QString& message, bool richText);

	/**
	 * Somebody wants to attach a file.
	 *
	 * Only asked for here. Choosing the file, hashing it and offering it to the hub all
	 * belong to whoever owns the connection, and the answer comes back through
	 * insertAttachment().
	 */
	void attachRequested();

private:
	/** Apply or lift a character format across the selection, or the word under it. */
	void toggleCharFormat(const QTextCharFormat& on, const QTextCharFormat& off,
	                      bool enable);

	void toggleBold();
	void toggleItalic();
	void toggleCode();

	/** Turn the selected blocks into a quote or a code block, or back to prose. */
	void toggleBlockKind(RichText::BlockKind kind);

	/**
	 * Turn the selected blocks into a bulleted or numbered list, or back to prose.
	 *
	 * Separate from toggleBlockKind() because a list is a QTextList as well as a kind:
	 * the items have to look like items while they are being typed, and Qt draws the
	 * markers and keeps the numbering only for a list it owns.
	 */
	void toggleList(RichText::BlockKind kind);

	/** What sort of block the cursor is in. */
	RichText::BlockKind blockKindAtCursor() const;

	/** Ask for an address and turn the selection into a link to it. */
	void insertLink();

	QToolButton* makeFormatButton(const QString& label, const QString& tooltip,
	                              bool bold, bool italic, bool monospace);

	void showEmojiPicker();
	void send();

	/** Follow the cursor: the format buttons show what is in force where it sits. */
	void refreshFormatState();

	/**
	 * Take all formatting off what is in the box, leaving the words.
	 *
	 * Done through a cursor rather than by setting the plain text back: QTextEdit
	 * applies the format at the cursor to text set that way, so a bold document would
	 * come back bold.
	 */
	void flattenDocument();

	ComposerEdit* m_edit;
	QToolButton* m_emoji;
	QToolButton* m_send;

	QToolButton* m_bold;
	QToolButton* m_italic;
	QToolButton* m_code;
	QToolButton* m_quote;
	QToolButton* m_codeBlock;
	QToolButton* m_attach;
	QToolButton* m_image;
	QToolButton* m_bullets;
	QToolButton* m_numbers;
	QToolButton* m_link;

	/** Everything that applies formatting, so it can be enabled as a group. */
	QList<QToolButton*> m_formatButtons;

	bool m_richTextEnabled = true;

	/** Which control asked for a file: the picture one, or the plain attachment. */
	bool m_wantsMedia = false;
};

}

#endif // HAVE_QUICKDC_QUI_COMPOSER_H

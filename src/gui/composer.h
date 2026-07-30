/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_COMPOSER_H
#define HAVE_QUICKDC_QUI_COMPOSER_H

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
 * Split out from Composer because two behaviours have to be intercepted before
 * QTextEdit sees them: Return sends rather than breaking the line, and typing
 * after an '@' offers the people who are here.
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
 * "Rich text" here means markup that survives being sent, not styled runs. Both
 * ADC and NMDC carry a chat line as one plain string - Hub::sendChatMessage takes
 * a const char* and there is no field for anything else - so the formatting
 * buttons wrap the selection in the markers other clients understand rather than
 * applying a QTextCharFormat that would be discarded on the way out.
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

	void clear();
	void focusInput();

signals:
	/** Never emitted with empty or whitespace-only text. */
	void sendRequested(const QString& message);

private:
	/**
	 * Put 'prefix' and 'suffix' around the selection, or around the word the
	 * cursor is in when there is no selection - which is what makes the buttons
	 * useful without having to select first.
	 */
	void wrapSelection(const QString& prefix, const QString& suffix);

	/** Prefix every selected line, for quotes. */
	void prefixLines(const QString& prefix);

	QToolButton* makeFormatButton(const QString& label, const QString& tooltip,
	                              bool bold, bool italic, bool monospace);

	void showEmojiPicker();
	void send();

	ComposerEdit* m_edit;
	QToolButton* m_emoji;
	QToolButton* m_send;
};

}

#endif // HAVE_QUICKDC_QUI_COMPOSER_H

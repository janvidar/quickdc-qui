/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "composer.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace {

/** What starts a mention. */
constexpr QChar MENTION = u'@';

/** How many characters after the '@' before completions are offered. */
constexpr int MENTION_MINIMUM = 1;

/** The text box grows with its content, up to this many lines. */
constexpr int MAX_LINES = 6;

/**
 * A small, fixed set of emoji.
 *
 * Fixed rather than the whole of Unicode: a full picker needs a searchable
 * database and per-platform font coverage checks, and what a chat window actually
 * needs is the handful people use. These are all long-standing code points with
 * wide font support.
 */
const QStringList& emoji_set()
{
	static const QStringList set = {
		QStringLiteral("🙂"), QStringLiteral("😃"), QStringLiteral("😄"),
		QStringLiteral("😁"), QStringLiteral("😆"), QStringLiteral("😉"),
		QStringLiteral("😊"), QStringLiteral("😍"), QStringLiteral("😘"),
		QStringLiteral("😜"), QStringLiteral("🤔"), QStringLiteral("😐"),
		QStringLiteral("😑"), QStringLiteral("😕"), QStringLiteral("🙁"),
		QStringLiteral("😞"), QStringLiteral("😢"), QStringLiteral("😭"),
		QStringLiteral("😤"), QStringLiteral("😠"), QStringLiteral("😡"),
		QStringLiteral("😳"), QStringLiteral("😱"), QStringLiteral("😴"),
		QStringLiteral("👍"), QStringLiteral("👎"), QStringLiteral("👌"),
		QStringLiteral("👏"), QStringLiteral("🙏"), QStringLiteral("💪"),
		QStringLiteral("🎉"), QStringLiteral("🔥"), QStringLiteral("💯"),
		QStringLiteral("❤️"), QStringLiteral("💔"), QStringLiteral("⭐"),
		QStringLiteral("✅"), QStringLiteral("❌"), QStringLiteral("⚠️"),
		QStringLiteral("❓"), QStringLiteral("❗"), QStringLiteral("💡"),
		QStringLiteral("🍺"), QStringLiteral("☕"), QStringLiteral("🍕"),
		QStringLiteral("🎵"), QStringLiteral("🎬"), QStringLiteral("📁")
	};

	return set;
}

/** How many emoji per row in the picker. */
constexpr int EMOJI_COLUMNS = 8;

}

Qdc::ComposerEdit::ComposerEdit(QWidget* parent)
	: QTextEdit(parent)
	, m_completer(new QCompleter(this))
	, m_nicknames(new QStringListModel(this))
{
	setAcceptRichText(false);
	setTabChangesFocus(true);
	setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	m_completer->setModel(m_nicknames);
	m_completer->setCaseSensitivity(Qt::CaseInsensitive);
	m_completer->setCompletionMode(QCompleter::PopupCompletion);
	m_completer->setWidget(this);

	connect(m_completer, QOverload<const QString&>::of(&QCompleter::activated),
	        this, &ComposerEdit::completeMention);

	/* Grow with the content, so a long message is visible as it is written. */
	connect(this, &QTextEdit::textChanged, this, [this] { updateGeometry(); });
}

void Qdc::ComposerEdit::setNicknames(const QStringList& nicknames)
{
	m_nicknames->setStringList(nicknames);
}

QSize Qdc::ComposerEdit::sizeHint() const
{
	const QFontMetrics metrics(font());
	const int line = metrics.lineSpacing();

	/*
	 * Measured from the laid out document rather than counted from the newlines,
	 * so a single wrapped line takes the room it actually needs.
	 */
	const int lines = qBound(1, qCeil(document()->size().height() / line), MAX_LINES);

	const int chrome = 2 * frameWidth()
		+ 2 * qCeil(document()->documentMargin())
		+ contentsMargins().top() + contentsMargins().bottom();

	return QSize(QTextEdit::sizeHint().width(), lines * line + chrome);
}

QSize Qdc::ComposerEdit::minimumSizeHint() const
{
	return sizeHint();
}

QString Qdc::ComposerEdit::mentionPrefix() const
{
	const QTextCursor cursor = textCursor();
	if (cursor.hasSelection()) return QString();

	const QString block = cursor.block().text();
	const int position = cursor.positionInBlock();

	for (int n = position - 1; n >= 0; --n)
	{
		const QChar at = block.at(n);

		if (at == MENTION)
		{
			/*
			 * Only at a word boundary, so "user@host" does not open the popup on
			 * the host part.
			 */
			if (n > 0 && !block.at(n - 1).isSpace()) return QString();

			return block.mid(n + 1, position - n - 1);
		}

		/* A mention is one word; anything that breaks it ends the search. */
		if (at.isSpace()) return QString();
	}

	return QString();
}

void Qdc::ComposerEdit::completeMention(const QString& nickname)
{
	QTextCursor cursor = textCursor();
	const QString prefix = mentionPrefix();

	/* Select back over what was typed, including the '@' itself. */
	cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor,
	                    prefix.length() + 1);
	cursor.insertText(MENTION + nickname + QLatin1Char(' '));

	setTextCursor(cursor);
}

void Qdc::ComposerEdit::keyPressEvent(QKeyEvent* event)
{
	QAbstractItemView* popup = m_completer->popup();

	/*
	 * While the popup is up it owns the keys that drive it. Enter in particular:
	 * it accepts the completion rather than sending the message, which is why
	 * this runs before the send handling below.
	 */
	if (popup && popup->isVisible())
	{
		switch (event->key())
		{
			case Qt::Key_Return:
			case Qt::Key_Enter:
			case Qt::Key_Tab:
			case Qt::Key_Escape:
			case Qt::Key_Up:
			case Qt::Key_Down:
				event->ignore();
				return;

			default:
				break;
		}
	}

	const bool isReturn = (event->key() == Qt::Key_Return
	                       || event->key() == Qt::Key_Enter);

	if (isReturn && !(event->modifiers() & Qt::ShiftModifier))
	{
		emit sendRequested();
		return;
	}

	QTextEdit::keyPressEvent(event);

	const QString prefix = mentionPrefix();

	if (prefix.length() < MENTION_MINIMUM)
	{
		if (popup) popup->hide();
		return;
	}

	m_completer->setCompletionPrefix(prefix);

	if (m_completer->completionCount() == 0)
	{
		if (popup) popup->hide();
		return;
	}

	m_completer->complete();
}

Qdc::Composer::Composer(QWidget* parent)
	: QWidget(parent)
	, m_edit(new ComposerEdit(this))
	, m_emoji(new QToolButton(this))
	, m_send(new QToolButton(this))
{
	m_edit->setPlaceholderText(tr("Write a message"));

	QHBoxLayout* bar = new QHBoxLayout;
	bar->setContentsMargins(0, 0, 0, 0);
	bar->setSpacing(2);

	QToolButton* bold = makeFormatButton(tr("B"), tr("Bold"), true, false, false);
	QToolButton* italic = makeFormatButton(tr("I"), tr("Italic"), false, true, false);
	QToolButton* code = makeFormatButton(tr("< >"), tr("Code"), false, false, true);
	QToolButton* quote = makeFormatButton(tr("❝"), tr("Quote"), false, false, false);

	connect(bold, &QToolButton::clicked, this, [this] {
		wrapSelection(QStringLiteral("**"), QStringLiteral("**"));
	});
	connect(italic, &QToolButton::clicked, this, [this] {
		wrapSelection(QStringLiteral("*"), QStringLiteral("*"));
	});
	connect(code, &QToolButton::clicked, this, [this] {
		wrapSelection(QStringLiteral("`"), QStringLiteral("`"));
	});
	connect(quote, &QToolButton::clicked, this, [this] {
		prefixLines(QStringLiteral("> "));
	});

	m_emoji->setText(QStringLiteral("🙂"));
	m_emoji->setAutoRaise(true);
	m_emoji->setToolTip(tr("Insert an emoji"));
	m_emoji->setCursor(Qt::PointingHandCursor);
	connect(m_emoji, &QToolButton::clicked, this, &Composer::showEmojiPicker);

	bar->addWidget(bold);
	bar->addWidget(italic);
	bar->addWidget(code);
	bar->addWidget(quote);
	bar->addWidget(m_emoji);
	bar->addStretch(1);

	m_send->setText(tr("Send"));
	m_send->setToolTip(tr("Send the message (Return; Shift+Return for a new line)"));
	m_send->setCursor(Qt::PointingHandCursor);
	m_send->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	connect(m_send, &QToolButton::clicked, this, &Composer::send);

	connect(m_edit, &ComposerEdit::sendRequested, this, &Composer::send);

	QHBoxLayout* row = new QHBoxLayout;
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(6);
	row->addWidget(m_edit, 1);
	row->addWidget(m_send, 0, Qt::AlignBottom);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 4, 8, 8);
	layout->setSpacing(4);
	layout->addLayout(bar);
	layout->addLayout(row);
}

QToolButton* Qdc::Composer::makeFormatButton(const QString& label,
                                             const QString& tooltip,
                                             bool bold, bool italic,
                                             bool monospace)
{
	QToolButton* button = new QToolButton(this);
	button->setText(label);
	button->setToolTip(tooltip);
	button->setAccessibleName(tooltip);
	button->setAutoRaise(true);
	button->setCursor(Qt::PointingHandCursor);

	/*
	 * The label is styled the way the button acts - a bold B, an italic I - which
	 * is why these are text buttons rather than glyphs. Cheaper than four more
	 * paths, and it says what it does in any language.
	 */
	QFont face = button->font();
	face.setBold(bold);
	face.setItalic(italic);
	if (monospace) face.setFamily(QStringLiteral("monospace"));
	button->setFont(face);

	return button;
}

void Qdc::Composer::setNicknames(const QStringList& nicknames)
{
	m_edit->setNicknames(nicknames);
}

void Qdc::Composer::setPlaceholderText(const QString& text)
{
	m_edit->setPlaceholderText(text);
}

void Qdc::Composer::setSendable(bool sendable, const QString& reason)
{
	m_edit->setEnabled(sendable);
	m_send->setEnabled(sendable);
	m_emoji->setEnabled(sendable);

	m_edit->setPlaceholderText(sendable ? tr("Write a message") : reason);
}

void Qdc::Composer::clear()
{
	m_edit->clear();
}

void Qdc::Composer::focusInput()
{
	m_edit->setFocus(Qt::OtherFocusReason);
}

void Qdc::Composer::wrapSelection(const QString& prefix, const QString& suffix)
{
	QTextCursor cursor = m_edit->textCursor();

	/* No selection: take the word the cursor is sitting in, if any. */
	if (!cursor.hasSelection()) cursor.select(QTextCursor::WordUnderCursor);

	const QString selected = cursor.selectedText();

	cursor.insertText(prefix + selected + suffix);

	/*
	 * With nothing to wrap the markers are inserted empty, so the cursor is put
	 * between them - otherwise the next thing typed lands outside.
	 */
	if (selected.isEmpty())
	{
		cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor,
		                    suffix.length());
		m_edit->setTextCursor(cursor);
	}

	m_edit->setFocus(Qt::OtherFocusReason);
}

void Qdc::Composer::prefixLines(const QString& prefix)
{
	QTextCursor cursor = m_edit->textCursor();

	const int start = cursor.selectionStart();
	const int end = cursor.selectionEnd();

	cursor.beginEditBlock();
	cursor.setPosition(start);

	/*
	 * Walked block by block from the start of the selection, re-reading the
	 * cursor position each time because inserting shifts everything after it.
	 */
	int offset = 0;
	while (true)
	{
		cursor.movePosition(QTextCursor::StartOfBlock);
		cursor.insertText(prefix);
		offset += prefix.length();

		if (cursor.position() >= end + offset) break;
		if (!cursor.movePosition(QTextCursor::NextBlock)) break;
	}

	cursor.endEditBlock();
	m_edit->setFocus(Qt::OtherFocusReason);
}

void Qdc::Composer::showEmojiPicker()
{
	/*
	 * A menu holding a grid, rather than a dialog: it dismisses itself on a click
	 * elsewhere and does not take focus away from the text box permanently.
	 */
	QMenu menu(this);

	QWidget* grid = new QWidget(&menu);
	QGridLayout* layout = new QGridLayout(grid);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(0);

	const QStringList& set = emoji_set();

	for (int n = 0; n < set.size(); ++n)
	{
		QToolButton* button = new QToolButton(grid);
		button->setText(set.at(n));
		button->setAutoRaise(true);
		button->setFixedSize(30, 30);
		button->setCursor(Qt::PointingHandCursor);

		QFont face = button->font();
		face.setPointSize(face.pointSize() + 4);
		button->setFont(face);

		const QString emoji = set.at(n);
		connect(button, &QToolButton::clicked, &menu, [this, &menu, emoji] {
			m_edit->insertPlainText(emoji);
			menu.close();
			m_edit->setFocus(Qt::OtherFocusReason);
		});

		layout->addWidget(button, n / EMOJI_COLUMNS, n % EMOJI_COLUMNS);
	}

	QWidgetAction* action = new QWidgetAction(&menu);
	action->setDefaultWidget(grid);
	menu.addAction(action);

	menu.exec(m_emoji->mapToGlobal(QPoint(0, m_emoji->height())));
}

void Qdc::Composer::send()
{
	const QString message = m_edit->toPlainText().trimmed();
	if (message.isEmpty()) return;

	emit sendRequested(message);
	m_edit->clear();
}

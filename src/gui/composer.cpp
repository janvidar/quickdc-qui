/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "composer.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QTextFragment>
#include <QUrl>
#include <QKeyEvent>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>
#include <QTextList>
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
	/*
	 * Rich: the buttons apply real formatting and the document is what gets serialized
	 * on the way out. What may be pasted in is narrowed separately - see
	 * insertFromMimeData().
	 */
	setAcceptRichText(true);
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

void Qdc::ComposerEdit::insertFromMimeData(const QMimeData* source)
{
	if (!source) return;

	/*
	 * Rebuilt run by run rather than filtered in place: a pasted fragment can carry
	 * headings, tables, colours, sizes and images, and there is no subtractive way to
	 * be sure none of it survived. Keeping only what is asked for is the only version
	 * of this that stays correct as Qt's HTML support grows.
	 */
	if (!source->hasHtml())
	{
		if (source->hasText()) insertPlainText(source->text());
		return;
	}

	QTextDocument parsed;
	parsed.setHtml(source->html());

	QTextCursor out = textCursor();
	out.beginEditBlock();

	bool firstBlock = true;

	for (QTextBlock block = parsed.begin(); block.isValid(); block = block.next())
	{
		if (!firstBlock) out.insertText(QStringLiteral("\n"));
		firstBlock = false;

		for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
		{
			const QTextFragment fragment = it.fragment();
			if (!fragment.isValid()) continue;

			const QTextCharFormat had = fragment.charFormat();

			QTextCharFormat kept;
			if (had.fontWeight() >= QFont::Bold) kept.setFontWeight(QFont::Bold);
			if (had.fontItalic()) kept.setFontItalic(true);

			/*
			 * A pasted monospace run is taken as inline code, which is what somebody
			 * copying a command out of a terminal or a code sample means by it.
			 */
			if (had.fontFixedPitch()
			    || had.property(Qdc::RichText::CodeProperty).toBool())
			{
				kept.setProperty(Qdc::RichText::CodeProperty, true);
				kept.setFontFixedPitch(true);
				kept.setFontFamilies({Qdc::RichText::monospaceFamily()});
			}

			/* A link is kept when QuickDC would open it, so pasting one from a
			   browser keeps its target rather than only its text. */
			const QUrl href(had.anchorHref(), QUrl::StrictMode);
			if (had.isAnchor() && Qdc::RichText::isLinkable(href))
			{
				kept.setAnchor(true);
				kept.setAnchorHref(href.toString());
				kept.setFontUnderline(true);
			}

			out.insertText(fragment.text(), kept);
		}
	}

	out.endEditBlock();
	setTextCursor(out);
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

	/*
	 * Shift+Return is the only way to get a new line here, so inside a list it has to
	 * mean the next item. QTextEdit would otherwise insert a soft break, which leaves
	 * two lines sharing one bullet.
	 */
	if (isReturn && textCursor().block().textList())
	{
		QTextCursor cursor = textCursor();
		cursor.insertBlock();
		setTextCursor(cursor);
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
	, m_bold(makeFormatButton(tr("B"), tr("Bold"), true, false, false))
	, m_italic(makeFormatButton(tr("I"), tr("Italic"), false, true, false))
	, m_code(makeFormatButton(tr("< >"), tr("Code"), false, false, true))
	, m_quote(makeFormatButton(tr("❝"), tr("Quote"), false, false, false))
	, m_codeBlock(makeFormatButton(tr("≣"), tr("Code block"), false, false, true))
	, m_bullets(makeFormatButton(tr("•"), tr("Bulleted list"), false, false, false))
	, m_numbers(makeFormatButton(tr("1."), tr("Numbered list"), false, false, false))
	, m_link(makeFormatButton(tr("🔗"), tr("Link"), false, false, false))
{
	m_edit->setPlaceholderText(tr("Write a message"));

	QHBoxLayout* bar = new QHBoxLayout;
	bar->setContentsMargins(0, 0, 0, 0);
	bar->setSpacing(2);

	m_formatButtons = {m_bold, m_italic, m_code, m_quote, m_codeBlock,
	                   m_bullets, m_numbers, m_link};

	/*
	 * Checkable, because each one reports whether its formatting is in force where the
	 * cursor sits. refreshFormatState() keeps them honest as it moves.
	 */
	for (QToolButton* button : m_formatButtons) button->setCheckable(true);
	m_link->setCheckable(false);

	connect(m_bold, &QToolButton::clicked, this, &Composer::toggleBold);
	connect(m_italic, &QToolButton::clicked, this, &Composer::toggleItalic);
	connect(m_code, &QToolButton::clicked, this, &Composer::toggleCode);
	connect(m_quote, &QToolButton::clicked, this, [this] {
		toggleBlockKind(RichText::Quote);
	});
	connect(m_codeBlock, &QToolButton::clicked, this, [this] {
		toggleBlockKind(RichText::CodeBlock);
	});
	connect(m_bullets, &QToolButton::clicked, this, [this] {
		toggleList(RichText::Bullet);
	});
	connect(m_numbers, &QToolButton::clicked, this, [this] {
		toggleList(RichText::Number);
	});
	connect(m_link, &QToolButton::clicked, this, &Composer::insertLink);

	connect(m_edit, &QTextEdit::cursorPositionChanged,
	        this, &Composer::refreshFormatState);
	connect(m_edit, &QTextEdit::currentCharFormatChanged,
	        this, &Composer::refreshFormatState);

	m_emoji->setText(QStringLiteral("🙂"));
	m_emoji->setAutoRaise(true);
	m_emoji->setToolTip(tr("Insert an emoji"));
	m_emoji->setCursor(Qt::PointingHandCursor);
	connect(m_emoji, &QToolButton::clicked, this, &Composer::showEmojiPicker);

	for (QToolButton* button : m_formatButtons) bar->addWidget(button);
	bar->addWidget(m_emoji);
	bar->addStretch(1);

	m_send->setObjectName(QStringLiteral("primaryButton"));
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

	refreshFormatState();
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
	if (monospace) face.setFamily(RichText::monospaceFamily());
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

	for (QToolButton* button : m_formatButtons)
		button->setEnabled(sendable && m_richTextEnabled);

	m_edit->setPlaceholderText(sendable ? tr("Write a message") : reason);
}

void Qdc::Composer::setRichTextEnabled(bool enabled)
{
	m_richTextEnabled = enabled;

	for (QToolButton* button : m_formatButtons)
	{
		button->setEnabled(enabled && isSendable());

		/* accessibleName() is what the button does, set when it was made. */
		button->setToolTip(enabled ? button->accessibleName()
		                           : tr("This hub cannot carry formatting"));
	}

	/*
	 * Formatting that cannot be sent must not sit in the box looking as though it will
	 * be. The markers are not left in its place either: that would send punctuation
	 * nobody typed.
	 */
	if (!enabled) flattenDocument();

	refreshFormatState();
}

bool Qdc::Composer::isRichTextEnabled() const
{
	return m_richTextEnabled;
}

bool Qdc::Composer::isSendable() const
{
	return m_send->isEnabled();
}

void Qdc::Composer::clear()
{
	m_edit->clear();
}

void Qdc::Composer::focusInput()
{
	m_edit->setFocus(Qt::OtherFocusReason);
}

void Qdc::Composer::toggleCharFormat(const QTextCharFormat& on,
                                     const QTextCharFormat& off, bool enable)
{
	QTextCursor cursor = m_edit->textCursor();

	/*
	 * With nothing selected the word under the cursor is taken, which is what makes
	 * the buttons useful without having to select first. An empty document has no word,
	 * and then the format applies to whatever is typed next.
	 */
	if (!cursor.hasSelection()) cursor.select(QTextCursor::WordUnderCursor);

	const QTextCharFormat& format = enable ? on : off;

	if (cursor.hasSelection())
		cursor.mergeCharFormat(format);
	else
		m_edit->mergeCurrentCharFormat(format);

	m_edit->setFocus(Qt::OtherFocusReason);
	refreshFormatState();
}

void Qdc::Composer::toggleBold()
{
	QTextCharFormat on;
	on.setFontWeight(QFont::Bold);

	QTextCharFormat off;
	off.setFontWeight(QFont::Normal);

	toggleCharFormat(on, off, m_bold->isChecked());
}

void Qdc::Composer::toggleItalic()
{
	QTextCharFormat on;
	on.setFontItalic(true);

	QTextCharFormat off;
	off.setFontItalic(false);

	toggleCharFormat(on, off, m_italic->isChecked());
}

void Qdc::Composer::toggleCode()
{
	/*
	 * The property is what marks a run as code; the monospace font is only how it
	 * looks. Detecting code from the family instead would mistake somebody who simply
	 * prefers a monospace font for somebody quoting a command.
	 */
	QTextCharFormat on;
	on.setProperty(RichText::CodeProperty, true);
	on.setFontFixedPitch(true);
	on.setFontFamilies({RichText::monospaceFamily()});

	QTextCharFormat off;
	off.setProperty(RichText::CodeProperty, false);
	off.setFontFixedPitch(false);
	off.setFontFamilies({m_edit->font().family()});

	toggleCharFormat(on, off, m_code->isChecked());
}

void Qdc::Composer::toggleBlockKind(RichText::BlockKind kind)
{
	QTextCursor cursor = m_edit->textCursor();

	const int from = cursor.selectionStart();
	const int to = cursor.selectionEnd();

	cursor.beginEditBlock();
	cursor.setPosition(from);

	/*
	 * Pressing the button again on a block that is already that kind puts it back to
	 * prose, so the same control both applies and lifts.
	 */
	const QVariant current = cursor.blockFormat().property(RichText::BlockKindProperty);
	const bool lifting = current.isValid()
		&& static_cast<RichText::BlockKind>(current.toInt()) == kind;

	while (true)
	{
		QTextBlockFormat format = cursor.blockFormat();
		format.setProperty(RichText::BlockKindProperty,
		                   lifting ? RichText::Paragraph : kind);

		/* Indented so a quote or a code block reads as set apart while it is typed. */
		format.setLeftMargin(lifting ? 0 : 12);
		cursor.setBlockFormat(format);

		QTextCharFormat runs;
		if (kind == RichText::CodeBlock && !lifting)
		{
			runs.setFontFixedPitch(true);
			runs.setFontFamilies({RichText::monospaceFamily()});
		}
		else if (kind == RichText::CodeBlock)
		{
			runs.setFontFixedPitch(false);
			runs.setFontFamilies({m_edit->font().family()});
		}

		if (runs.propertyCount() > 0)
		{
			QTextCursor whole = cursor;
			whole.select(QTextCursor::BlockUnderCursor);
			whole.mergeCharFormat(runs);
		}

		if (cursor.block().position() + cursor.block().length() > to) break;
		if (!cursor.movePosition(QTextCursor::NextBlock)) break;
	}

	cursor.endEditBlock();

	m_edit->setFocus(Qt::OtherFocusReason);
	refreshFormatState();
}

Qdc::RichText::BlockKind Qdc::Composer::blockKindAtCursor() const
{
	const QTextBlock block = m_edit->textCursor().block();

	const QVariant kind = block.blockFormat().property(RichText::BlockKindProperty);
	if (kind.isValid()) return static_cast<RichText::BlockKind>(kind.toInt());

	if (const QTextList* list = block.textList())
	{
		return list->format().style() == QTextListFormat::ListDecimal
			? RichText::Number : RichText::Bullet;
	}

	return RichText::Paragraph;
}

void Qdc::Composer::toggleList(RichText::BlockKind kind)
{
	QTextCursor cursor = m_edit->textCursor();

	const int from = cursor.selectionStart();
	const int to = cursor.selectionEnd();

	const bool lifting = blockKindAtCursor() == kind;

	cursor.beginEditBlock();
	cursor.setPosition(from);

	QTextList* list = nullptr;

	while (true)
	{
		const QTextBlock block = cursor.block();

		if (lifting)
		{
			if (QTextList* owner = block.textList()) owner->remove(block);

			QTextBlockFormat format = cursor.blockFormat();
			format.clearProperty(RichText::BlockKindProperty);
			format.setIndent(0);
			format.setLeftMargin(0);
			cursor.setBlockFormat(format);
		}
		else
		{
			/*
			 * One list for the whole run, so Qt numbers the items against each other
			 * rather than restarting at every block.
			 */
			if (!list)
			{
				QTextListFormat listFormat;
				listFormat.setStyle(kind == RichText::Number
					? QTextListFormat::ListDecimal : QTextListFormat::ListDisc);
				listFormat.setIndent(1);

				list = cursor.createList(listFormat);
			}
			else
			{
				list->add(block);
			}

			/*
			 * Merged rather than set: the block format is where list membership is
			 * recorded, and replacing it would take the block out of the list again.
			 */
			QTextBlockFormat mark;
			mark.setProperty(RichText::BlockKindProperty, kind);
			cursor.mergeBlockFormat(mark);
		}

		if (cursor.block().position() + cursor.block().length() > to) break;
		if (!cursor.movePosition(QTextCursor::NextBlock)) break;
	}

	cursor.endEditBlock();

	m_edit->setFocus(Qt::OtherFocusReason);
	refreshFormatState();
}

void Qdc::Composer::insertLink()
{
	QTextCursor cursor = m_edit->textCursor();

	if (!cursor.hasSelection()) cursor.select(QTextCursor::WordUnderCursor);

	const QString label = cursor.selectedText();

	bool accepted = false;
	const QString typed = QInputDialog::getText(this, tr("Link"),
		tr("Address:"), QLineEdit::Normal, QString(), &accepted);

	if (!accepted) return;

	const QUrl url(typed.trimmed(), QUrl::StrictMode);

	/*
	 * The same set the feed is willing to open, checked here so that a link nobody
	 * could follow is refused while it can still be corrected rather than arriving as
	 * dead text at the far end.
	 */
	if (!RichText::isLinkable(url))
	{
		QMessageBox::information(this, tr("Link"),
			tr("'%1' is not an address QuickDC will open. Web addresses and hub "
			   "addresses work: http, https, adc, adcs and dchub.")
				.arg(typed.trimmed()));
		return;
	}

	QTextCharFormat format;
	format.setAnchor(true);
	format.setAnchorHref(url.toString());
	format.setFontUnderline(true);

	cursor.insertText(label.isEmpty() ? url.toString() : label, format);

	/*
	 * The anchor format would otherwise carry on into whatever is typed next, which
	 * would quietly extend the link past its text.
	 */
	QTextCharFormat plain;
	plain.setAnchor(false);
	plain.setAnchorHref(QString());
	plain.setFontUnderline(false);
	m_edit->setCurrentCharFormat(plain);

	m_edit->setFocus(Qt::OtherFocusReason);
}

void Qdc::Composer::flattenDocument()
{
	QTextDocument* document = m_edit->document();

	QTextCursor cursor(document);
	cursor.beginEditBlock();

	cursor.select(QTextCursor::Document);
	cursor.setCharFormat(QTextCharFormat());

	for (QTextBlock block = document->begin(); block.isValid(); block = block.next())
	{
		/* Out of the list first, so no QTextList is left holding a block that is no
		   longer an item of it. */
		if (QTextList* list = block.textList()) list->remove(block);

		QTextCursor at(block);
		at.setBlockFormat(QTextBlockFormat());
	}

	cursor.endEditBlock();

	m_edit->setCurrentCharFormat(QTextCharFormat());
}

void Qdc::Composer::refreshFormatState()
{
	const QTextCharFormat format = m_edit->currentCharFormat();
	const RichText::BlockKind current = blockKindAtCursor();

	for (QToolButton* button : m_formatButtons)
	{
		const QSignalBlocker blocker(button);

		if (button == m_bold) button->setChecked(format.fontWeight() >= QFont::Bold);
		else if (button == m_italic) button->setChecked(format.fontItalic());
		else if (button == m_code)
			button->setChecked(format.property(RichText::CodeProperty).toBool());
		else if (button == m_quote) button->setChecked(current == RichText::Quote);
		else if (button == m_codeBlock)
			button->setChecked(current == RichText::CodeBlock);
		else if (button == m_bullets) button->setChecked(current == RichText::Bullet);
		else if (button == m_numbers) button->setChecked(current == RichText::Number);
	}
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

	QList<QToolButton*> buttons;

	for (int n = 0; n < set.size(); ++n)
	{
		QToolButton* button = new QToolButton(grid);
		button->setText(set.at(n));
		button->setAutoRaise(true);
		button->setCursor(Qt::PointingHandCursor);
		button->setAccessibleName(set.at(n));

		QFont face = button->font();
		face.setPointSize(face.pointSize() + 4);
		button->setFont(face);

		const QString emoji = set.at(n);
		connect(button, &QToolButton::clicked, &menu, [this, &menu, emoji] {
			m_edit->insertPlainText(emoji);
			menu.close();
			m_edit->setFocus(Qt::OtherFocusReason);
		});

		buttons.append(button);
		layout->addWidget(button, n / EMOJI_COLUMNS, n % EMOJI_COLUMNS);
	}

	/*
	 * Square cells, all the size of the largest thing the style asks for. Asked rather
	 * than chosen: an emoji is drawn from a colour font whose glyphs are wider than the
	 * interface font's, and the padding around them comes from the stylesheet - so a
	 * size picked here would be a guess at two things this does not own, and a cell too
	 * small by a pixel makes the button elide the emoji away to an ellipsis.
	 */
	int cell = 0;
	for (const QToolButton* button : buttons)
	{
		const QSize wanted = button->sizeHint();
		cell = qMax(cell, qMax(wanted.width(), wanted.height()));
	}

	for (QToolButton* button : buttons) button->setFixedSize(cell, cell);

	QWidgetAction* action = new QWidgetAction(&menu);
	action->setDefaultWidget(grid);
	menu.addAction(action);

	menu.exec(m_emoji->mapToGlobal(QPoint(0, m_emoji->height())));
}

void Qdc::Composer::send()
{
	const QString plain = m_edit->toPlainText().trimmed();
	if (plain.isEmpty()) return;

	const bool richText = m_richTextEnabled && RichText::hasMarkup(*m_edit->document());

	/*
	 * Unformatted text is sent exactly as it was typed, which is the whole reason the
	 * flag exists: somebody who types *asterisks* has them arrive as asterisks, because
	 * nothing told the far end to read them as markup.
	 */
	const QString message = richText
		? RichText::toMarkdown(*m_edit->document()).trimmed()
		: plain;

	if (message.isEmpty()) return;

	emit sendRequested(message, richText);

	m_edit->clear();

	/* clear() leaves the format at the cursor behind, so the next message would open
	   in whatever the last one ended in. */
	m_edit->setCurrentCharFormat(QTextCharFormat());
	refreshFormatState();
}

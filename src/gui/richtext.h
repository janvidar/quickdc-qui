/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_RICHTEXT_H
#define HAVE_QUICKDC_QUI_RICHTEXT_H

#include <QColor>
#include <QString>
#include <QUrl>

class QTextDocument;

namespace Qdc {

/**
 * The markup a chat line may carry, and how it crosses the wire.
 *
 * ADC carries a chat line as one string, so formatting has to be encoded *into* that
 * string. Markdown is the encoding because it degrades: a client that knows nothing
 * about it shows `**bold**`, which is ugly but readable, where anything denser would be
 * noise. A message that is meant to be read as markup says so with the RT1 flag, so
 * somebody typing a literal asterisk still gets a literal asterisk.
 *
 * Everything here is a free function over strings and documents. No widgets, no state,
 * no theme lookups - the colours arrive as arguments. That is deliberate: this is the
 * part that has to be exactly right, and it can be tested on its own.
 *
 * The subset is closed, and closed on purpose:
 *
 *     **bold**            *italic*            `code`
 *     ```                 > quote             [text](url)
 *     block               > continues         bare://url
 *     ```
 *     - bullet            1. numbered
 *     - another           2. and on
 *
 * No headings, tables or images, and no nesting except emphasis inside a quote or a list
 * item. A remote user cannot introduce anything outside this set.
 */
namespace RichText {

/**
 * Which schemes a link may use, and who opens it.
 *
 * Anything not on the list is not turned into a link at all - it renders as the text it
 * is. That is the whole defence: a remote user chooses the href, so the set of things
 * they may choose has to be small and fixed. `file:` would read the local disk,
 * `javascript:` is meaningless to QTextDocument but would be a mistake to normalise, and
 * `data:` can carry anything.
 */
bool isLinkable(const QUrl& url);

/** A hub address, which QuickDC opens itself rather than handing to the desktop. */
bool isHubUrl(const QUrl& url);

/**
 * The family to set code in.
 *
 * Asked of the font database rather than written as "monospace", which is a fontconfig
 * name and resolves to nothing on macOS - where it silently leaves code in the body font.
 */
QString monospaceFamily();

/**
 * Which paragraph kind a block of the composer's document is.
 *
 * Stored on the block format under BlockKindProperty rather than inferred from margins
 * or fonts, so that a block says what it is instead of being guessed at.
 */
enum BlockKind {
	Paragraph = 0,
	Quote,
	CodeBlock,

	/** One item of a bulleted list; each item is a block of its own. */
	Bullet,

	/** One item of a numbered list. The number is not stored: it is the position. */
	Number
};

/**
 * Custom format properties.
 *
 * QTextFormat property ids from QTextFormat::UserProperty upward are ours to use. Inline
 * code is marked with a property rather than detected from the font family, because a
 * user may legitimately choose a monospace font for ordinary text and the two must not
 * be confused.
 */
enum Property {
	CodeProperty = 0x1000,     /**< bool, on a char format: inline code. */
	BlockKindProperty = 0x1001 /**< BlockKind, on a block format. */
};

/**
 * Serialize a composer document to markdown.
 *
 * Plain runs have their markdown-significant characters escaped, so a bold word beside a
 * literal asterisk survives as `**bold** \*`. That escaping is only correct for a
 * message that will be sent with RT1 - see hasMarkup().
 */
QString toMarkdown(const QTextDocument& document);

/**
 * Whether the document carries any formatting at all.
 *
 * This decides the RT1 flag, and so decides whether toMarkdown() is used at all. A
 * document with nothing but plain runs is sent as its own plain text, unescaped, which
 * is what lets somebody type `*text*` and have it arrive as `*text*`.
 */
bool hasMarkup(const QTextDocument& document);

/** Colours for rendering. Passed in so this module never reads the theme. */
struct RenderColours {
	QColor code;       /**< Ink for inline code and code blocks. */
	QColor codeBack;   /**< The panel behind them. */
	QColor quote;      /**< Ink for a quoted line. */
	QColor quoteBar;   /**< The rule down its left edge. */
	QColor link;
};

/**
 * How a message is laid out.
 *
 * 'indent' is the hanging indent: the width of the timestamp and speaker that 'prefix'
 * draws, so that every line after the first sits under the text rather than under the
 * clock.
 */
struct RenderOptions {
	/** Ready-made HTML for the timestamp and speaker, inlined into the first block. */
	QString prefix;

	int indent = 0;

	/**
	 * Ink for the message body, when it is not the palette's own.
	 *
	 * A line the hub generated is muted, so that it does not read as somebody speaking.
	 * The prefix keeps its own colours either way.
	 */
	QColor body;

	/** Narration - a hub notice, or a "/me" line - is set in italic throughout. */
	bool italic = false;

	RenderColours colours;
};

/**
 * Render markdown as HTML for the chat feed.
 *
 * The input is escaped before anything else, and the only tags in the result are ones
 * this function wrote. Remote text therefore cannot introduce markup, which is why this
 * exists instead of QTextDocument::setMarkdown() - that would accept the whole of
 * CommonMark from a stranger, including images, and an image makes the document go and
 * fetch a resource.
 *
 * The negative half of the hanging indent lands on the first block only. Applied to
 * every block it would drag the opening line of a quote or a code block out to the left,
 * because that is what a first-line indent means.
 */
QString renderMarkdown(const QString& markdown, const RenderOptions& options);

/**
 * Render an unformatted line, escaped, with its newlines kept.
 *
 * The path for a message that arrived without RT1: no markup is looked for, so `*text*`
 * stays as it was typed.
 */
QString renderPlain(const QString& text, const RenderOptions& options);

/**
 * Markdown with its markers taken off.
 *
 * For anything that wants prose rather than presentation: the accessible name of a row,
 * and what lands on the clipboard.
 */
QString toPlainText(const QString& markdown);

}

}

#endif // HAVE_QUICKDC_QUI_RICHTEXT_H

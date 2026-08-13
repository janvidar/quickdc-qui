/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_RICHTEXT_H
#define HAVE_QUICKDC_QUI_RICHTEXT_H

#include <QColor>
#include <QList>
#include <QSize>
#include <QString>
#include <QtTypes>

#include <functional>
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
 * A file offered in a message, named by its hash rather than by where it is.
 *
 * The wire form is an ADC magnet URI, which is what lets a message point at a file
 * without anybody having to agree on a location for it:
 *
 *     [notes.pdf](magnet:?xt=urn:tree:tiger:VN6...&dn=notes.pdf&xl=1048576)
 *
 * The hash is the whole address. Whoever has the file answers a search for that hash,
 * and until somebody asks, nothing has been transferred.
 */
struct Attachment {
	/** Tiger tree root, base32, as it appears in the URI. Empty if there was none. */
	QString tth;

	/** The name to show, from 'dn'. Never a path: a sender does not choose where a
	    file lands here. */
	QString name;

	/** Bytes, from 'xl'. Zero when the sender left it out. */
	quint64 size = 0;

	/** Whether the sender meant it to be shown in the message rather than offered. */
	bool inlineMedia = false;

	bool isValid() const { return !tth.isEmpty(); }
};

/**
 * Read a magnet URI, or return something isValid() says no to.
 *
 * Only the three parameters a chat attachment needs are read - xt, dn and xl - and only
 * a tiger tree hash of the right shape is accepted. Anything else in the URI is ignored
 * rather than trusted.
 */
Attachment readMagnet(const QUrl& url);

/** A magnet URI for a file, as a message carries it. */
QString writeMagnet(const Attachment& file);

/**
 * Every file a message points at, in the order they appear.
 *
 * For deciding what to do about a message rather than for drawing it: whether anything
 * has to be fetched, and how big it would be. inlineMedia says which of them the sender
 * meant to be shown rather than offered.
 */
QList<Attachment> attachmentsIn(const QString& markdown);

/** A file size for a person to read: "1.0 MiB", not "1048576". */
QString describeSize(quint64 bytes);

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
	BlockKindProperty = 0x1001, /**< BlockKind, on a block format. */

	/**
	 * bool, on a char format: this link is media to be shown in the message.
	 *
	 * The difference between a file offered and a picture shown is what the sender
	 * meant, and nothing about the file itself - so it is recorded here rather than
	 * guessed from the name.
	 */
	MediaProperty = 0x1002
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

/**
 * A file named in a message that turns out to be on this machine.
 *
 * Whether a picture can be shown is not something a message can say - only this client
 * knows what it has - so the answer is asked for rather than assumed, and the path comes
 * from our own record of it. Nothing in a message ever becomes a path.
 */
struct LocalMedia {
	/** Where the file is, or empty when it is not here. */
	QString path;

	/** Its pixel size, so a caller can decide how much room to give it. */
	QSize size;

	bool isValid() const { return !path.isEmpty() && size.isValid(); }
};

/** Colours for rendering. Passed in so this module never reads the theme. */
struct RenderColours {
	QColor code;       /**< Ink for inline code and code blocks. */
	QColor codeBack;   /**< The panel behind them. */
	QColor quote;      /**< Ink for a quoted line. */
	QColor quoteBar;   /**< The rule down its left edge. */
	QColor link;

	/** The chip an attachment is drawn as. */
	QColor chip;
	QColor chipBack;
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

	/**
	 * Asked whether a picture named by its hash is here, and how big it is.
	 *
	 * Left unset, media renders as the chip an offered file gets: something to click
	 * rather than something to look at. That is also what a picture nobody has fetched
	 * yet looks like.
	 */
	std::function<LocalMedia(const QString& tth)> media;

	/** How much room a picture may take: the text column, and this many pixels tall. */
	int mediaWidth = 0;
	int mediaHeight = 300;

	/**
	 * The point size of the body font, from which a heading's size is worked out.
	 *
	 * Asked for rather than assumed because Qt's rich text honours a font size given in
	 * points and ignores one given in em or as a percentage, so a heading cannot be
	 * scaled without knowing what it is being scaled from. Left at zero, a heading is set
	 * bold and no larger, which is legible but flat.
	 */
	qreal fontPointSize = 0.0;
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

/**
 * A remote-chosen string, reduced to something safe to draw in a row of a list.
 *
 * A nickname, a hub name, a name out of somebody else's file list: RTF0 requires that
 * such a string be stripped of control characters, be bounded in length, and be presented
 * "in a way that cannot be confused with the client's own interface text". BBS0 repeats
 * the requirement for a board's name and a post's subject, and says why it is worse there:
 * a board persists where a chat line scrolls away.
 *
 * Control characters go. C0 and C1 are nothing a list row wants, and the five whitespace
 * ones become one space each rather than vanishing, so that a description written on two
 * lines reads as one instead of having its words run together. The line and paragraph
 * separators are treated the same way, being line breaks in everything Qt draws.
 *
 * The bidirectional embeddings, overrides and isolates are replaced with U+FFFD rather
 * than removed. One unterminated override reverses everything after it - "photo<RLO>gpj.exe"
 * reads as "photoexe.jpg" - and that is the whole of the attack, so a name doctored that
 * way should look doctored rather than quietly reading as some third thing. The invisible
 * ones are dropped instead: LRM and RLM reorder only the neutrals beside them, and a
 * zero-width space or a word joiner in a name is a way of making two names look alike, so
 * in both cases there is nothing worth marking the place of.
 *
 * Nothing else is touched. ZWJ and ZWNJ stay, because they are orthography in Persian and
 * Hindi and the joint in every emoji built from a sequence; an Arabic or Hebrew name
 * passes through exactly as it arrived, since right-to-left text is not an attack.
 * Confusable *letters* - a Cyrillic 'а' among Latin ones - are not addressed here at all:
 * telling those apart needs script and identifier data this has no business carrying, and
 * a half-hearted attempt would only be mistaken for one.
 *
 * The result is at most 128 UTF-16 units, ends in an ellipsis when anything was cut, and
 * is never cut between the halves of a surrogate pair.
 *
 * Applied where text is handed to a view and nowhere earlier, so that what is matched,
 * sorted, downloaded or stored is still what the other end actually said. It is a name
 * this is for and not a body of prose: both the bound and the flattening of newlines
 * would be wrong for a message, and a message has renderMarkdown() instead.
 */
QString displayText(const QString& text);

/**
 * Plain text as the markup that renders as exactly that text.
 *
 * A tooltip is not a plain string. QToolTip hands what it is given to a QLabel left on
 * Qt::AutoText, and AutoText asks Qt::mightBeRichText() - so a hub calling itself
 * "<b>Best</b> Hub" is drawn bold in one, an <img> in a name makes the label go and fetch a
 * resource, and an <a href> is a link the reader never chose. displayText() cannot close
 * that on its own and deliberately does not try: nearly everything it feeds is a delegate
 * drawing plain text, and a row reading "&amp;" where a name has an ampersand would be a
 * worse fault than the one being fixed.
 *
 * So the text is escaped and then wrapped in <html>...</html>, which is how Qt is told
 * outright that a string is rich text. Said outright, the heuristic is not consulted at
 * all, and nothing inside can be markup because every character that could have been has
 * been escaped. The alternative - keeping a tooltip on the plain-text path - means
 * arranging for mightBeRichText() to answer no, and that predicate is Qt's own, is
 * documented as a heuristic, and already answers yes to a bare "&lt;" and to every one of
 * the element names its parser knows. Nor is it asked here and acted on: wrapping only the
 * strings it objects to would be correct, Qt asking the same question of the same string -
 * but then what a model returns depends on which characters are in a name, and a reader of
 * one of them would have to know the predicate to know which of two shapes they have. One
 * shape, always.
 *
 * The newlines become <br>, because this is markup now and markup folds a literal newline
 * into a space; and the paragraph is set white-space:pre, because it also folds a run of
 * spaces into one - which would be this client quietly rewriting a name into a different
 * name, the very thing displayText() is careful not to do. Together they are also what
 * makes a wrapped tooltip lay out at the size the plain one did, to the pixel, in each
 * shape the models build: one line, several, or two with a blank line between them.
 *
 * Applied after displayText() and after the join, never before either. Escaped first, the
 * bound would fall on markup instead of on characters - an ampersand is one character to
 * read and five to write, so a name made of them would be cut to a fifth of what it is
 * allowed, and the cut could land inside an entity and draw as "&am". Reduced after the
 * join, the newlines a model put between its own fields would be flattened along with the
 * remote text; which is why displayText() stays where it is, on each field, one at a time,
 * and this is the single pass over the finished tooltip.
 *
 * An empty string stays empty. A tooltip with no text is not shown at all, where a wrapper
 * around nothing is an empty box that is.
 */
QString literalHtml(const QString& text);

}

}

#endif // HAVE_QUICKDC_QUI_RICHTEXT_H

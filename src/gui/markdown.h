/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_MARKDOWN_H
#define HAVE_QUICKDC_QUI_MARKDOWN_H

#include <QList>
#include <QString>

namespace Qdc {

/**
 * CommonMark, parsed to a tree.
 *
 * The whole of CommonMark 0.31.2 except raw HTML, plus two GFM extensions - tables and
 * strikethrough - and bare URLs, which chat has always linked. Conformance is not a
 * claim: tst_markdown runs the specification's own examples through parse() and a
 * canonical renderer and counts them, so what is and is not supported is a number that
 * cannot drift without a test failing.
 *
 * Raw HTML is the one deliberate omission, and it is not a matter of effort. Every
 * string handed to parse() was written by a stranger on a hub, and the reason this
 * exists rather than QTextDocument::setMarkdown() is that a renderer which accepts
 * markup from a stranger has given away the thing it was protecting. An HTML block and
 * an inline tag therefore parse as the characters they are made of, exactly as any other
 * unsupported construct would.
 *
 * Parsing is separate from rendering on purpose. This produces a tree and knows nothing
 * about colours, indents, magnet URIs or which schemes may be followed - richtext.cpp
 * decides all of that from the tree, and the tree is also what attachmentsIn() walks. A
 * parser that also drew things would have to be tested through a QTextDocument.
 */
namespace Markdown {

enum class Kind {
	Document,

	/* Blocks. */
	Paragraph,
	Heading,
	ThematicBreak,
	CodeBlock,
	BlockQuote,
	List,
	Item,
	Table,
	TableRow,
	TableCell,

	/* Inlines. */
	Text,
	SoftBreak,
	HardBreak,
	Emphasis,
	Strong,
	Strike,
	Code,
	Link,
	Image
};

/** A table column's alignment, from the delimiter row. */
enum class Align { None, Left, Centre, Right };

/**
 * One node of the tree.
 *
 * One struct for every kind rather than a class hierarchy: a tree of values copies,
 * compares and is walked with a switch, and the fields a kind does not use cost a few
 * bytes each. Which fields mean anything is written against them below.
 */
struct Node {
	Kind kind = Kind::Document;

	/** Text and Code: the content. CodeBlock: the body, newline separated. */
	QString literal;

	/** CodeBlock: the info string after the opening fence, if any. */
	QString info;

	/** Link and Image: where it points, already unescaped and percent-encoded. */
	QString destination;

	/** Link and Image: the optional title. */
	QString title;

	/** Heading: 1 to 6. List: the number the first item is written with. */
	int level = 0;

	/** List: whether it counts. */
	bool ordered = false;

	/**
	 * List: no blank line between its items, so the items hold no paragraphs.
	 *
	 * A loose list is the same tree with a Paragraph inside each Item; a renderer that
	 * does not care can ignore this and read the children.
	 */
	bool tight = true;

	/** TableRow: this is the header row rather than a body row. */
	bool header = false;

	/** TableCell: what the delimiter row said about its column. */
	Align align = Align::None;

	QList<Node> children;
};

/** Parse a document. Never fails: anything unrecognised is text. */
Node parse(const QString& text);

/**
 * Standard CommonMark HTML.
 *
 * The yardstick, and nothing else: this is what the specification's examples are
 * compared against, so that "how much of CommonMark works" has an answer that is
 * checked rather than asserted. The chat feed does not use it - see
 * RichText::renderMarkdown(), which renders the same tree the way a message should look.
 */
QString toCanonicalHtml(const Node& document);

}

}

#endif // HAVE_QUICKDC_QUI_MARKDOWN_H

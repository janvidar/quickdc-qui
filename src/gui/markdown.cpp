/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "markdown.h"

#include "markdownentities.h"

#include <QHash>
#include <QStringList>

#include <cstring>
#include <memory>
#include <vector>

namespace {

using Qdc::Markdown::Align;
using Qdc::Markdown::Kind;
using Qdc::Markdown::Node;

/** A tab advances to the next multiple of this, which is what makes indents ambiguous. */
constexpr int TAB_STOP = 4;

/** Four columns of indent is a code block, and is the cause of most of the arithmetic. */
constexpr int CODE_INDENT = 4;

/* ---- characters ---- */

bool is_ascii_punct(QChar c)
{
	const char16_t u = c.unicode();

	return (u >= u'!' && u <= u'/') || (u >= u':' && u <= u'@')
	    || (u >= u'[' && u <= u'`') || (u >= u'{' && u <= u'~');
}

/**
 * A punctuation character as CommonMark defines it, which is wider than ASCII.
 *
 * It decides whether a delimiter run may open or close emphasis, so an em dash beside an
 * asterisk has to count the same way a hyphen does.
 */
bool is_punctuation(QChar c)
{
	if (is_ascii_punct(c)) return true;

	switch (c.category())
	{
		case QChar::Punctuation_Connector:
		case QChar::Punctuation_Dash:
		case QChar::Punctuation_Close:
		case QChar::Punctuation_FinalQuote:
		case QChar::Punctuation_InitialQuote:
		case QChar::Punctuation_Other:
		case QChar::Punctuation_Open:
		case QChar::Symbol_Currency:
		case QChar::Symbol_Modifier:
		case QChar::Symbol_Math:
		case QChar::Symbol_Other:
			return true;

		default:
			return false;
	}
}

bool is_unicode_space(QChar c)
{
	const char16_t u = c.unicode();

	if (u == u' ' || u == u'\t' || u == u'\n' || u == u'\v' || u == u'\f' || u == u'\r')
		return true;

	return c.category() == QChar::Separator_Space;
}

bool is_space_or_tab(QChar c)
{
	return c == QLatin1Char(' ') || c == QLatin1Char('\t');
}

/**
 * Whitespace as a link's parts are separated by, which is ASCII only.
 *
 * A no-break space between a destination and a title does not separate them - it is part
 * of the destination - so this cannot be the Unicode question.
 */
bool is_ascii_space(QChar c)
{
	const char16_t u = c.unicode();

	return u == u' ' || u == u'\t' || u == u'\n' || u == u'\v' || u == u'\f'
	    || u == u'\r';
}

/** The value of a named character reference, or empty when there is no such name. */
QString entity_value(const QString& name)
{
	const QByteArray key = name.toLatin1();

	int low = 0;
	int high = Qdc::Markdown::NAMED_ENTITY_COUNT - 1;

	while (low <= high)
	{
		const int middle = (low + high) / 2;
		const int order =
			std::strcmp(key.constData(), Qdc::Markdown::NAMED_ENTITIES[middle].name);

		if (order == 0)
			return QString::fromUtf16(Qdc::Markdown::NAMED_ENTITIES[middle].value);

		if (order < 0) high = middle - 1;
		else low = middle + 1;
	}

	return QString();
}

/**
 * Read a character reference at 'at', returning how many characters it spans.
 *
 * Zero for anything that is not one, which is the common case: an ampersand in prose is
 * an ampersand, and only a complete and known reference is anything else.
 */
int scan_entity(const QString& text, int at, QString* value)
{
	const int n = text.size();
	if (at >= n || text.at(at) != QLatin1Char('&')) return 0;

	int i = at + 1;

	if (i < n && text.at(i) == QLatin1Char('#'))
	{
		++i;

		const bool hex = i < n
			&& (text.at(i) == QLatin1Char('x') || text.at(i) == QLatin1Char('X'));
		if (hex) ++i;

		int digits = 0;
		char32_t number = 0;

		for (; i < n; ++i)
		{
			const QChar c = text.at(i);
			int digit = -1;

			if (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
				digit = c.unicode() - u'0';
			else if (hex && c.toLower() >= QLatin1Char('a') && c.toLower() <= QLatin1Char('f'))
				digit = c.toLower().unicode() - u'a' + 10;

			if (digit < 0) break;

			number = number * char32_t(hex ? 16 : 10) + char32_t(digit);
			if (++digits > 8) return 0;
		}

		if (digits == 0 || digits > (hex ? 6 : 7)) return 0;
		if (i >= n || text.at(i) != QLatin1Char(';')) return 0;
		++i;

		/* Nothing may decode to a null, an unpaired surrogate or past the last plane. */
		if (number == 0 || number > 0x10FFFF || (number >= 0xD800 && number <= 0xDFFF))
			number = 0xFFFD;

		*value = QString::fromUcs4(&number, 1);
		return i - at;
	}

	const int from = i;
	while (i < n && text.at(i).unicode() < 128 && text.at(i).isLetterOrNumber()) ++i;

	if (i == from || i >= n || text.at(i) != QLatin1Char(';')) return 0;

	const QString found = entity_value(text.mid(from, i - from));
	if (found.isEmpty()) return 0;

	*value = found;
	return i + 1 - at;
}

/** Backslash escapes and character references resolved, for a destination or a title. */
QString unescape(const QString& text)
{
	QString out;
	out.reserve(text.size());

	for (int i = 0; i < text.size(); )
	{
		const QChar c = text.at(i);

		if (c == QLatin1Char('\\') && i + 1 < text.size()
		    && is_ascii_punct(text.at(i + 1)))
		{
			out += text.at(i + 1);
			i += 2;
			continue;
		}

		QString entity;
		if (const int span = scan_entity(text, i, &entity))
		{
			out += entity;
			i += span;
			continue;
		}

		out += c;
		++i;
	}

	return out;
}

/**
 * A link label reduced to what two labels are the same by.
 *
 * Case folded and with internal whitespace collapsed, so that "[Foo  bar]" and
 * "[foo bar]" name one definition.
 */
QString normalise_label(const QString& label)
{
	QString out;
	bool space = false;

	for (const QChar c : label.trimmed())
	{
		if (is_unicode_space(c)) { space = true; continue; }
		if (space && !out.isEmpty()) out += QLatin1Char(' ');
		space = false;
		out += c;
	}

	return out.toCaseFolded();
}

/* ---- blocks ---- */

/**
 * A block while it is being built.
 *
 * Not a Node: the parser holds pointers into this tree for the whole of a document and a
 * QList of values would move under them. The tree is converted once, at the end.
 */
struct Block {
	Kind kind = Kind::Document;

	std::vector<std::unique_ptr<Block>> children;
	Block* parent = nullptr;

	bool open = true;

	/**
	 * Whether the last line this block saw was blank.
	 *
	 * Kept per block rather than as one flag on the parser, because it is what decides
	 * whether a list is loose, and that question is asked of an item long after the
	 * blank line went past.
	 */
	bool lastLineBlank = false;

	/** Leaf content, one entry per line. */
	QStringList lines;

	/* List and Item. */
	bool ordered = false;
	int start = 1;
	QChar marker;
	int contentIndent = 0;
	bool tight = true;

	/* CodeBlock. */
	bool fenced = false;
	QChar fenceChar;
	int fenceLength = 0;
	int fenceIndent = 0;
	QString info;

	/* Heading. */
	int level = 0;

	/* Table. */
	QList<Align> aligns;

	Block* lastChild() const
	{
		return children.empty() ? nullptr : children.back().get();
	}
};

/** Whether a block of one kind may hold a block of another. */
bool can_contain(Kind parent, Kind child)
{
	switch (parent)
	{
		case Kind::Document:
		case Kind::BlockQuote:
		case Kind::Item:
			return child != Kind::Item;

		/* A list holds nothing but its items, so a paragraph beside one goes further up. */
		case Kind::List:
			return child == Kind::Item;

		default:
			return false;
	}
}

/** Whether a block of this kind may hold a paragraph's worth of text. */
bool accepts_lines(Kind kind)
{
	return kind == Kind::Paragraph || kind == Kind::Heading || kind == Kind::CodeBlock
	    || kind == Kind::Table;
}

/** A link reference definition, by its normalised label. */
struct Reference {
	QString destination;
	QString title;
};

/**
 * The whole of the block phase, and the line arithmetic it needs.
 *
 * Written as one object rather than a set of functions because the position within the
 * current line is shared by everything that looks at it: a container that matches
 * consumes its own marker, and what is left is what the next thing sees.
 */
class BlockParser {

public:
	Node run(const QString& text);

private:
	void incorporateLine(const QString& line);

	/** How far past 'offset' the next character that is not a space or tab lies. */
	void findNextNonspace();

	void advanceOffset(int count, bool columns);

	enum class Continues { Yes, No, LineIsDone };

	Continues continueBlock(Block* block);
	void closeUnmatched();
	void markBlankLine(Block* where);
	Block* addChild(Kind kind, int offsetColumn);
	void addLine();
	void finalise(Block* block);
	void finaliseTree();

	bool tryBlockQuote();
	bool tryAtxHeading();
	bool tryCodeFence();
	bool trySetextHeading();
	bool tryThematicBreak();
	bool tryListItem();
	bool tryTable();
	bool tryIndentedCode();

	bool endsWithBlankLine(Block* block) const;
	void setListTightness(Block* list);

	int parseReferences(QString* content);

	QString line;
	int offset = 0;
	int column = 0;
	int nextNonspace = 0;
	int nextNonspaceColumn = 0;
	int indent = 0;
	bool indented = false;
	bool blank = false;
	bool partialTab = false;
	bool allMatched = true;

	/** The deepest block that was open before this line, for closeUnmatched(). */
	Block* oldTip = nullptr;

	/** The deepest block this line matched, which is as far as closing goes. */
	Block* lastMatched = nullptr;

	/**
	 * A leaf opened by the line being read.
	 *
	 * Its opening line is its marker and not its content: the text after "```" is the
	 * info string and has already been taken, and a heading's text was read where the
	 * hashes were counted.
	 */
	Block* justOpened = nullptr;

	std::unique_ptr<Block> document;
	Block* tip = nullptr;
	Block* container = nullptr;

	QHash<QString, Reference> references;
};

void BlockParser::findNextNonspace()
{
	int i = offset;
	int c = column;

	while (i < line.size())
	{
		const QChar at = line.at(i);

		if (at == QLatin1Char(' ')) { ++i; ++c; }
		else if (at == QLatin1Char('\t')) { ++i; c += TAB_STOP - (c % TAB_STOP); }
		else break;
	}

	blank = i >= line.size();
	nextNonspace = i;
	nextNonspaceColumn = c;
	indent = nextNonspaceColumn - column;
	indented = indent >= CODE_INDENT;
}

/**
 * Move past 'count' characters, or 'count' columns when a tab may be split.
 *
 * A tab that a container only half consumes is the awkward case: the rest of it is still
 * indentation for whatever comes next, so the position stays on the tab and the leftover
 * columns are remembered.
 */
void BlockParser::advanceOffset(int count, bool columns)
{
	while (count > 0 && offset < line.size())
	{
		if (line.at(offset) == QLatin1Char('\t'))
		{
			const int toTab = TAB_STOP - (column % TAB_STOP);

			if (columns)
			{
				partialTab = toTab > count;
				const int by = qMin(count, toTab);
				column += by;
				if (!partialTab) ++offset;
				count -= by;
			}
			else
			{
				partialTab = false;
				column += toTab;
				++offset;
				--count;
			}
		}
		else
		{
			partialTab = false;
			++offset;
			++column;
			--count;
		}
	}
}

Block* BlockParser::addChild(Kind kind, int offsetColumn)
{
	while (tip && tip->parent && !can_contain(tip->kind, kind))
	{
		Block* parent = tip->parent;
		finalise(tip);
		tip = parent;
	}

	auto owned = std::make_unique<Block>();
	owned->kind = kind;
	owned->parent = tip;
	owned->contentIndent = offsetColumn;

	Block* raw = owned.get();
	tip->children.push_back(std::move(owned));
	tip = raw;

	return raw;
}

void BlockParser::addLine()
{
	QString rest;

	/*
	 * The unconsumed half of a tab is written out as the spaces it stands for. Keeping
	 * the tab would put four columns back into a block that only had one of them.
	 */
	if (partialTab)
	{
		const int toTab = TAB_STOP - (column % TAB_STOP);
		rest = QString(toTab, QLatin1Char(' ')) + line.mid(offset + 1);
	}
	else
	{
		rest = line.mid(offset);
	}

	tip->lines.append(rest);
}

bool BlockParser::tryAtxHeading()
{
	if (indented || nextNonspace >= line.size()
	    || line.at(nextNonspace) != QLatin1Char('#'))
		return false;

	int hashes = 0;
	int i = nextNonspace;

	while (i < line.size() && line.at(i) == QLatin1Char('#')) { ++i; ++hashes; }

	if (hashes > 6) return false;
	if (i < line.size() && !is_space_or_tab(line.at(i))) return false;

	advanceOffset(indent, false);
	closeUnmatched();

	Block* heading = addChild(Kind::Heading, nextNonspaceColumn);
	heading->level = hashes;

	/* The text, with the opening hashes and any closing run of them taken off. */
	QString rest = line.mid(i).trimmed();

	int trailing = rest.size();
	while (trailing > 0 && rest.at(trailing - 1) == QLatin1Char('#')) --trailing;

	if (trailing < rest.size()
	    && (trailing == 0 || is_space_or_tab(rest.at(trailing - 1))))
		rest = rest.left(trailing).trimmed();

	heading->lines.append(rest);
	justOpened = heading;

	offset = line.size();
	return true;
}

bool BlockParser::tryCodeFence()
{
	if (indented || nextNonspace >= line.size()) return false;

	const QChar c = line.at(nextNonspace);
	if (c != QLatin1Char('`') && c != QLatin1Char('~')) return false;

	int length = 0;
	int i = nextNonspace;
	while (i < line.size() && line.at(i) == c) { ++i; ++length; }

	if (length < 3) return false;

	const QString rest = line.mid(i);

	/* A backtick fence's info string may not contain a backtick, or it is prose. */
	if (c == QLatin1Char('`') && rest.contains(QLatin1Char('`'))) return false;

	closeUnmatched();

	Block* code = addChild(Kind::CodeBlock, nextNonspaceColumn);
	code->fenced = true;
	code->fenceChar = c;
	code->fenceLength = length;
	code->fenceIndent = indent;
	code->info = unescape(rest.trimmed());
	justOpened = code;

	offset = line.size();
	return true;
}

bool BlockParser::trySetextHeading()
{
	if (indented || !container || container->kind != Kind::Paragraph) return false;
	if (container->lines.isEmpty() || nextNonspace >= line.size()) return false;

	const QChar c = line.at(nextNonspace);
	if (c != QLatin1Char('=') && c != QLatin1Char('-')) return false;

	int i = nextNonspace;
	while (i < line.size() && line.at(i) == c) ++i;
	while (i < line.size() && is_space_or_tab(line.at(i))) ++i;

	if (i < line.size()) return false;

	/*
	 * Definitions come off the front before anything is underlined. A paragraph that was
	 * nothing but definitions is not there to be a heading, and the underline becomes the
	 * first line of whatever follows.
	 */
	QString content = container->lines.join(QLatin1Char('\n'));

	if (parseReferences(&content) > 0)
	{
		if (content.trimmed().isEmpty())
		{
			container->lines.clear();
			return false;
		}

		container->lines = QStringList{content};
	}

	container->kind = Kind::Heading;
	container->level = (c == QLatin1Char('=')) ? 1 : 2;

	tip = container;
	justOpened = container;

	offset = line.size();
	return true;
}

bool BlockParser::tryThematicBreak()
{
	if (indented || nextNonspace >= line.size()) return false;

	const QChar c = line.at(nextNonspace);
	if (c != QLatin1Char('*') && c != QLatin1Char('-') && c != QLatin1Char('_'))
		return false;

	int count = 0;

	for (int i = nextNonspace; i < line.size(); ++i)
	{
		const QChar at = line.at(i);

		if (at == c) ++count;
		else if (!is_space_or_tab(at)) return false;
	}

	if (count < 3) return false;

	closeUnmatched();
	justOpened = addChild(Kind::ThematicBreak, nextNonspaceColumn);

	offset = line.size();
	return true;
}

bool BlockParser::tryBlockQuote()
{
	if (indented || nextNonspace >= line.size()
	    || line.at(nextNonspace) != QLatin1Char('>'))
		return false;

	advanceOffset(indent + 1, false);

	if (offset < line.size() && is_space_or_tab(line.at(offset)))
		advanceOffset(1, true);

	closeUnmatched();
	addChild(Kind::BlockQuote, nextNonspaceColumn);

	return true;
}

bool BlockParser::tryListItem()
{
	/* Four columns of indent is code, so a marker out there is not a marker at all. */
	if (indent >= CODE_INDENT) return false;
	if (nextNonspace >= line.size()) return false;

	const QChar c = line.at(nextNonspace);

	bool ordered = false;
	int start = 1;
	QChar marker;
	int i = nextNonspace;

	if (c == QLatin1Char('-') || c == QLatin1Char('+') || c == QLatin1Char('*'))
	{
		marker = c;
		++i;
	}
	else if (c.isDigit())
	{
		int digits = 0;
		int value = 0;

		while (i < line.size() && line.at(i).isDigit() && digits < 10)
		{
			value = value * 10 + (line.at(i).unicode() - u'0');
			++i;
			++digits;
		}

		if (digits > 9 || i >= line.size()) return false;

		const QChar after = line.at(i);
		if (after != QLatin1Char('.') && after != QLatin1Char(')')) return false;

		ordered = true;
		start = value;
		marker = after;
		++i;
	}
	else
	{
		return false;
	}

	/* The marker has to be followed by a space, a tab, or nothing at all. */
	const bool empty = i >= line.size();

	if (!empty && !is_space_or_tab(line.at(i))) return false;

	/*
	 * Interrupting a paragraph is only allowed for a list that starts at one and has
	 * something in its first item, so that "the year 2024. It was" stays prose.
	 */
	if (container && container->kind == Kind::Paragraph)
	{
		if (empty) return false;
		if (ordered && start != 1) return false;
	}

	const int markerOffset = indent;
	const int markerWidth = i - nextNonspace;

	advanceOffset(indent, false);
	advanceOffset(markerWidth, true);

	/*
	 * How far the content is indented: the marker, then one to four spaces. Five or more,
	 * or none at all, and the content is taken to begin one space past the marker - what
	 * is left over is indentation inside the item, which is how a code block gets into
	 * one.
	 */
	int padding = 1;

	if (!empty)
	{
		const int spacesStart = column;
		int scan = offset;
		int columns = column;

		while (scan < line.size() && is_space_or_tab(line.at(scan)))
		{
			if (line.at(scan) == QLatin1Char('\t'))
				columns += TAB_STOP - (columns % TAB_STOP);
			else
				++columns;

			++scan;
			if (columns - spacesStart > 4) break;
		}

		const bool allSpaces = scan >= line.size();
		padding = (allSpaces || columns - spacesStart > 4) ? 1 : columns - spacesStart;
	}

	advanceOffset(padding, true);

	/*
	 * Counted rather than measured from where the cursor stopped: an empty item has
	 * nothing after its marker to advance over, and its content is still indented.
	 */
	const int contentIndent = markerOffset + markerWidth + padding;

	closeUnmatched();

	/*
	 * A marker of a different sort starts a list of its own: "- a" then "* b" is two
	 * lists, which is what keeps a change of bullet from being invisible.
	 */
	if (!tip || tip->kind != Kind::List || tip->marker != marker
	    || tip->ordered != ordered)
	{
		Block* list = addChild(Kind::List, nextNonspaceColumn);
		list->ordered = ordered;
		list->start = start;
		list->marker = marker;
	}

	Block* item = addChild(Kind::Item, nextNonspaceColumn);
	item->contentIndent = contentIndent;
	item->marker = marker;

	return true;
}

/** Split a table row on the pipes that are not escaped. */
QStringList table_cells(const QString& row)
{
	QString trimmed = row.trimmed();

	if (trimmed.startsWith(QLatin1Char('|'))) trimmed = trimmed.mid(1);

	/* A closing pipe is decoration, unless it is escaped and so part of the last cell. */
	if (trimmed.endsWith(QLatin1Char('|')))
	{
		int slashes = 0;
		int i = trimmed.size() - 2;
		while (i >= 0 && trimmed.at(i) == QLatin1Char('\\')) { ++slashes; --i; }
		if (slashes % 2 == 0) trimmed.chop(1);
	}

	QStringList cells;
	QString current;

	for (int i = 0; i < trimmed.size(); ++i)
	{
		const QChar c = trimmed.at(i);

		if (c == QLatin1Char('\\') && i + 1 < trimmed.size()
		    && trimmed.at(i + 1) == QLatin1Char('|'))
		{
			current += QLatin1Char('|');
			++i;
			continue;
		}

		if (c == QLatin1Char('|'))
		{
			cells.append(current.trimmed());
			current.clear();
			continue;
		}

		current += c;
	}

	cells.append(current.trimmed());
	return cells;
}

/** The alignments a delimiter row declares, or empty when it is not one. */
QList<Align> table_alignments(const QString& row)
{
	QList<Align> aligns;

	if (!row.contains(QLatin1Char('-'))) return aligns;

	for (const QString& cell : table_cells(row))
	{
		const QString one = cell.trimmed();
		if (one.isEmpty()) return QList<Align>();

		const bool left = one.startsWith(QLatin1Char(':'));
		const bool right = one.endsWith(QLatin1Char(':'));

		const QString dashes =
			one.mid(left ? 1 : 0, one.size() - (left ? 1 : 0) - (right ? 1 : 0));

		if (dashes.isEmpty()) return QList<Align>();

		for (const QChar c : dashes)
			if (c != QLatin1Char('-')) return QList<Align>();

		if (left && right) aligns.append(Align::Centre);
		else if (left) aligns.append(Align::Left);
		else if (right) aligns.append(Align::Right);
		else aligns.append(Align::None);
	}

	return aligns;
}

/**
 * Four columns of indent, with no paragraph above it, is a code block.
 *
 * Tried after everything else, because every other construct is allowed up to three
 * columns of indent and this one is what the fourth means.
 */
bool BlockParser::tryIndentedCode()
{
	if (!indented) return false;
	if (tip && tip->kind == Kind::Paragraph) return false;
	if (blank) return false;

	advanceOffset(CODE_INDENT, true);
	closeUnmatched();

	justOpened = addChild(Kind::CodeBlock, column);
	return true;
}

bool BlockParser::tryTable()
{
	if (indented || !container || container->kind != Kind::Paragraph) return false;
	if (container->lines.size() != 1) return false;
	if (!line.contains(QLatin1Char('|')) && !line.contains(QLatin1Char('-'))) return false;

	const QList<Align> aligns = table_alignments(line.mid(nextNonspace));
	if (aligns.isEmpty()) return false;

	const QStringList header = table_cells(container->lines.first());
	if (header.size() != aligns.size()) return false;

	/* A single column needs a pipe somewhere, or "-" under a word is a setext heading. */
	if (aligns.size() == 1 && !line.contains(QLatin1Char('|'))) return false;

	container->kind = Kind::Table;
	container->aligns = aligns;
	tip = container;
	justOpened = container;

	offset = line.size();
	return true;
}

bool BlockParser::endsWithBlankLine(Block* block) const
{
	for (Block* at = block; at; at = at->lastChild())
	{
		if (at->lastLineBlank) return true;
		if (at->kind != Kind::List && at->kind != Kind::Item) return false;
	}

	return false;
}

void BlockParser::setListTightness(Block* list)
{
	for (const auto& item : list->children)
	{
		if (endsWithBlankLine(item.get()) && item.get() != list->children.back().get())
		{
			list->tight = false;
			return;
		}

		for (const auto& inner : item->children)
		{
			const bool last = inner.get() == item->children.back().get()
				&& item.get() == list->children.back().get();

			if (endsWithBlankLine(inner.get()) && !last)
			{
				list->tight = false;
				return;
			}
		}
	}
}

/**
 * Take any link reference definitions off the front of a paragraph.
 *
 * They are not content and leave nothing behind, so a paragraph that was nothing but
 * definitions disappears - which is why this reports what is left rather than editing in
 * place and hoping the caller notices.
 */
int BlockParser::parseReferences(QString* content)
{
	int taken = 0;

	forever
	{
		const QString text = *content;
		int i = 0;

		const auto skipSpace = [&](bool acrossOneNewline) {
			int newlines = 0;

			while (i < text.size() && is_ascii_space(text.at(i)))
			{
				if (text.at(i) == QLatin1Char('\n'))
				{
					if (!acrossOneNewline || ++newlines > 1) return false;
				}

				++i;
			}

			return true;
		};

		while (i < text.size() && is_space_or_tab(text.at(i))) ++i;
		if (i >= text.size() || text.at(i) != QLatin1Char('[')) break;

		/* The label, which may hold escaped brackets but no unescaped ones. */
		const int labelFrom = ++i;
		bool closed = false;

		while (i < text.size())
		{
			const QChar c = text.at(i);

			if (c == QLatin1Char('\\') && i + 1 < text.size()) { i += 2; continue; }
			if (c == QLatin1Char(']')) { closed = true; break; }
			if (c == QLatin1Char('[')) break;

			++i;
		}

		if (!closed) break;

		const QString label = text.mid(labelFrom, i - labelFrom);
		if (label.trimmed().isEmpty() || label.size() > 999) break;

		++i;
		if (i >= text.size() || text.at(i) != QLatin1Char(':')) break;
		++i;

		if (!skipSpace(true)) break;

		/* The destination, in angle brackets or as a bare run without spaces. */
		QString destination;
		bool haveDestination = false;

		if (i < text.size() && text.at(i) == QLatin1Char('<'))
		{
			const int from = ++i;

			while (i < text.size())
			{
				const QChar c = text.at(i);

				if (c == QLatin1Char('\\') && i + 1 < text.size()) { i += 2; continue; }
				if (c == QLatin1Char('>')) break;
				if (c == QLatin1Char('<') || c == QLatin1Char('\n')) break;

				++i;
			}

			if (i < text.size() && text.at(i) == QLatin1Char('>'))
			{
				destination = text.mid(from, i - from);
				haveDestination = true;
				++i;
			}
		}
		else
		{
			const int from = i;
			int depth = 0;

			while (i < text.size())
			{
				const QChar c = text.at(i);

				if (c == QLatin1Char('\\') && i + 1 < text.size()) { i += 2; continue; }
				if (is_ascii_space(c)) break;
				if (c.unicode() < 0x20 || c.unicode() == 0x7f) break;

				if (c == QLatin1Char('(')) ++depth;
				else if (c == QLatin1Char(')')) { if (--depth < 0) break; }

				++i;
			}

			if (i > from && depth == 0)
			{
				destination = text.mid(from, i - from);
				haveDestination = true;
			}
		}

		if (!haveDestination) break;

		/* A title, which may be on the next line - but only if there is nothing else. */
		const int afterDestination = i;
		int newlines = 0;

		while (i < text.size() && is_ascii_space(text.at(i)))
		{
			if (text.at(i) == QLatin1Char('\n') && ++newlines > 1) break;
			++i;
		}

		QString title;
		bool haveTitle = false;

		if (newlines <= 1 && i < text.size() && i > afterDestination)
		{
			const QChar open = text.at(i);
			const QChar close = (open == QLatin1Char('(')) ? QLatin1Char(')') : open;

			if (open == QLatin1Char('"') || open == QLatin1Char('\'')
			    || open == QLatin1Char('('))
			{
				const int from = ++i;
				bool done = false;

				while (i < text.size())
				{
					const QChar c = text.at(i);

					if (c == QLatin1Char('\\') && i + 1 < text.size()) { i += 2; continue; }
					if (c == open && open == QLatin1Char('(')) break;
					if (c == close) { done = true; break; }

					++i;
				}

				if (done)
				{
					title = text.mid(from, i - from);
					haveTitle = true;
					++i;
				}
			}
		}

		/*
		 * Nothing but the end of the line may follow. Where that line ends depends on
		 * whether a title was found: without one, the destination ends it, and the
		 * newline crossed while looking for a title belongs to whatever comes next.
		 */
		const auto endOfLineFrom = [&text](int from, int* stop) {
			int at = from;
			while (at < text.size() && is_space_or_tab(text.at(at))) ++at;

			*stop = at;
			return at >= text.size() || text.at(at) == QLatin1Char('\n');
		};

		int after = i;

		if (!haveTitle)
		{
			i = afterDestination;
			if (!endOfLineFrom(i, &after)) break;
		}
		else if (!endOfLineFrom(i, &after))
		{
			/* The title stood on a line of its own and belonged to something else. */
			i = afterDestination;
			title.clear();

			if (!endOfLineFrom(i, &after)) break;
		}

		const QString key = normalise_label(label);
		if (!key.isEmpty() && !references.contains(key))
			references.insert(key, Reference{unescape(destination), unescape(title)});

		++taken;

		while (after < text.size() && text.at(after) == QLatin1Char('\n')) ++after;
		*content = text.mid(after);

		if (content->isEmpty()) break;
	}

	return taken;
}

void BlockParser::finalise(Block* block)
{
	if (!block || !block->open) return;

	block->open = false;

	switch (block->kind)
	{
		case Kind::Paragraph:
		{
			QString content = block->lines.join(QLatin1Char('\n'));

			if (parseReferences(&content) > 0 && content.trimmed().isEmpty())
			{
				/* Nothing but definitions: the paragraph itself was never content. */
				block->kind = Kind::Paragraph;
				block->lines.clear();
				return;
			}

			while (content.endsWith(QLatin1Char('\n')) || content.endsWith(QLatin1Char(' '))
			       || content.endsWith(QLatin1Char('\t')))
				content.chop(1);

			block->lines = QStringList{content};
			break;
		}

		case Kind::Heading:
		{
			QString content = block->lines.join(QLatin1Char('\n')).trimmed();
			block->lines = QStringList{content};
			break;
		}

		case Kind::CodeBlock:
		{
			if (!block->fenced)
			{
				/* An indented block keeps its interior blank lines and drops the rest. */
				while (!block->lines.isEmpty()
				       && block->lines.last().trimmed().isEmpty())
					block->lines.removeLast();
			}

			break;
		}

		case Kind::List:
			setListTightness(block);
			break;

		default:
			break;
	}
}

/** Close everything still open at the end of the document, innermost first. */
void BlockParser::finaliseTree()
{
	while (tip)
	{
		Block* parent = tip->parent;
		finalise(tip);
		tip = parent;
	}
}

/**
 * Whether 'block' carries on into the line being read, consuming whatever marks it.
 *
 * Every kind answers for itself, and a leaf answers too - which is the point. A line
 * beginning "> " has to be able to end an indented code block and open a quote, and it
 * only gets that chance if the code block is asked first and says no.
 */
BlockParser::Continues BlockParser::continueBlock(Block* block)
{
	switch (block->kind)
	{
		case Kind::Document:
		case Kind::List:
			return Continues::Yes;

		case Kind::BlockQuote:
		{
			if (indented || nextNonspace >= line.size()
			    || line.at(nextNonspace) != QLatin1Char('>'))
				return Continues::No;

			advanceOffset(indent + 1, false);

			if (offset < line.size() && is_space_or_tab(line.at(offset)))
				advanceOffset(1, true);

			return Continues::Yes;
		}

		case Kind::Item:
		{
			if (blank)
			{
				/* A blank line straight after the marker leaves the item empty. */
				if (block->children.empty()) return Continues::No;

				advanceOffset(nextNonspace - offset, false);
				return Continues::Yes;
			}

			if (indent >= block->contentIndent)
			{
				advanceOffset(block->contentIndent, true);
				return Continues::Yes;
			}

			return Continues::No;
		}

		case Kind::Paragraph:
			return blank ? Continues::No : Continues::Yes;

		case Kind::Heading:
			return Continues::No;

		case Kind::CodeBlock:
		{
			if (!block->fenced)
			{
				if (indent >= CODE_INDENT)
				{
					advanceOffset(CODE_INDENT, true);
					return Continues::Yes;
				}

				if (blank)
				{
					advanceOffset(nextNonspace - offset, false);
					return Continues::Yes;
				}

				return Continues::No;
			}

			/* A closing fence is the same character, at least as long, and alone. */
			if (!indented && nextNonspace < line.size()
			    && line.at(nextNonspace) == block->fenceChar)
			{
				int length = 0;
				int i = nextNonspace;

				while (i < line.size() && line.at(i) == block->fenceChar)
				{ ++i; ++length; }

				int after = i;
				while (after < line.size() && is_space_or_tab(line.at(after))) ++after;

				if (length >= block->fenceLength && after >= line.size())
				{
					finalise(block);
					tip = block->parent;
					return Continues::LineIsDone;
				}
			}

			/* Only as much of the opening indent as this line actually has. */
			int strip = block->fenceIndent;

			while (strip > 0 && offset < line.size() && is_space_or_tab(line.at(offset)))
			{
				advanceOffset(1, true);
				--strip;
			}

			return Continues::Yes;
		}

		case Kind::Table:
		{
			/* A pipe is what makes a row; a line without one is what follows the table. */
			if (blank || !line.mid(nextNonspace).contains(QLatin1Char('|')))
				return Continues::No;

			advanceOffset(indent, false);
			return Continues::Yes;
		}

		default:
			return Continues::No;
	}
}

void BlockParser::closeUnmatched()
{
	if (allMatched) return;

	while (oldTip && oldTip != lastMatched)
	{
		Block* parent = oldTip->parent;
		finalise(oldTip);
		oldTip = parent;
	}

	tip = lastMatched;
	allMatched = true;
}

void BlockParser::incorporateLine(const QString& raw)
{
	line = raw;
	offset = 0;
	column = 0;
	partialTab = false;
	justOpened = nullptr;

	oldTip = tip;
	container = document.get();

	/*
	 * Down through everything that is already open, each block consuming what marks it.
	 * The first that will not continue stops the descent, and where it stopped decides
	 * whether what follows is a lazy paragraph continuation or something new.
	 */
	forever
	{
		Block* child = container->lastChild();
		if (!child || !child->open) break;

		container = child;
		findNextNonspace();

		const Continues answer = continueBlock(container);

		if (answer == Continues::LineIsDone)
		{
			markBlankLine(container);
			return;
		}

		if (answer == Continues::No)
		{
			container = container->parent;
			break;
		}
	}

	allMatched = container == oldTip;
	lastMatched = container;

	/*
	 * Now look for something new to open, until the deepest open block is a leaf that
	 * takes lines. A paragraph does not stop the search: several constructs are allowed
	 * to interrupt one.
	 */
	bool matchedLeaf = container->kind != Kind::Paragraph && accepts_lines(container->kind);

	while (!matchedLeaf)
	{
		findNextNonspace();

		if (tryBlockQuote() || tryAtxHeading() || tryCodeFence() || trySetextHeading()
		    || tryThematicBreak() || tryTable() || tryListItem() || tryIndentedCode())
		{
			container = tip;
			matchedLeaf = container->kind != Kind::Paragraph
				&& accepts_lines(container->kind);
			continue;
		}

		advanceOffset(indent, false);
		break;
	}

	findNextNonspace();

	/*
	 * A paragraph whose container did not match carries on regardless. That is lazy
	 * continuation, and it is why a quoted paragraph need not repeat its marker.
	 */
	if (!allMatched && !blank && tip && tip->kind == Kind::Paragraph)
	{
		advanceOffset(indent, false);
		addLine();
		return;
	}

	closeUnmatched();

	if (blank && container->lastChild()) container->lastChild()->lastLineBlank = true;

	markBlankLine(container);

	if (accepts_lines(container->kind))
	{
		if (container != justOpened || (container->kind == Kind::CodeBlock
		                                && !container->fenced))
			addLine();
	}
	else if (!blank && offset < line.size())
	{
		advanceOffset(indent, false);
		tip = container;
		addChild(Kind::Paragraph, column);
		addLine();
	}
}

void BlockParser::markBlankLine(Block* where)
{
	if (!where) return;

	const bool counts = blank
		&& where->kind != Kind::BlockQuote
		&& where->kind != Kind::Heading
		&& !(where->kind == Kind::CodeBlock && where->fenced)
		&& !(where->kind == Kind::Item && where->children.empty());

	for (Block* at = where; at; at = at->parent) at->lastLineBlank = counts;
}

}

/* ---- inlines ---- */

namespace {

/**
 * An inline while it is being built.
 *
 * A tree with parent pointers rather than a flat list, because the two things that
 * happen to inlines both move a run of siblings under a new node: emphasis wraps what
 * lies between two delimiters, and a link wraps what lies after its bracket. Once those
 * nodes have a new parent, the delimiter stack still points at them - so every splice
 * has to work wherever the node has ended up, which is what a parent pointer is for.
 */
struct Inl {
	Kind kind = Kind::Text;

	QString text;
	QString destination;
	QString title;

	Inl* parent = nullptr;
	Inl* prev = nullptr;
	Inl* next = nullptr;
	Inl* firstChild = nullptr;
	Inl* lastChild = nullptr;
};

/** Take a node out of whatever list it is in. */
void unlink(Inl* node)
{
	if (node->prev) node->prev->next = node->next;
	else if (node->parent) node->parent->firstChild = node->next;

	if (node->next) node->next->prev = node->prev;
	else if (node->parent) node->parent->lastChild = node->prev;

	node->parent = nullptr;
	node->prev = nullptr;
	node->next = nullptr;
}

void append_child(Inl* parent, Inl* node)
{
	unlink(node);

	node->parent = parent;
	node->prev = parent->lastChild;

	if (parent->lastChild) parent->lastChild->next = node;
	else parent->firstChild = node;

	parent->lastChild = node;
}

/** Put 'node' immediately after 'after', in whatever list that is. */
void insert_after(Inl* after, Inl* node)
{
	node->parent = after->parent;
	node->prev = after;
	node->next = after->next;

	if (after->next) after->next->prev = node;
	else if (after->parent) after->parent->lastChild = node;

	after->next = node;
}

/** One run of emphasis characters, waiting to be paired with another. */
struct Delimiter {
	Inl* node = nullptr;
	QChar ch;
	int length = 0;
	int originalLength = 0;
	bool canOpen = false;
	bool canClose = false;
	Delimiter* prev = nullptr;
	Delimiter* next = nullptr;
};

/** An opening bracket, waiting for the "]" that may turn it into a link. */
struct Bracket {
	Inl* node = nullptr;
	Bracket* prev = nullptr;
	Delimiter* delimitersBelow = nullptr;
	int position = 0;
	bool image = false;
	bool active = true;
};

class InlineParser {

public:
	InlineParser(const QHash<QString, Reference>& refs) : m_refs(refs) {}

	QList<Node> parse(const QString& text);

private:
	Inl* make(Kind kind);
	void append(Inl* node);

	void handleNewline();
	bool handleBackslash();
	bool handleEntity();
	bool handleCode();
	bool handleAutolink();
	bool handleDelimiterRun(QChar c);
	void handleOpenBracket(bool image);
	bool handleCloseBracket();

	void scanDelimiterRun(QChar c, int* length, bool* canOpen, bool* canClose);
	void processEmphasis(Delimiter* bottom);
	void removeDelimiter(Delimiter* what);

	bool readDestinationAndTitle(int* at, QString* destination, QString* title);
	QString readLabel(int* at);

	QList<Node> convert(Inl* first);

	QString m_text;
	int m_pos = 0;

	/** The document all inlines hang from, so that every node has a parent. */
	Inl* m_root = nullptr;

	Delimiter* m_delimiters = nullptr;
	Bracket* m_brackets = nullptr;

	std::vector<std::unique_ptr<Inl>> m_nodes;
	std::vector<std::unique_ptr<Delimiter>> m_delimiterStore;
	std::vector<std::unique_ptr<Bracket>> m_bracketStore;

	const QHash<QString, Reference>& m_refs;
};

Inl* InlineParser::make(Kind kind)
{
	auto owned = std::make_unique<Inl>();
	owned->kind = kind;

	Inl* raw = owned.get();
	m_nodes.push_back(std::move(owned));

	return raw;
}

void InlineParser::append(Inl* node)
{
	append_child(m_root, node);
}

void InlineParser::removeDelimiter(Delimiter* what)
{
	if (what->prev) what->prev->next = what->next;
	if (what->next) what->next->prev = what->prev;
	if (m_delimiters == what) m_delimiters = what->prev;
}

/**
 * Whether a run of 'c' at the current position may open emphasis, close it, or both.
 *
 * This is the whole of CommonMark's flanking rule, and it is why "2 * 3 * 4" is
 * arithmetic while "*two*" is emphasis: what counts is the characters on either side of
 * the run, not the run itself.
 */
void InlineParser::scanDelimiterRun(QChar c, int* length, bool* canOpen, bool* canClose)
{
	int count = 0;
	int i = m_pos;

	while (i < m_text.size() && m_text.at(i) == c) { ++i; ++count; }

	const QChar before = m_pos > 0 ? m_text.at(m_pos - 1) : QChar(u'\n');
	const QChar after = i < m_text.size() ? m_text.at(i) : QChar(u'\n');

	const bool spaceBefore = is_unicode_space(before);
	const bool spaceAfter = is_unicode_space(after);
	const bool punctBefore = is_punctuation(before);
	const bool punctAfter = is_punctuation(after);

	const bool leftFlanking = !spaceAfter
		&& (!punctAfter || spaceBefore || punctBefore);
	const bool rightFlanking = !spaceBefore
		&& (!punctBefore || spaceAfter || punctAfter);

	*length = count;

	if (c == QLatin1Char('_'))
	{
		/*
		 * Underscore may not open or close inside a word, which is what keeps
		 * snake_case intact where asterisks would emphasise.
		 */
		*canOpen = leftFlanking && (!rightFlanking || punctBefore);
		*canClose = rightFlanking && (!leftFlanking || punctAfter);
	}
	else
	{
		*canOpen = leftFlanking;
		*canClose = rightFlanking;
	}
}

bool InlineParser::handleDelimiterRun(QChar c)
{
	int length = 0;
	bool canOpen = false;
	bool canClose = false;

	scanDelimiterRun(c, &length, &canOpen, &canClose);

	/* Strikethrough is one or two tildes and nothing longer. */
	if (c == QLatin1Char('~') && length > 2) { canOpen = false; canClose = false; }

	Inl* node = make(Kind::Text);
	node->text = QString(length, c);
	append(node);

	m_pos += length;

	if (!canOpen && !canClose) return true;

	auto owned = std::make_unique<Delimiter>();
	owned->node = node;
	owned->ch = c;
	owned->length = length;
	owned->originalLength = length;
	owned->canOpen = canOpen;
	owned->canClose = canClose;
	owned->prev = m_delimiters;

	Delimiter* raw = owned.get();
	m_delimiterStore.push_back(std::move(owned));

	if (m_delimiters) m_delimiters->next = raw;
	m_delimiters = raw;

	return true;
}

/**
 * Pair up whatever emphasis delimiters are above 'bottom'.
 *
 * The "rule of three" below is the part nobody guesses: a run that can both open and
 * close cannot pair with one whose combined length is a multiple of three, which is what
 * makes "*foo**bar**baz*" come out as one emphasis around a strong one.
 */
void InlineParser::processEmphasis(Delimiter* bottom)
{
	Delimiter* openersBottom[3][2][3] = {};

	for (int a = 0; a < 3; ++a)
		for (int b = 0; b < 2; ++b)
			for (int d = 0; d < 3; ++d)
				openersBottom[a][b][d] = bottom;

	/* Down to the first closer above the bottom of the stack. */
	Delimiter* closer = m_delimiters;
	while (closer && closer->prev != bottom) closer = closer->prev;

	while (closer)
	{
		if (!closer->canClose) { closer = closer->next; continue; }

		const int which = closer->ch == QLatin1Char('*') ? 0
			: (closer->ch == QLatin1Char('_') ? 1 : 2);

		Delimiter* opener = closer->prev;
		Delimiter* found = nullptr;

		while (opener && opener != bottom
		       && opener != openersBottom[which][closer->canOpen ? 1 : 0]
		                                 [closer->originalLength % 3])
		{
			if (opener->canOpen && opener->ch == closer->ch)
			{
				const bool oddMatch =
					(closer->canOpen || opener->canClose)
					&& closer->originalLength % 3 != 0
					&& (opener->originalLength + closer->originalLength) % 3 == 0;

				if (!oddMatch) { found = opener; break; }
			}

			opener = opener->prev;
		}

		Delimiter* nextCloser = closer->next;

		if (!found)
		{
			openersBottom[which][closer->canOpen ? 1 : 0][closer->originalLength % 3] =
				closer->prev;

			if (!closer->canOpen) removeDelimiter(closer);

			closer = nextCloser;
			continue;
		}

		/* Two of the character is strong, one is emphasis, and a tilde is a strike. */
		int used = 1;

		if (closer->ch == QLatin1Char('~'))
			used = qMin(found->length, closer->length);
		else if (found->length >= 2 && closer->length >= 2)
			used = 2;

		const Kind kind = closer->ch == QLatin1Char('~')
			? Kind::Strike
			: (used == 2 ? Kind::Strong : Kind::Emphasis);

		Inl* wrapper = make(kind);
		insert_after(found->node, wrapper);

		/* Everything between the two runs becomes the emphasised content. */
		for (Inl* at = wrapper->next; at && at != closer->node; )
		{
			Inl* following = at->next;
			append_child(wrapper, at);
			at = following;
		}

		found->length -= used;
		closer->length -= used;
		found->node->text.chop(used);
		closer->node->text = closer->node->text.mid(used);

		/* Anything left between them was never paired, so it is text. */
		for (Delimiter* between = closer->prev; between && between != found; )
		{
			Delimiter* below = between->prev;
			removeDelimiter(between);
			between = below;
		}

		if (found->length == 0)
		{
			unlink(found->node);
			removeDelimiter(found);
		}

		if (closer->length == 0)
		{
			unlink(closer->node);

			Delimiter* following = closer->next;
			removeDelimiter(closer);
			closer = following;
		}
	}

	/* Whatever is left above the bottom never paired and stays as its own text. */
	while (m_delimiters && m_delimiters != bottom)
	{
		Delimiter* below = m_delimiters->prev;
		removeDelimiter(m_delimiters);
		m_delimiters = below;
	}
}

void InlineParser::handleNewline()
{
	/*
	 * Two spaces before the break make it hard; so does a backslash, which is handled
	 * where the backslash is read. Either way the spaces themselves are not content.
	 */
	bool hard = false;

	Inl* before = m_root->lastChild;

	if (before && before->kind == Kind::Text && before->text.endsWith(QLatin1Char(' ')))
	{
		int spaces = 0;
		int i = before->text.size() - 1;

		while (i >= 0 && before->text.at(i) == QLatin1Char(' ')) { ++spaces; --i; }

		hard = spaces >= 2;
		before->text.chop(spaces);

		if (before->text.isEmpty()) unlink(before);
	}

	append(make(hard ? Kind::HardBreak : Kind::SoftBreak));
	++m_pos;

	/* Whitespace at the start of the next line is not content either. */
	while (m_pos < m_text.size() && is_space_or_tab(m_text.at(m_pos))) ++m_pos;
}

bool InlineParser::handleBackslash()
{
	if (m_pos + 1 < m_text.size() && is_ascii_punct(m_text.at(m_pos + 1)))
	{
		Inl* node = make(Kind::Text);
		node->text = m_text.at(m_pos + 1);
		append(node);
		m_pos += 2;
		return true;
	}

	if (m_pos + 1 < m_text.size() && m_text.at(m_pos + 1) == QLatin1Char('\n'))
	{
		append(make(Kind::HardBreak));
		m_pos += 2;

		while (m_pos < m_text.size() && is_space_or_tab(m_text.at(m_pos))) ++m_pos;
		return true;
	}

	Inl* node = make(Kind::Text);
	node->text = QStringLiteral("\\");
	append(node);
	++m_pos;

	return true;
}

bool InlineParser::handleEntity()
{
	QString value;
	const int span = scan_entity(m_text, m_pos, &value);
	if (span == 0) return false;

	Inl* node = make(Kind::Text);
	node->text = value;
	append(node);
	m_pos += span;

	return true;
}

bool InlineParser::handleCode()
{
	int fence = 0;
	int i = m_pos;

	while (i < m_text.size() && m_text.at(i) == QLatin1Char('`')) { ++i; ++fence; }

	/* A run of the same length, and no shorter one in between. */
	int at = i;

	while (at < m_text.size())
	{
		if (m_text.at(at) != QLatin1Char('`')) { ++at; continue; }

		int length = 0;
		const int from = at;

		while (at < m_text.size() && m_text.at(at) == QLatin1Char('`')) { ++at; ++length; }

		if (length != fence) continue;

		QString content = m_text.mid(i, from - i);
		content.replace(QLatin1Char('\n'), QLatin1Char(' '));

		/*
		 * One space either side comes off, which is what lets a span hold a backtick of
		 * its own - but only when there is something else in there too.
		 */
		if (content.size() >= 2 && content.startsWith(QLatin1Char(' '))
		    && content.endsWith(QLatin1Char(' ')) && content.trimmed().size() > 0)
			content = content.mid(1, content.size() - 2);

		Inl* node = make(Kind::Code);
		node->text = content;
		append(node);

		m_pos = at;
		return true;
	}

	Inl* node = make(Kind::Text);
	node->text = QString(fence, QLatin1Char('`'));
	append(node);
	m_pos = i;

	return true;
}

bool InlineParser::handleAutolink()
{
	const int n = m_text.size();
	if (m_pos + 1 >= n) return false;

	/* An absolute URI: a scheme, a colon, then anything but space or angle brackets. */
	int i = m_pos + 1;
	int schemeLength = 0;

	if (i < n && m_text.at(i).isLetter())
	{
		while (i < n)
		{
			const QChar c = m_text.at(i);

			if (c.isLetterOrNumber() || c == QLatin1Char('+') || c == QLatin1Char('.')
			    || c == QLatin1Char('-'))
			{
				++i;
				++schemeLength;
				continue;
			}

			break;
		}
	}

	if (schemeLength >= 2 && schemeLength <= 32 && i < n && m_text.at(i) == QLatin1Char(':'))
	{
		int at = i + 1;

		while (at < n)
		{
			const QChar c = m_text.at(at);

			if (c == QLatin1Char('>')) break;
			if (is_unicode_space(c) || c == QLatin1Char('<')) return false;
			if (c.unicode() < 0x20) return false;

			++at;
		}

		if (at < n && m_text.at(at) == QLatin1Char('>'))
		{
			const QString target = m_text.mid(m_pos + 1, at - m_pos - 1);

			Inl* link = make(Kind::Link);

			/* An autolink's target is taken as written: no escapes are resolved in it. */
			link->destination = target;

			Inl* label = make(Kind::Text);
			label->text = target;

			append(link);
			append_child(link, label);

			m_pos = at + 1;
			return true;
		}
	}

	/* An email address, which becomes a mailto: link. */
	static const QString allowed = QStringLiteral(".!#$%&'*+/=?^_`{|}~-");

	int local = 0;
	i = m_pos + 1;

	while (i < n)
	{
		const QChar c = m_text.at(i);
		if (c.isLetterOrNumber() || allowed.contains(c)) { ++i; ++local; continue; }
		break;
	}

	if (local == 0 || i >= n || m_text.at(i) != QLatin1Char('@')) return false;

	++i;
	int labelLength = 0;
	int domain = 0;

	while (i < n)
	{
		const QChar c = m_text.at(i);

		if (c.isLetterOrNumber()) { ++i; ++labelLength; ++domain; continue; }
		if (c == QLatin1Char('-') && labelLength > 0) { ++i; ++labelLength; ++domain; continue; }

		if (c == QLatin1Char('.') && labelLength > 0
		    && m_text.at(i - 1) != QLatin1Char('-'))
		{
			++i;
			labelLength = 0;
			++domain;
			continue;
		}

		break;
	}

	if (domain == 0 || labelLength == 0 || i >= n || m_text.at(i) != QLatin1Char('>'))
		return false;

	if (m_text.at(i - 1) == QLatin1Char('-')) return false;

	const QString address = m_text.mid(m_pos + 1, i - m_pos - 1);

	Inl* link = make(Kind::Link);
	link->destination = QStringLiteral("mailto:") + address;

	Inl* label = make(Kind::Text);
	label->text = address;

	append(link);
	append_child(link, label);

	m_pos = i + 1;

	return true;
}

void InlineParser::handleOpenBracket(bool image)
{
	Inl* node = make(Kind::Text);
	node->text = image ? QStringLiteral("![") : QStringLiteral("[");
	append(node);

	auto owned = std::make_unique<Bracket>();
	owned->node = node;
	owned->prev = m_brackets;
	owned->delimitersBelow = m_delimiters;
	owned->image = image;
	owned->position = m_pos + (image ? 2 : 1);

	m_brackets = owned.get();
	m_bracketStore.push_back(std::move(owned));

	m_pos += image ? 2 : 1;
}

/** Read a bracketed label starting at '[', leaving 'at' past the ']'. */
QString InlineParser::readLabel(int* at)
{
	const int n = m_text.size();
	int i = *at;

	if (i >= n || m_text.at(i) != QLatin1Char('[')) return QString();

	const int from = ++i;

	while (i < n)
	{
		const QChar c = m_text.at(i);

		if (c == QLatin1Char('\\') && i + 1 < n) { i += 2; continue; }

		/* An unescaped bracket either ends the label or means this was never one. */
		if (c == QLatin1Char('[')) return QString();
		if (c == QLatin1Char(']')) break;

		++i;
	}

	if (i >= n || m_text.at(i) != QLatin1Char(']')) return QString();
	if (i - from > 999) return QString();

	*at = i + 1;
	return m_text.mid(from, i - from);
}

/** Read "(destination "title")" at 'at', which is what makes a link inline. */
bool InlineParser::readDestinationAndTitle(int* at, QString* destination, QString* title)
{
	const int n = m_text.size();
	int i = *at;

	if (i >= n || m_text.at(i) != QLatin1Char('(')) return false;
	++i;

	while (i < n && is_ascii_space(m_text.at(i))) ++i;

	QString dest;

	if (i < n && m_text.at(i) == QLatin1Char('<'))
	{
		const int from = ++i;

		while (i < n)
		{
			const QChar c = m_text.at(i);

			if (c == QLatin1Char('\\') && i + 1 < n) { i += 2; continue; }
			if (c == QLatin1Char('>')) break;
			if (c == QLatin1Char('<') || c == QLatin1Char('\n')) return false;

			++i;
		}

		if (i >= n) return false;

		dest = m_text.mid(from, i - from);
		++i;
	}
	else
	{
		const int from = i;
		int depth = 0;

		while (i < n)
		{
			const QChar c = m_text.at(i);

			if (c == QLatin1Char('\\') && i + 1 < n) { i += 2; continue; }
			if (is_ascii_space(c)) break;
			if (c.unicode() < 0x20 || c.unicode() == 0x7f) break;

			if (c == QLatin1Char('(')) { ++depth; ++i; continue; }

			if (c == QLatin1Char(')'))
			{
				if (depth == 0) break;
				--depth;
				++i;
				continue;
			}

			++i;
		}

		if (depth != 0) return false;
		dest = m_text.mid(from, i - from);
	}

	int spaces = 0;
	while (i < n && is_ascii_space(m_text.at(i))) { ++i; ++spaces; }

	QString foundTitle;

	if (i < n && (m_text.at(i) == QLatin1Char('"') || m_text.at(i) == QLatin1Char('\'')
	              || m_text.at(i) == QLatin1Char('(')))
	{
		if (spaces == 0 && !dest.isEmpty()) return false;

		const QChar open = m_text.at(i);
		const QChar close = open == QLatin1Char('(') ? QLatin1Char(')') : open;

		const int from = ++i;
		bool closed = false;

		while (i < n)
		{
			const QChar c = m_text.at(i);

			if (c == QLatin1Char('\\') && i + 1 < n) { i += 2; continue; }
			if (c == open && open == QLatin1Char('(')) return false;
			if (c == close) { closed = true; break; }

			++i;
		}

		if (!closed) return false;

		foundTitle = m_text.mid(from, i - from);
		++i;

		while (i < n && is_ascii_space(m_text.at(i))) ++i;
	}

	if (i >= n || m_text.at(i) != QLatin1Char(')')) return false;

	*destination = unescape(dest);
	*title = unescape(foundTitle);
	*at = i + 1;

	return true;
}

bool InlineParser::handleCloseBracket()
{
	Bracket* opener = m_brackets;
	++m_pos;

	if (!opener)
	{
		Inl* node = make(Kind::Text);
		node->text = QStringLiteral("]");
		append(node);
		return true;
	}

	if (!opener->active)
	{
		m_brackets = opener->prev;

		Inl* node = make(Kind::Text);
		node->text = QStringLiteral("]");
		append(node);
		return true;
	}

	QString destination;
	QString title;
	bool matched = false;

	int at = m_pos;

	if (readDestinationAndTitle(&at, &destination, &title))
	{
		matched = true;
		m_pos = at;
	}
	else
	{
		/* A reference, in one of its three forms: full, collapsed, or nothing at all. */
		QString label;
		int after = m_pos;
		bool written = false;

		if (after < m_text.size() && m_text.at(after) == QLatin1Char('['))
		{
			const int from = after;
			const QString read = readLabel(&after);

			if (read.isNull()) after = from;
			else { label = read; written = true; }
		}

		/* Nothing between the brackets means the label is the link's own text. */
		const bool collapsed = label.trimmed().isEmpty();

		if (collapsed)
			label = m_text.mid(opener->position, m_pos - 1 - opener->position);

		const auto found = m_refs.constFind(normalise_label(label));

		if (found != m_refs.constEnd())
		{
			destination = found->destination;
			title = found->title;
			matched = true;

			/* Only a reference that resolved consumes the brackets it was written in. */
			m_pos = after;
		}
		else if (written && collapsed)
		{
			m_pos = after;
		}
	}

	if (!matched)
	{
		m_brackets = opener->prev;

		Inl* node = make(Kind::Text);
		node->text = QStringLiteral("]");
		append(node);
		return true;
	}

	Inl* wrapper = make(opener->image ? Kind::Image : Kind::Link);
	wrapper->destination = destination;
	wrapper->title = title;

	/*
	 * Everything after the opening bracket is the label's content, and it moves under the
	 * link before the emphasis inside it is paired up - the delimiters still point at
	 * those nodes, which is why they have to keep working once they have moved.
	 */
	insert_after(opener->node, wrapper);

	for (Inl* at = wrapper->next; at; )
	{
		Inl* following = at->next;
		append_child(wrapper, at);
		at = following;
	}

	/* The bracket's own text is gone, replaced by what it turned into. */
	unlink(opener->node);

	processEmphasis(opener->delimitersBelow);

	/* No link inside a link: an earlier bracket cannot become one now. */
	if (!opener->image)
		for (Bracket* earlier = opener->prev; earlier; earlier = earlier->prev)
			if (!earlier->image) earlier->active = false;

	m_brackets = opener->prev;

	return true;
}

QList<Node> InlineParser::convert(Inl* first)
{
	QList<Node> out;

	for (Inl* at = first; at; at = at->next)
	{
		Node node;
		node.kind = at->kind;
		node.literal = at->text;
		node.destination = at->destination;
		node.title = at->title;
		node.children = convert(at->firstChild);

		/* Runs that ended up beside each other read as one piece of text. */
		if (node.kind == Kind::Text && !out.isEmpty() && out.last().kind == Kind::Text)
			out.last().literal += node.literal;
		else
			out.append(node);
	}

	return out;
}

QList<Node> InlineParser::parse(const QString& text)
{
	m_text = text;
	m_pos = 0;
	m_root = make(Kind::Document);
	m_delimiters = nullptr;
	m_brackets = nullptr;

	const int n = m_text.size();

	while (m_pos < n)
	{
		const QChar c = m_text.at(m_pos);
		bool handled = false;

		switch (c.unicode())
		{
			case u'\n':
				handleNewline();
				handled = true;
				break;

			case u'\\':
				handled = handleBackslash();
				break;

			case u'`':
				handled = handleCode();
				break;

			case u'&':
				handled = handleEntity();
				break;

			case u'<':
				handled = handleAutolink();
				break;

			case u'*':
			case u'_':
			case u'~':
				handled = handleDelimiterRun(c);
				break;

			case u'[':
				handleOpenBracket(false);
				handled = true;
				break;

			case u'!':
				if (m_pos + 1 < n && m_text.at(m_pos + 1) == QLatin1Char('['))
				{
					handleOpenBracket(true);
					handled = true;
				}
				break;

			case u']':
				handled = handleCloseBracket();
				break;

			default:
				break;
		}

		if (handled) continue;

		/* Ordinary text, up to the next character that might mean something. */
		const int from = m_pos;
		++m_pos;

		while (m_pos < n)
		{
			const char16_t u = m_text.at(m_pos).unicode();

			if (u == u'\n' || u == u'\\' || u == u'`' || u == u'&' || u == u'<'
			    || u == u'*' || u == u'_' || u == u'~' || u == u'[' || u == u']'
			    || u == u'!')
				break;

			++m_pos;
		}

		Inl* node = make(Kind::Text);
		node->text = m_text.mid(from, m_pos - from);
		append(node);
	}

	processEmphasis(nullptr);

	return convert(m_root->firstChild);
}

}

/* ---- the tree ---- */

namespace {

/** Turn the parser's blocks into the tree the callers see, parsing inlines on the way. */
Node to_node(Block* block, const QHash<QString, Reference>& refs)
{
	Node node;
	node.kind = block->kind;
	node.level = block->level;
	node.ordered = block->ordered;
	node.tight = block->tight;
	node.info = block->info;

	switch (block->kind)
	{
		case Kind::Paragraph:
		case Kind::Heading:
		{
			InlineParser inlines(refs);
			node.children = inlines.parse(block->lines.value(0));
			break;
		}

		case Kind::CodeBlock:
		{
			QString body = block->lines.join(QLatin1Char('\n'));
			if (!block->lines.isEmpty()) body += QLatin1Char('\n');
			node.literal = body;
			break;
		}

		case Kind::List:
			node.level = block->start;
			break;

		case Kind::Table:
		{
			for (int row = 0; row < block->lines.size(); ++row)
			{
				Node line;
				line.kind = Kind::TableRow;
				line.header = row == 0;

				const QStringList cells = table_cells(block->lines.at(row));

				for (int n = 0; n < block->aligns.size(); ++n)
				{
					Node cell;
					cell.kind = Kind::TableCell;
					cell.align = block->aligns.at(n);
					cell.header = row == 0;

					if (n < cells.size())
					{
						InlineParser inlines(refs);
						cell.children = inlines.parse(cells.at(n));
					}

					line.children.append(cell);
				}

				node.children.append(line);
			}

			return node;
		}

		default:
			break;
	}

	for (const auto& child : block->children)
	{
		/* A paragraph that was nothing but link definitions leaves no node behind. */
		if (child->kind == Kind::Paragraph && child->lines.isEmpty()) continue;

		node.children.append(to_node(child.get(), refs));
	}

	return node;
}

QStringList split_lines(const QString& text)
{
	QString normalised = text;
	normalised.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
	normalised.replace(QLatin1Char('\r'), QLatin1Char('\n'));

	/* A null is not something a document may contain; the spec replaces it. */
	normalised.replace(QChar(u'\0'), QChar(0xFFFD));

	QStringList lines = normalised.split(QLatin1Char('\n'));

	/* A trailing newline ends the last line rather than starting an empty one. */
	if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();

	return lines;
}

}

Node BlockParser::run(const QString& text)
{
	document = std::make_unique<Block>();
	document->kind = Kind::Document;
	tip = document.get();

	for (const QString& one : split_lines(text)) incorporateLine(one);

	finaliseTree();

	/* Definitions are collected while paragraphs close, so the tree is built after. */
	return to_node(document.get(), references);
}

Node Qdc::Markdown::parse(const QString& text)
{
	BlockParser parser;
	return parser.run(text);
}

/* ---- canonical HTML ---- */

namespace {

QString escape_html(const QString& text)
{
	QString out;
	out.reserve(text.size());

	for (const QChar c : text)
	{
		switch (c.unicode())
		{
			case u'<': out += QStringLiteral("&lt;"); break;
			case u'>': out += QStringLiteral("&gt;"); break;
			case u'&': out += QStringLiteral("&amp;"); break;
			case u'"': out += QStringLiteral("&quot;"); break;
			default: out += c; break;
		}
	}

	return out;
}

/** A destination as an href: percent-encoded where it has to be, then escaped. */
QString escape_href(const QString& url)
{
	static const QString safe =
		QStringLiteral("-_.+!*'(),%#@?=;:/,+&$~");

	QString out;

	for (const QByteArray& utf8 = url.toUtf8(); const char raw : utf8)
	{
		const uchar c = uchar(raw);

		if (c == '&') { out += QStringLiteral("&amp;"); continue; }
		if (c == '\'') { out += QStringLiteral("&#x27;"); continue; }

		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
		    || safe.contains(QChar(char16_t(c))))
		{
			out += QChar(char16_t(c));
			continue;
		}

		out += QLatin1Char('%')
			+ QStringLiteral("%1").arg(c, 2, 16, QLatin1Char('0')).toUpper();
	}

	return out;
}

/**
 * Start a line, unless one has just been started.
 *
 * Every block element begins on a line of its own, and this is what puts it there. It is
 * the difference between "<li>foo\n<ul>" and "<li>foo<ul>": in a tight item the
 * paragraph writes no tags at all, so the newline before the nested list has nowhere
 * else to come from.
 */
void cr(QString* out)
{
	if (!out->isEmpty() && !out->endsWith(QLatin1Char('\n'))) *out += QLatin1Char('\n');
}

void render_canonical(const Node& node, QString* out, bool tight);

void render_children(const Node& node, QString* out, bool tight)
{
	for (const Node& child : node.children) render_canonical(child, out, tight);
}

/** The alt text of an image: its content with every marker gone. */
QString plain_of(const Node& node)
{
	QString out;

	switch (node.kind)
	{
		case Kind::Text:
		case Kind::Code:
			return escape_html(node.literal);

		case Kind::SoftBreak:
		case Kind::HardBreak:
			return QStringLiteral("\n");

		default:
			break;
	}

	for (const Node& child : node.children) out += plain_of(child);

	return out;
}

void render_canonical(const Node& node, QString* out, bool tight)
{
	switch (node.kind)
	{
		case Kind::Document:
			render_children(node, out, false);
			break;

		case Kind::Paragraph:
			if (tight) { render_children(node, out, false); break; }
			cr(out);
			*out += QStringLiteral("<p>");
			render_children(node, out, false);
			*out += QStringLiteral("</p>\n");
			break;

		case Kind::Heading:
			cr(out);
			*out += QStringLiteral("<h%1>").arg(node.level);
			render_children(node, out, false);
			*out += QStringLiteral("</h%1>\n").arg(node.level);
			break;

		case Kind::ThematicBreak:
			cr(out);
			*out += QStringLiteral("<hr />\n");
			break;

		case Kind::CodeBlock:
		{
			cr(out);

			const QString info = node.info.section(QLatin1Char(' '), 0, 0);

			*out += info.isEmpty()
				? QStringLiteral("<pre><code>")
				: QStringLiteral("<pre><code class=\"language-%1\">")
					.arg(escape_html(info));

			*out += escape_html(node.literal);
			*out += QStringLiteral("</code></pre>\n");
			break;
		}

		case Kind::BlockQuote:
			cr(out);
			*out += QStringLiteral("<blockquote>\n");
			render_children(node, out, false);
			cr(out);
			*out += QStringLiteral("</blockquote>\n");
			break;

		case Kind::List:
			cr(out);

			if (node.ordered)
			{
				*out += node.level == 1
					? QStringLiteral("<ol>\n")
					: QStringLiteral("<ol start=\"%1\">\n").arg(node.level);
			}
			else
			{
				*out += QStringLiteral("<ul>\n");
			}

			for (const Node& item : node.children) render_canonical(item, out, node.tight);

			cr(out);
			*out += node.ordered ? QStringLiteral("</ol>\n") : QStringLiteral("</ul>\n");
			break;

		case Kind::Item:
			cr(out);
			*out += QStringLiteral("<li>");

			for (const Node& child : node.children) render_canonical(child, out, tight);

			*out += QStringLiteral("</li>\n");
			break;

		case Kind::Table:
		{
			*out += QStringLiteral("<table>\n");

			bool inBody = false;

			for (const Node& row : node.children)
			{
				if (row.header) *out += QStringLiteral("<thead>\n");
				else if (!inBody) { *out += QStringLiteral("<tbody>\n"); inBody = true; }

				render_canonical(row, out, false);

				if (row.header) *out += QStringLiteral("</thead>\n");
			}

			if (inBody) *out += QStringLiteral("</tbody>\n");

			*out += QStringLiteral("</table>\n");
			break;
		}

		case Kind::TableRow:
			*out += QStringLiteral("<tr>\n");
			render_children(node, out, false);
			*out += QStringLiteral("</tr>\n");
			break;

		case Kind::TableCell:
		{
			const QString tag = node.header ? QStringLiteral("th") : QStringLiteral("td");
			QString attribute;

			switch (node.align)
			{
				case Align::Left: attribute = QStringLiteral(" align=\"left\""); break;
				case Align::Centre: attribute = QStringLiteral(" align=\"center\""); break;
				case Align::Right: attribute = QStringLiteral(" align=\"right\""); break;
				case Align::None: break;
			}

			*out += QStringLiteral("<%1%2>").arg(tag, attribute);
			render_children(node, out, false);
			*out += QStringLiteral("</%1>\n").arg(tag);
			break;
		}

		case Kind::Text:
			*out += escape_html(node.literal);
			break;

		case Kind::SoftBreak:
			*out += QStringLiteral("\n");
			break;

		case Kind::HardBreak:
			*out += QStringLiteral("<br />\n");
			break;

		case Kind::Emphasis:
			*out += QStringLiteral("<em>");
			render_children(node, out, false);
			*out += QStringLiteral("</em>");
			break;

		case Kind::Strong:
			*out += QStringLiteral("<strong>");
			render_children(node, out, false);
			*out += QStringLiteral("</strong>");
			break;

		case Kind::Strike:
			*out += QStringLiteral("<del>");
			render_children(node, out, false);
			*out += QStringLiteral("</del>");
			break;

		case Kind::Code:
			*out += QStringLiteral("<code>") + escape_html(node.literal)
				+ QStringLiteral("</code>");
			break;

		case Kind::Link:
			*out += QStringLiteral("<a href=\"%1\"").arg(escape_href(node.destination));
			if (!node.title.isEmpty())
				*out += QStringLiteral(" title=\"%1\"").arg(escape_html(node.title));
			*out += QStringLiteral(">");
			render_children(node, out, false);
			*out += QStringLiteral("</a>");
			break;

		case Kind::Image:
			*out += QStringLiteral("<img src=\"%1\" alt=\"%2\"")
				.arg(escape_href(node.destination), plain_of(node));
			if (!node.title.isEmpty())
				*out += QStringLiteral(" title=\"%1\"").arg(escape_html(node.title));
			*out += QStringLiteral(" />");
			break;
	}
}

}

QString Qdc::Markdown::toCanonicalHtml(const Node& document)
{
	QString out;
	render_canonical(document, &out, false);

	return out;
}

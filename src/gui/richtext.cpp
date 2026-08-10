/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "richtext.h"

#include "markdown.h"

#include <QFontDatabase>
#include <QFileInfo>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QStringList>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextList>
#include <QTextFragment>

namespace {

/** Schemes a link may use. Everything else is left as text. */
const QStringList& external_schemes()
{
	static const QStringList schemes = {
		QStringLiteral("http"), QStringLiteral("https")
	};

	return schemes;
}

/** Hub schemes, which QuickDC opens itself. */
const QStringList& hub_schemes()
{
	static const QStringList schemes = {
		QStringLiteral("adc"), QStringLiteral("adcs"), QStringLiteral("dchub")
	};

	return schemes;
}

/**
 * Escape a plain run so that it survives being parsed back.
 *
 * Only what could change meaning, and no more: every backslash is noise on a client that
 * does not know this extension, and a message full of them is worse for its reader than
 * one where an underscore is occasionally taken for emphasis. So the decision is made per
 * character, in context.
 */
QString escape_markdown(const QString& text)
{
	const int tildes = text.count(QLatin1Char('~'));

	QString out;
	out.reserve(text.size());

	for (int i = 0; i < text.size(); ++i)
	{
		const QChar c = text.at(i);
		const QChar before = i > 0 ? text.at(i - 1) : QChar();
		const QChar after = i + 1 < text.size() ? text.at(i + 1) : QChar();

		bool escape = false;

		switch (c.unicode())
		{
			case u'\\':
			case u'`':
			case u'*':
			case u'[':
			case u']':
				escape = true;
				break;

			case u'_':
				/*
				 * Underscore cannot emphasise inside a word, which is what lets
				 * snake_case through untouched where an asterisk would have to be
				 * escaped.
				 */
				escape = !(before.isLetterOrNumber() && after.isLetterOrNumber());
				break;

			case u'~':
				/* A lone tilde pairs with nothing, so "~/Music" needs no help. */
				escape = tildes > 1;
				break;

			case u'<':
				/* Only where it could open an autolink or look like a tag. */
				escape = after.isLetter() || after == QLatin1Char('/')
					|| after == QLatin1Char('!') || after == QLatin1Char('?');
				break;

			case u'&':
				/* Only where it could begin a character reference. */
				escape = after.isLetter() || after == QLatin1Char('#');
				break;

			default:
				break;
		}

		if (escape) out += QLatin1Char('\\');
		out += c;
	}

	return out;
}

/**
 * The longest run of backticks in 'text'.
 *
 * A code span has to be fenced by more backticks than it contains, or the first one
 * inside it ends the span.
 */
int longest_backtick_run(const QString& text)
{
	int longest = 0;
	int run = 0;

	for (const QChar c : text)
	{
		run = (c == QLatin1Char('`')) ? run + 1 : 0;
		longest = qMax(longest, run);
	}

	return longest;
}

/** Wrap 'text' as a code span, with a fence long enough to contain it. */
QString code_span(const QString& text)
{
	const QString fence(longest_backtick_run(text) + 1, QLatin1Char('`'));

	/*
	 * A space either side when the content begins or ends with a backtick, which is
	 * how CommonMark keeps the fence from merging with the content. The reader
	 * strips one space from each end.
	 */
	const bool pad = text.startsWith(QLatin1Char('`')) || text.endsWith(QLatin1Char('`'));
	const QString gap = pad ? QStringLiteral(" ") : QString();

	return fence + gap + text + gap + fence;
}

/** Newlines as they appear inside one paragraph of a QTextDocument. */
QString normalise_breaks(const QString& text)
{
	QString normalised = text;

	/* Shift+Return inserts a line separator rather than starting a new block. */
	normalised.replace(QChar::LineSeparator, QLatin1Char('\n'));
	normalised.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
	normalised.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
	normalised.replace(QLatin1Char('\r'), QLatin1Char('\n'));

	return normalised;
}

Qdc::RichText::BlockKind kind_of(const QTextBlock& block)
{
	const QVariant kind =
		block.blockFormat().property(Qdc::RichText::BlockKindProperty);

	if (kind.isValid()) return static_cast<Qdc::RichText::BlockKind>(kind.toInt());

	/*
	 * A QTextList the property knows nothing about, which is what a paste from a browser
	 * arrives as. Read rather than ignored, so a pasted list is still sent as one.
	 */
	if (const QTextList* list = block.textList())
	{
		switch (list->format().style())
		{
			case QTextListFormat::ListDecimal:
			case QTextListFormat::ListLowerAlpha:
			case QTextListFormat::ListUpperAlpha:
			case QTextListFormat::ListLowerRoman:
			case QTextListFormat::ListUpperRoman:
				return Qdc::RichText::Number;

			default:
				return Qdc::RichText::Bullet;
		}
	}

	return Qdc::RichText::Paragraph;
}


bool is_code(const QTextCharFormat& format)
{
	return format.property(Qdc::RichText::CodeProperty).toBool();
}

bool is_bold(const QTextCharFormat& format)
{
	return format.fontWeight() >= QFont::Bold;
}

/* ---- rendering ---- */

enum class Mode { Html, Plain };

QString emit_text(const QString& text, Mode mode)
{
	if (mode == Mode::Plain) return text;

	QString escaped = text.toHtmlEscaped();
	escaped.replace(QLatin1Char('\n'), QStringLiteral("<br>"));

	return escaped;
}

/**
 * Where a bare URL ends.
 *
 * Trailing punctuation is left out, because a sentence usually ends after a link and
 * "see https://example.org/rules." should not link the full stop.
 */
int url_end(const QString& text, int from)
{
	int end = from;
	while (end < text.size() && !text.at(end).isSpace()) ++end;

	/*
	 * The asterisk is here for the sake of a message that is not markdown, where it is
	 * how people have always emphasised a word by hand: an address written between two
	 * of them is an address between two markers, not one ending in a marker. In a
	 * formatted message emphasis has already been parsed away before this runs, so it
	 * costs nothing there.
	 */
	static const QString trailing = QStringLiteral(".,;:!?'\")]}*");
	while (end > from && trailing.contains(text.at(end - 1))) --end;

	return end;
}

/**
 * A scheme this client would open, starting at 'at', or -1.
 *
 * Two shapes, because not every URI names a host. http, https and the hub schemes are
 * followed by "://"; a magnet has no authority at all and is followed by nothing but
 * its colon. The bare colon is accepted only for magnet, deliberately: taking any word
 * followed by one would make a link of "note:" and of the time of day.
 */
int scheme_length(const QString& text, int at)
{
	int n = at;
	while (n < text.size())
	{
		const QChar c = text.at(n);
		if (c.isLetterOrNumber() || c == QLatin1Char('+') || c == QLatin1Char('.')
		    || c == QLatin1Char('-'))
		{
			++n;
			continue;
		}
		break;
	}

	if (n == at || !text.at(at).isLetter()) return -1;

	const QStringView rest = QStringView(text).mid(n);

	if (rest.startsWith(QStringLiteral("://"))) return n - at;

	if (rest.startsWith(QLatin1Char(':'))
	    && QStringView(text).mid(at, n - at)
	           .compare(QStringLiteral("magnet"), Qt::CaseInsensitive) == 0)
	{
		return n - at;
	}

	return -1;
}

QString wrap_link(const QString& label, const QUrl& url, Mode mode,
                  const Qdc::RichText::RenderColours& colours)
{
	if (mode == Mode::Plain) return label;

	/*
	 * The href is the one piece of a message that is not just displayed, so it is
	 * checked against a fixed set of schemes before it becomes an anchor at all. An
	 * address that does not qualify falls back to being shown as text.
	 */
	return QStringLiteral("<a href=\"%1\" style=\"color:%2\">%3</a>")
		.arg(url.toString(QUrl::FullyEncoded).toHtmlEscaped(),
		     colours.link.name(), label);
}

/**
 * Walk one line of markdown, emitting either HTML or bare text.
 *
 * One scanner rather than a chain of regular expressions: precedence between code,
 * links and emphasis has to be decided at each position, and a code span has to swallow
 * whatever is inside it without the other rules getting a look.
 */
/**
 * Draw an attachment as a chip: what the file is called, and how big it is.
 *
 * A hash is not something anybody can read, so the link's own text is never shown. The
 * name comes from the URI and the size beside it is what tells somebody whether they
 * want it - which is the whole decision, since nothing has been fetched yet.
 */
/**
 * Draw a picture that is here, scaled to the room it may have.
 *
 * Both dimensions are given explicitly rather than left to the layout, so the aspect
 * ratio survives and a large photograph cannot push a message off the screen. The image
 * sits inside its own anchor, so clicking it reaches the same place clicking a chip does
 * and can open the file at its full size.
 */
QString picture_for(const Qdc::RichText::Attachment& file, const QUrl& url,
                    const Qdc::RichText::LocalMedia& here,
                    const Qdc::RichText::RenderOptions& options)
{
	QSize shown = here.size;

	const int room = options.mediaWidth > 0 ? options.mediaWidth : here.size.width();

	if (shown.width() > room)
		shown = QSize(room, shown.height() * room / shown.width());

	if (options.mediaHeight > 0 && shown.height() > options.mediaHeight)
	{
		shown = QSize(shown.width() * options.mediaHeight / shown.height(),
		              options.mediaHeight);
	}

	/*
	 * The source is the path this client recorded for the hash, never anything the
	 * message carried. A sender chooses which file to point at, not where a file is.
	 */
	/*
	 * On lines of its own. Left in the flow of the sentence, a picture three hundred
	 * pixels tall makes a line box that tall as well, which drops the speaker's name to
	 * the foot of it and leaves the words stranded beside the image.
	 */
	return QStringLiteral(
		"<br><a href=\"%1\"><img src=\"%2\" width=\"%3\" height=\"%4\" "
		"alt=\"%5\"></a><br>")
		.arg(url.toString(QUrl::FullyEncoded).toHtmlEscaped(),
		     here.path.toHtmlEscaped())
		.arg(qMax(1, shown.width())).arg(qMax(1, shown.height()))
		.arg(file.name.toHtmlEscaped());
}

QString chip_for(const Qdc::RichText::Attachment& file, const QUrl& url, Mode mode,
                 const Qdc::RichText::RenderColours& colours)
{
	const QString size = file.size > 0
		? Qdc::RichText::describeSize(file.size) : QStringLiteral("size unknown");

	if (mode == Mode::Plain)
		return QStringLiteral("%1 (%2)").arg(file.name, size);

	/*
	 * An anchor, so a click reaches the feed the same way any other link does, and the
	 * only thing that has to know a magnet from a web address is whoever follows it.
	 */
	return QStringLiteral(
		"<a href=\"%1\" style=\"color:%2; text-decoration:none\">"
		"<span style=\"background-color:%3; color:%2\">"
		"&nbsp;\U0001F4C4&nbsp;%4&nbsp;&middot;&nbsp;%5&nbsp;</span></a>")
		.arg(url.toString(QUrl::FullyEncoded).toHtmlEscaped(),
		     colours.chip.name(), colours.chipBack.name(),
		     file.name.toHtmlEscaped(), size.toHtmlEscaped());
}

/**
 * Wrap a block's body in whatever the whole message is set in.
 *
 * Applied to the body and not to the prefix, so a muted hub notice still has its
 * timestamp in the timestamp's colour and a "/me" line still has its speaker in theirs.
 */
QString style_body(const QString& content, const Qdc::RichText::RenderOptions& options)
{
	QString out = content;

	if (options.body.isValid())
	{
		out = QStringLiteral("<span style=\"color:%1\">%2</span>")
			.arg(options.body.name(), out);
	}

	if (options.italic) out = QStringLiteral("<i>%1</i>").arg(out);

	return out;
}

/**
 * Put emphasis markers around a run, leaving any whitespace at its edges outside them.
 *
 * Markdown cannot emphasise a leading or trailing space - a marker beside whitespace is
 * not read as one - so "* hi *" would arrive as literal asterisks. Moving them inwards
 * keeps the run's own spacing and still says what was meant.
 */
QString wrap_emphasis(const QString& text, const QString& marker)
{
	int lead = 0;
	while (lead < text.size() && text.at(lead).isSpace()) ++lead;

	/* Nothing but whitespace: there is no word here to emphasise. */
	if (lead == text.size()) return text;

	int trail = 0;
	while (trail < text.size() - lead
	       && text.at(text.size() - 1 - trail).isSpace())
	{
		++trail;
	}

	const QString core = text.mid(lead, text.size() - lead - trail);

	return text.left(lead) + marker + core + marker + text.right(trail);
}

/**
 * Escape a line of prose that begins the way a block marker does.
 *
 * Somebody who types "- not a list" in a message that also carries formatting means the
 * dash. Without this the far end would read the line as a bullet, because by then the
 * only thing telling prose from markup is the marker itself.
 *
 * Which character is escaped is not always the first: "1. text" has to become "1\. text",
 * since a backslash before a digit escapes nothing and would be shown as itself.
 *
 * The shapes here are exactly the ones that open a block, and no wider - "**bold" begins
 * with a marker character but no reader would take it for a list, and escaping it would
 * break the emphasis it actually is.
 */
QString protect_markers(const QString& line)
{
	struct Shape {
		QRegularExpression pattern;

		/** Which capture holds the character to put a backslash in front of. */
		int group;
	};

	static const Shape shapes[] = {
		/* A bullet, a quote, a heading, a fence. */
		{ QRegularExpression(QStringLiteral("^ {0,3}([-*+])(\\s|$)")), 1 },
		{ QRegularExpression(QStringLiteral("^ {0,3}(>)")), 1 },
		{ QRegularExpression(QStringLiteral("^ {0,3}(#{1,6})(\\s|$)")), 1 },
		{ QRegularExpression(QStringLiteral("^ {0,3}(```|~~~)")), 1 },

		/* A numbered item, where the delimiter is what has to be escaped. */
		{ QRegularExpression(QStringLiteral("^ {0,3}[0-9]{1,9}([.)])(\\s|$)")), 1 },

		/* A thematic break, and a line that would underline the one above it. */
		{ QRegularExpression(
			QStringLiteral("^ {0,3}([-*_])[ \\t]*(?:\\1[ \\t]*){2,}$")), 1 },
		{ QRegularExpression(QStringLiteral("^ {0,3}(=+|-+)[ \\t]*$")), 1 },

		/* A row of pipes and dashes, which would turn the line above into a table. */
		{ QRegularExpression(
			QStringLiteral("^ {0,3}([|:\\-][|:\\- \\t]*)$")), 1 }
	};

	for (const Shape& shape : shapes)
	{
		const QRegularExpressionMatch found = shape.pattern.match(line);
		if (!found.hasMatch()) continue;

		const int at = found.capturedStart(shape.group);
		return line.left(at) + QLatin1Char('\\') + line.mid(at);
	}

	return line;
}

/* ---- rendering a parsed document ---- */

using Qdc::Markdown::Kind;
using Qdc::Markdown::Node;

/** How much further in each level of quoting or nesting sits, in pixels. */
constexpr int NEST_INDENT = 18;

/**
 * The space between one block and the next, in pixels.
 *
 * Pixels because that is the unit Qt's rich text honours here: a margin given in points
 * is accepted and then ignored, which is worse than being rejected.
 */
constexpr int BLOCK_GAP = 8;

/**
 * One line of the rendered message: a div's worth of content and where it sits.
 *
 * The message is a list of these rather than one string because the hanging indent
 * belongs to the first line alone - applied to every line it would drag the top of a
 * quote or a code block out under the timestamp.
 */
struct Line {
	QString content;

	/** Indent from nesting, on top of the message's own. */
	int indent = 0;

	/** Draw the code panel behind it. */
	bool code = false;

	/**
	 * Whether the speaker's name can sit on this line.
	 *
	 * Only a line of ordinary prose at the top level: a name in front of a quote's rule,
	 * a bullet or a monospace panel reads as part of it.
	 */
	bool prose = false;

	/**
	 * Whether the content is a block-level element rather than inline markup.
	 *
	 * A rule and a table are tags Qt lays out as blocks of their own. Wrapping one in
	 * inline markup - a quote's rule down the left edge, or a list's marker - makes Qt
	 * close the surrounding block out and lose the indent with it, which puts the element
	 * hard against the left edge. So decoration is left off these lines instead.
	 */
	bool blockLevel = false;

	/**
	 * Whether this line opens a block that is set apart from the one above it.
	 *
	 * A blank line in a message means a paragraph break, and a paragraph break has to be
	 * visible as one: without this, two paragraphs and two lines of the same paragraph
	 * look identical, and the sender's blank line has been thrown away.
	 */
	bool spaced = false;
};

/**
 * A bare URL in a run of text, linked.
 *
 * Not CommonMark - the specification wants angle brackets round a bare address - but chat
 * has always linked a URL somebody simply typed, and conformance is not worth taking that
 * away for. It happens to the text of a Text node, so nothing in the parser knows.
 */
QString link_bare_urls(const QString& text, Mode mode,
                       const Qdc::RichText::RenderColours& colours)
{
	QString out;
	int i = 0;
	int plainFrom = 0;

	const auto flush = [&](int upto) {
		if (upto > plainFrom) out += emit_text(text.mid(plainFrom, upto - plainFrom), mode);
	};

	while (i < text.size())
	{
		if (scheme_length(text, i) <= 0) { ++i; continue; }

		const int end = url_end(text, i);
		const QUrl url(text.mid(i, end - i), QUrl::StrictMode);

		if (!Qdc::RichText::isLinkable(url)) { ++i; continue; }

		flush(i);

		/*
		 * A magnet names a file rather than a page, and its name is the only part of
		 * one a person can read - the rest is a hash. So it is drawn as the chip a
		 * magnet written as a link already gets, which says what the file is called
		 * and how big it is, rather than as forty characters of base32 wrapped over
		 * two lines. A bare one is never a picture: displaying media inline is
		 * something the sender asks for with the '!' only a written link can carry.
		 */
		const Qdc::RichText::Attachment file = Qdc::RichText::readMagnet(url);

		out += file.isValid()
			? chip_for(file, url, mode, colours)
			: wrap_link(emit_text(text.mid(i, end - i), mode), url, mode, colours);

		i = end;
		plainFrom = end;
	}

	flush(text.size());
	return out;
}

QString render_inlines(const QList<Node>& nodes, Mode mode,
                       const Qdc::RichText::RenderColours& colours,
                       const Qdc::RichText::RenderOptions* options);

/**
 * A link or an image, which in a chat message is usually a file.
 *
 * A magnet is the interesting case: it names a file by its hash, so it is drawn as
 * something to fetch rather than as an address. Everything else is a link if its scheme
 * is one this client will open, and its own text if it is not - see isLinkable().
 */
QString render_link(const Node& node, Mode mode,
                    const Qdc::RichText::RenderColours& colours,
                    const Qdc::RichText::RenderOptions* options)
{
	const QUrl url(node.destination, QUrl::StrictMode);
	const Qdc::RichText::Attachment file = Qdc::RichText::readMagnet(url);

	if (file.isValid())
	{
		/*
		 * Shown only when the sender asked for it and the file is actually here. Anything
		 * else is a chip: a picture nobody has fetched yet is something to click, not
		 * something to look at.
		 */
		const Qdc::RichText::LocalMedia here =
			(node.kind == Kind::Image && options && options->media)
				? options->media(file.tth)
				: Qdc::RichText::LocalMedia();

		if (mode == Mode::Html && node.kind == Kind::Image && here.isValid())
			return picture_for(file, url, here, *options);

		return chip_for(file, url, mode, colours);
	}

	/*
	 * An image that is not a magnet is never fetched - that would hand a stranger the
	 * address of everyone who read their message - so only its description is shown.
	 */
	if (node.kind == Kind::Image)
		return render_inlines(node.children, mode, colours, options);

	if (!Qdc::RichText::isLinkable(url))
	{
		/* Not a scheme we will open. Shown as what it was written as, rather than lost. */
		return emit_text(QStringLiteral("["), mode)
			+ render_inlines(node.children, mode, colours, options)
			+ emit_text(QStringLiteral("](") + node.destination + QStringLiteral(")"),
			            mode);
	}

	return wrap_link(render_inlines(node.children, mode, colours, options), url, mode,
	                 colours);
}

QString render_inlines(const QList<Node>& nodes, Mode mode,
                       const Qdc::RichText::RenderColours& colours,
                       const Qdc::RichText::RenderOptions* options)
{
	QString out;

	for (const Node& node : nodes)
	{
		switch (node.kind)
		{
			case Kind::Text:
				out += link_bare_urls(node.literal, mode, colours);
				break;

			case Kind::SoftBreak:
			case Kind::HardBreak:
				out += (mode == Mode::Plain) ? QStringLiteral("\n")
				                             : QStringLiteral("<br>");
				break;

			case Kind::Emphasis:
				out += (mode == Mode::Plain)
					? render_inlines(node.children, mode, colours, options)
					: QStringLiteral("<i>%1</i>")
						.arg(render_inlines(node.children, mode, colours, options));
				break;

			case Kind::Strong:
				out += (mode == Mode::Plain)
					? render_inlines(node.children, mode, colours, options)
					: QStringLiteral("<b>%1</b>")
						.arg(render_inlines(node.children, mode, colours, options));
				break;

			case Kind::Strike:
				out += (mode == Mode::Plain)
					? render_inlines(node.children, mode, colours, options)
					: QStringLiteral("<s>%1</s>")
						.arg(render_inlines(node.children, mode, colours, options));
				break;

			case Kind::Code:
				out += (mode == Mode::Plain)
					? node.literal
					: QStringLiteral("<span style=\"font-family:'%1';"
					                 " background-color:%2; color:%3\">%4</span>")
						.arg(Qdc::RichText::monospaceFamily(),
						     colours.codeBack.name(), colours.code.name(),
						     emit_text(node.literal, mode));
				break;

			case Kind::Link:
			case Kind::Image:
				out += render_link(node, mode, colours, options);
				break;

			default:
				out += render_inlines(node.children, mode, colours, options);
				break;
		}
	}

	return out;
}

/** A code block's body, with its spacing kept. */
QString render_code_block(const QString& literal, Mode mode)
{
	QString body = literal;
	while (body.endsWith(QLatin1Char('\n'))) body.chop(1);

	if (mode == Mode::Plain) return body;

	/*
	 * Spaces become non-breaking so that indentation inside the block survives. Relying
	 * on a white-space property would leave the layout at the mercy of what this Qt
	 * build's CSS subset happens to honour.
	 */
	QString escaped = body.toHtmlEscaped();
	escaped.replace(QLatin1Char(' '), QStringLiteral("&nbsp;"));
	escaped.replace(QLatin1Char('\t'), QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;"));
	escaped.replace(QLatin1Char('\n'), QStringLiteral("<br>"));

	return escaped;
}

/**
 * A heading, set bold and a step larger.
 *
 * Deliberately not <h1> to <h6>, though Qt scales those itself: they are block elements,
 * and a block nested inside the inline markup of a quote makes Qt close the block out and
 * lose the margin the line was indented by - which puts a heading inside a quote hard
 * against the left edge. A span cannot do that.
 *
 * The steps are small. A chat line is not a document, and a heading has to read as one
 * without turning a message into a poster.
 */
QString render_heading(int level, const QString& body, qreal pointSize)
{
	static const qreal scale[] = { 1.45, 1.3, 1.18, 1.08, 1.0, 1.0 };

	const int step = qBound(1, level, 6) - 1;

	if (pointSize <= 0.0 || scale[step] == 1.0)
		return QStringLiteral("<b>%1</b>").arg(body);

	return QStringLiteral("<b><span style=\"font-size:%1pt\">%2</span></b>")
		.arg(pointSize * scale[step], 0, 'f', 1).arg(body);
}

QString render_table(const Node& table, Mode mode,
                     const Qdc::RichText::RenderColours& colours,
                     const Qdc::RichText::RenderOptions* options)
{
	if (mode == Mode::Plain)
	{
		QStringList rows;

		for (const Node& row : table.children)
		{
			QStringList cells;
			for (const Node& cell : row.children)
				cells.append(render_inlines(cell.children, mode, colours, options));

			rows.append(cells.join(QStringLiteral(" | ")));
		}

		return rows.join(QLatin1Char('\n'));
	}

	QString out = QStringLiteral(
		"<table border=\"1\" cellspacing=\"0\" cellpadding=\"4\""
		" style=\"border-color:%1\">").arg(colours.quoteBar.name());

	for (const Node& row : table.children)
	{
		out += QStringLiteral("<tr>");

		for (const Node& cell : row.children)
		{
			QString align;

			switch (cell.align)
			{
				case Qdc::Markdown::Align::Left: align = QStringLiteral("left"); break;
				case Qdc::Markdown::Align::Centre: align = QStringLiteral("center"); break;
				case Qdc::Markdown::Align::Right: align = QStringLiteral("right"); break;
				case Qdc::Markdown::Align::None: break;
			}

			const QString body = render_inlines(cell.children, mode, colours, options);

			out += QStringLiteral("<td align=\"%1\">%2</td>")
				.arg(align, cell.header ? QStringLiteral("<b>%1</b>").arg(body) : body);
		}

		out += QStringLiteral("</tr>");
	}

	return out + QStringLiteral("</table>");
}

/**
 * Render a container's blocks.
 *
 * 'spaceBlocks' says whether siblings here are set apart with vertical space. They are
 * everywhere except inside a tight list, which is what tightness means: its items were
 * written without blank lines between them and are meant to read as one run.
 */
void render_blocks(const QList<Node>& nodes, Mode mode, int depth, bool spaceBlocks,
                   const Qdc::RichText::RenderColours& colours,
                   const Qdc::RichText::RenderOptions* options, QList<Line>* out);

/** One list, its markers drawn as part of each item's first line. */
void render_list(const Node& list, Mode mode, int depth,
                 const Qdc::RichText::RenderColours& colours,
                 const Qdc::RichText::RenderOptions* options, QList<Line>* out)
{
	int ordinal = list.level > 0 ? list.level : 1;

	for (const Node& item : list.children)
	{
		QList<Line> inner;
		render_blocks(item.children, mode, depth + 1, !list.tight, colours, options,
		              &inner);

		/*
		 * The marker is drawn as part of the line rather than with a QTextList, because
		 * the whole message is one hanging indent and a list Qt owns would indent against
		 * the block instead of the text column.
		 */
		const QString marker = list.ordered
			? QStringLiteral("%1.").arg(ordinal)
			: (mode == Mode::Plain ? QStringLiteral("\u2022")
			                       : QStringLiteral("&#8226;"));

		if (inner.isEmpty()) inner.append(Line());

		/*
		 * The marker cannot go in front of a rule or a table for the same reason a
		 * quote's own rule cannot, so it takes a line to itself instead.
		 */
		if (inner.first().blockLevel)
		{
			Line marked;
			marked.indent = inner.first().indent;
			marked.spaced = inner.first().spaced;
			inner.first().spaced = false;
			inner.prepend(marked);
		}

		if (mode == Mode::Plain)
		{
			inner.first().content = marker + QLatin1Char(' ') + inner.first().content;
		}
		else
		{
			inner.first().content =
				marker + QStringLiteral("&nbsp;") + inner.first().content;
		}

		/* A loose list is one whose items were separated by blank lines; keep them. */
		if (!inner.isEmpty()) inner.first().spaced = !list.tight && ordinal > list.level;

		for (Line& line : inner)
		{
			line.prose = false;
			out->append(line);
		}

		++ordinal;
	}
}

/** One quote, with the rule down its left edge on every line of it. */
void render_quote(const Node& quote, Mode mode, int depth,
                  const Qdc::RichText::RenderColours& colours,
                  const Qdc::RichText::RenderOptions* options, QList<Line>* out)
{
	QList<Line> inner;
	render_blocks(quote.children, mode, depth + 1, true, colours, options, &inner);

	for (Line& line : inner)
	{
		if (mode == Mode::Html && !line.blockLevel)
		{
			/*
			 * The rule goes down the whole quote, so every line carries it - a bar on the
			 * opening line alone reads as a stray character.
			 */
			QStringList marked;

			for (const QString& part : line.content.split(QStringLiteral("<br>")))
			{
				marked.append(QStringLiteral(
					"<span style=\"color:%1\">&#9615;</span> "
					"<span style=\"color:%2\">%3</span>")
					.arg(colours.quoteBar.name(), colours.quote.name(), part));
			}

			line.content = marked.join(QStringLiteral("<br>"));
		}

		line.prose = false;
		out->append(line);
	}
}

void render_blocks(const QList<Node>& nodes, Mode mode, int depth, bool spaceBlocks,
                   const Qdc::RichText::RenderColours& colours,
                   const Qdc::RichText::RenderOptions* options, QList<Line>* out)
{
	for (const Node& node : nodes)
	{
		/* Where this block's lines begin, so the first of them can be set apart. */
		const int from = out->size();

		switch (node.kind)
		{
			case Kind::Paragraph:
			{
				Line line;
				line.indent = depth * NEST_INDENT;
				line.prose = depth == 0;
				line.content = render_inlines(node.children, mode, colours, options);
				out->append(line);
				break;
			}

			case Kind::Heading:
			{
				Line line;
				line.indent = depth * NEST_INDENT;
				const QString body = render_inlines(node.children, mode, colours, options);

				line.content = (mode == Mode::Plain)
					? body
					: render_heading(node.level, body,
					                 options ? options->fontPointSize : 0.0);

				out->append(line);
				break;
			}

			case Kind::ThematicBreak:
			{
				Line line;
				line.indent = depth * NEST_INDENT;

				/*
				 * A rule rather than "---": the markers were the sender's way of asking
				 * for a divider, and this is the divider.
				 */
				line.blockLevel = mode == Mode::Html;
				line.content = (mode == Mode::Plain)
					? QStringLiteral("───")
					: QStringLiteral("<hr>");

				out->append(line);
				break;
			}

			case Kind::CodeBlock:
			{
				Line line;
				line.indent = depth * NEST_INDENT;
				line.code = mode == Mode::Html;
				line.content = render_code_block(node.literal, mode);

				if (mode == Mode::Html)
				{
					line.content = QStringLiteral(
						"<span style=\"font-family:'%1'; color:%2\">%3</span>")
						.arg(Qdc::RichText::monospaceFamily(), colours.code.name(),
						     line.content);
				}

				out->append(line);
				break;
			}

			case Kind::BlockQuote:
				render_quote(node, mode, depth, colours, options, out);
				break;

			case Kind::List:
				render_list(node, mode, depth, colours, options, out);
				break;

			case Kind::Table:
			{
				Line line;
				line.indent = depth * NEST_INDENT;
				line.blockLevel = mode == Mode::Html;
				line.content = render_table(node, mode, colours, options);
				out->append(line);
				break;
			}

			default:
			{
				Line line;
				line.indent = depth * NEST_INDENT;
				line.content = render_inlines(node.children, mode, colours, options);
				out->append(line);
				break;
			}
		}

		/*
		 * Anything but the first block in a container is set apart from what precedes it.
		 * For a list that means the space in front of the list: the space between its own
		 * items is its own business, and render_list() has already decided it.
		 */
		if (spaceBlocks && from > 0 && from < out->size()) (*out)[from].spaced = true;
	}
}

}

QString Qdc::RichText::monospaceFamily()
{
	static const QString family =
		QFontDatabase::systemFont(QFontDatabase::FixedFont).family();

	return family;
}

bool Qdc::RichText::isLinkable(const QUrl& url)
{
	if (!url.isValid()) return false;

	const QString scheme = url.scheme().toLower();

	/*
	 * A magnet names a file by its hash and has no host to check, so it is judged by
	 * whether the hash is there and of the right shape. One without a usable hash is
	 * not a link at all: there would be nothing to ask anybody for.
	 */
	if (scheme == QStringLiteral("magnet")) return readMagnet(url).isValid();

	if (url.host().isEmpty()) return false;

	return external_schemes().contains(scheme) || hub_schemes().contains(scheme);
}

Qdc::RichText::Attachment Qdc::RichText::readMagnet(const QUrl& url)
{
	Attachment file;

	if (url.scheme().toLower() != QStringLiteral("magnet")) return file;

	/*
	 * The query is read with the delimiters magnet uses. QUrl::query() would otherwise
	 * hand back the whole thing for us to split by hand.
	 */
	const QUrlQuery query(url.query(QUrl::FullyDecoded));

	for (const auto& [key, value] : query.queryItems(QUrl::FullyDecoded))
	{
		if (key.compare(QStringLiteral("xt"), Qt::CaseInsensitive) == 0)
		{
			static const QString prefix = QStringLiteral("urn:tree:tiger:");

			if (value.startsWith(prefix, Qt::CaseInsensitive))
				file.tth = value.mid(prefix.size()).trimmed().toUpper();
		}
		else if (key.compare(QStringLiteral("dn"), Qt::CaseInsensitive) == 0)
		{
			/*
			 * Taken as a name and never as a path: a sender does not get to say where
			 * anything lands, and "../" in a display name is somebody trying to.
			 */
			file.name = QFileInfo(value).fileName().trimmed();
		}
		else if (key.compare(QStringLiteral("xl"), Qt::CaseInsensitive) == 0)
		{
			bool number = false;
			const quint64 size = value.toULongLong(&number);
			if (number) file.size = size;
		}
	}

	/*
	 * A tiger tree root is 39 base32 characters. Checked rather than assumed, because
	 * this value is about to be handed to a search and a transfer.
	 */
	static const QRegularExpression base32(QStringLiteral("^[A-Z2-7]{39}$"));
	if (!base32.match(file.tth).hasMatch()) file.tth.clear();

	if (file.name.isEmpty()) file.name = file.tth;

	return file;
}

namespace {

void collect_attachments(const Node& node, QList<Qdc::RichText::Attachment>* found)
{
	if (node.kind == Kind::Link || node.kind == Kind::Image)
	{
		Qdc::RichText::Attachment file =
			Qdc::RichText::readMagnet(QUrl(node.destination, QUrl::StrictMode));

		if (file.isValid())
		{
			file.inlineMedia = node.kind == Kind::Image;
			found->append(file);
		}
	}

	for (const Node& child : node.children) collect_attachments(child, found);
}

}

QList<Qdc::RichText::Attachment> Qdc::RichText::attachmentsIn(const QString& markdown)
{
	/*
	 * Walked rather than searched for: the same tree the renderer draws from, so what is
	 * decided about a message and what is drawn from it cannot disagree. A magnet inside
	 * a code span is not a link, and a pattern over the text would think it was.
	 */
	QList<Attachment> found;
	collect_attachments(Markdown::parse(markdown), &found);

	return found;
}

QString Qdc::RichText::writeMagnet(const Attachment& file)
{
	if (!file.isValid()) return QString();

	QUrlQuery query;
	query.addQueryItem(QStringLiteral("xt"),
	                   QStringLiteral("urn:tree:tiger:") + file.tth);

	if (!file.name.isEmpty()) query.addQueryItem(QStringLiteral("dn"), file.name);
	if (file.size > 0)
		query.addQueryItem(QStringLiteral("xl"), QString::number(file.size));

	QUrl url;
	url.setScheme(QStringLiteral("magnet"));
	url.setQuery(query);

	return url.toString(QUrl::FullyEncoded);
}

QString Qdc::RichText::describeSize(quint64 bytes)
{
	static const QStringList units = {
		QStringLiteral("bytes"), QStringLiteral("KiB"), QStringLiteral("MiB"),
		QStringLiteral("GiB"), QStringLiteral("TiB")
	};

	double size = static_cast<double>(bytes);
	int unit = 0;

	while (size >= 1024.0 && unit + 1 < units.size())
	{
		size /= 1024.0;
		++unit;
	}

	/* Whole bytes, and one decimal above that: "700 bytes", "1.0 MiB". */
	return unit == 0
		? QStringLiteral("%1 %2").arg(bytes).arg(units.at(unit))
		: QStringLiteral("%1 %2").arg(size, 0, 'f', 1).arg(units.at(unit));
}

bool Qdc::RichText::isHubUrl(const QUrl& url)
{
	if (!url.isValid()) return false;
	return hub_schemes().contains(url.scheme().toLower());
}

bool Qdc::RichText::hasMarkup(const QTextDocument& document)
{
	for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
	{
		if (kind_of(block) != Paragraph) return true;

		for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
		{
			const QTextFragment fragment = it.fragment();
			if (!fragment.isValid()) continue;

			const QTextCharFormat format = fragment.charFormat();

			if (is_bold(format) || format.fontItalic() || is_code(format)
			    || format.isAnchor())
			{
				return true;
			}
		}
	}

	return false;
}

QString Qdc::RichText::toMarkdown(const QTextDocument& document)
{
	QStringList out;

	BlockKind previous = Paragraph;
	int ordinal = 0;

	for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
	{
		const BlockKind kind = kind_of(block);

		QString line;

		for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
		{
			const QTextFragment fragment = it.fragment();
			if (!fragment.isValid()) continue;

			const QTextCharFormat format = fragment.charFormat();
			const QString text = normalise_breaks(fragment.text());

			if (text.isEmpty()) continue;

			/*
			 * A code block is literal throughout, so nothing inside it is marked up
			 * and nothing is escaped - the fence is what protects it.
			 */
			if (kind == CodeBlock)
			{
				line += text;
				continue;
			}

			QString run = is_code(format) ? code_span(text)
			                              : escape_markdown(text);

			if (!is_code(format))
			{
				if (format.fontItalic()) run = wrap_emphasis(run, QStringLiteral("*"));
				if (is_bold(format)) run = wrap_emphasis(run, QStringLiteral("**"));
			}

			/*
			 * Only a scheme the far end would be willing to open is worth writing as a
			 * link; anything else is left as its own text rather than sent as a target
			 * nobody can follow.
			 */
			if (format.isAnchor()
			    && Qdc::RichText::isLinkable(QUrl(format.anchorHref(), QUrl::StrictMode)))
			{
				/* '!' asks for it to be shown rather than offered; see MediaProperty. */
				run = format.property(MediaProperty).toBool()
					? QStringLiteral("![%1](%2)").arg(run, format.anchorHref())
					: QStringLiteral("[%1](%2)").arg(run, format.anchorHref());
			}

			line += run;
		}

		switch (kind)
		{
			case Quote:
				/* Every line of a quote carries the marker, including a blank one. */
				for (const QString& part : line.split(QLatin1Char('\n')))
					out.append(QStringLiteral("> ") + part);
				break;

			case Bullet:
				/*
				 * A marker per line: a block may hold soft breaks, from a paste or from
				 * an editor that puts two lines in one item, and each of those lines is
				 * an item of its own once it is written down.
				 */
				for (const QString& part : line.split(QLatin1Char('\n')))
					out.append(QStringLiteral("- ") + part);
				break;

			case Number:
			{
				/*
				 * Numbered from where this run of items started rather than from the
				 * block's position in the document, so a second list further down
				 * starts at one again.
				 */
				if (previous != Number) ordinal = 0;

				for (const QString& part : line.split(QLatin1Char('\n')))
					out.append(QStringLiteral("%1. ").arg(++ordinal) + part);
				break;
			}

			case CodeBlock:
			{
				const QString fence(qMax(3, longest_backtick_run(line) + 1),
				                    QLatin1Char('`'));
				out.append(fence);
				out.append(line.split(QLatin1Char('\n')));
				out.append(fence);
				break;
			}

			case Paragraph:
			default:
				out.append(protect_markers(line));
				break;
		}

		previous = kind;
	}

	/*
	 * Trailing blank lines carry no meaning and only make the wire payload longer, so
	 * they go. Interior ones are kept - somebody may have meant them.
	 */
	while (!out.isEmpty() && out.last().trimmed().isEmpty()) out.removeLast();

	return out.join(QLatin1Char('\n'));
}

QString Qdc::RichText::toPlainText(const QString& markdown)
{
	QList<Line> lines;

	/*
	 * A list keeps its markers here, where a quote does not: without them the items run
	 * together as unrelated lines, and this is what a screen reader is given.
	 */
	render_blocks(Markdown::parse(markdown).children, Mode::Plain, 0, true,
	              RenderColours(), nullptr, &lines);

	QStringList out;
	for (const Line& line : lines) out.append(line.content);

	return out.join(QLatin1Char('\n'));
}

QString Qdc::RichText::renderMarkdown(const QString& markdown,
                                      const RenderOptions& options)
{
	QList<Line> lines;
	render_blocks(Markdown::parse(markdown).children, Mode::Html, 0, true,
	              options.colours, &options, &lines);

	/*
	 * The speaker belongs on the first line of prose. When a message opens with a code
	 * block or a quote there is no such line, so the prefix gets one of its own rather
	 * than being dragged into a monospace panel.
	 */
	if (lines.isEmpty() || !lines.first().prose) lines.prepend(Line());

	QString html;

	for (int n = 0; n < lines.size(); ++n)
	{
		const Line& line = lines.at(n);

		QString style = QStringLiteral("margin-left:%1px;")
			.arg(options.indent + line.indent);

		if (line.spaced) style += QStringLiteral(" margin-top:%1px;").arg(BLOCK_GAP);

		/*
		 * Only the first line pulls its opening words back out to the left. Applied to
		 * every one, a first-line indent would drag the top of a quote or a code block
		 * out under the timestamp.
		 */
		if (n == 0)
			style += QStringLiteral(" text-indent:-%1px;").arg(options.indent);

		if (line.code)
		{
			style += QStringLiteral(" background-color:%1;")
				.arg(options.colours.codeBack.name());
		}

		QString content = style_body(line.content, options);
		if (n == 0) content = options.prefix + content;

		html += QStringLiteral("<div style=\"%1\">%2</div>").arg(style, content);
	}

	return html;
}

QString Qdc::RichText::renderPlain(const QString& text, const RenderOptions& options)
{
	/*
	 * An address somebody simply typed is linked here as it is in a formatted message.
	 * Nothing else about the text is interpreted - an asterisk stays an asterisk - but
	 * an address is not markup, it is the one thing in a line of chat that is worth
	 * clicking, and most hubs cannot carry the flag that would make a message formatted
	 * in the first place. The escaping is link_bare_urls()' own, run over each run of
	 * text between the addresses, so a stranger's angle brackets are as harmless here
	 * as they were before.
	 */
	const QString body = link_bare_urls(normalise_breaks(text), Mode::Html,
	                                    options.colours);

	return QStringLiteral("<div style=\"margin-left:%1px; text-indent:-%1px;\">%2%3</div>")
		.arg(options.indent)
		.arg(options.prefix, style_body(body, options));
}

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "richtext.h"

#include <QFontDatabase>
#include <QStringList>
#include <QTextBlock>
#include <QTextDocument>
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
 * The characters our own inline scanner gives meaning to.
 *
 * Underscore is deliberately absent: emphasis is only ever asterisks here, so
 * snake_case survives a formatted message untouched instead of arriving full of
 * backslashes. Every escape we add is noise for somebody reading the raw markers.
 */
bool is_significant(QChar c)
{
	return c == QLatin1Char('\\') || c == QLatin1Char('*')
		|| c == QLatin1Char('`') || c == QLatin1Char('[')
		|| c == QLatin1Char(']');
}

/** Escape a plain run so that it survives being parsed back. */
QString escape_markdown(const QString& text)
{
	QString escaped;
	escaped.reserve(text.size());

	for (const QChar c : text)
	{
		if (is_significant(c)) escaped += QLatin1Char('\\');
		escaped += c;
	}

	return escaped;
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

	if (!kind.isValid()) return Qdc::RichText::Paragraph;

	return static_cast<Qdc::RichText::BlockKind>(kind.toInt());
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

	static const QString trailing = QStringLiteral(".,;:!?'\")]}");
	while (end > from && trailing.contains(text.at(end - 1))) --end;

	return end;
}

/** A scheme followed by "://" starting at 'at', or -1. */
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
	if (!QStringView(text).mid(n).startsWith(QStringLiteral("://"))) return -1;

	return n - at;
}

QString scan_inline(const QString& text, Mode mode,
                    const Qdc::RichText::RenderColours& colours);

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
QString scan_inline(const QString& text, Mode mode,
                    const Qdc::RichText::RenderColours& colours)
{
	QString out;
	QString pending;

	const auto flush = [&] {
		if (pending.isEmpty()) return;
		out += emit_text(pending, mode);
		pending.clear();
	};

	int i = 0;
	const int n = text.size();

	while (i < n)
	{
		const QChar c = text.at(i);

		/* An escape always yields the next character literally. */
		if (c == QLatin1Char('\\') && i + 1 < n)
		{
			pending += text.at(i + 1);
			i += 2;
			continue;
		}

		if (c == QLatin1Char('`'))
		{
			int fence = 0;
			while (i + fence < n && text.at(i + fence) == QLatin1Char('`')) ++fence;

			const QString close(fence, QLatin1Char('`'));
			const int end = text.indexOf(close, i + fence);

			if (end > 0)
			{
				QString body = text.mid(i + fence, end - i - fence);

				/* Undo the padding code_span() may have added. */
				if (body.startsWith(QLatin1Char(' ')) && body.endsWith(QLatin1Char(' '))
				    && body.trimmed().size() != body.size() - 1)
				{
					body = body.mid(1, body.size() - 2);
				}

				flush();

				out += (mode == Mode::Plain)
					? body
					: QStringLiteral("<span style=\"font-family:'%1';"
					                 " background-color:%2; color:%3\">%4</span>")
						.arg(Qdc::RichText::monospaceFamily(),
						     colours.codeBack.name(), colours.code.name(),
						     emit_text(body, mode));

				i = end + fence;
				continue;
			}
		}

		if (c == QLatin1Char('['))
		{
			const int close = text.indexOf(QLatin1Char(']'), i + 1);

			if (close > 0 && close + 1 < n
			    && text.at(close + 1) == QLatin1Char('('))
			{
				const int stop = text.indexOf(QLatin1Char(')'), close + 2);

				if (stop > 0)
				{
					const QString label = text.mid(i + 1, close - i - 1);
					const QUrl url(text.mid(close + 2, stop - close - 2).trimmed(),
					               QUrl::StrictMode);

					if (Qdc::RichText::isLinkable(url))
					{
						flush();
						out += wrap_link(scan_inline(label, mode, colours), url, mode,
						                 colours);
						i = stop + 1;
						continue;
					}

					/*
					 * Not a scheme we will open. Shown as the text it is rather than
					 * dropped, so nothing silently disappears from a message.
					 */
					flush();
					out += emit_text(text.mid(i, stop + 1 - i), mode);
					i = stop + 1;
					continue;
				}
			}
		}

		if (c == QLatin1Char('*'))
		{
			const bool strong = (i + 1 < n && text.at(i + 1) == QLatin1Char('*'));
			const QString marker = strong ? QStringLiteral("**")
			                              : QStringLiteral("*");
			const int end = text.indexOf(marker, i + marker.size());

			if (end > 0 && end > i + marker.size())
			{
				const QString body =
					text.mid(i + marker.size(), end - i - marker.size());

				flush();

				const QString inner = scan_inline(body, mode, colours);
				out += (mode == Mode::Plain)
					? inner
					: (strong ? QStringLiteral("<b>%1</b>").arg(inner)
					          : QStringLiteral("<i>%1</i>").arg(inner));

				i = end + marker.size();
				continue;
			}
		}

		const int scheme = scheme_length(text, i);
		if (scheme > 0)
		{
			const int end = url_end(text, i);
			const QUrl url(text.mid(i, end - i), QUrl::StrictMode);

			if (Qdc::RichText::isLinkable(url))
			{
				flush();
				out += wrap_link(emit_text(text.mid(i, end - i), mode), url, mode,
				                 colours);
				i = end;
				continue;
			}
		}

		pending += c;
		++i;
	}

	flush();
	return out;
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

/** One rendered block, and what sort it was. */
struct Block {
	Qdc::RichText::BlockKind kind = Qdc::RichText::Paragraph;
	QString content;
};

/**
 * Split markdown into blocks, rendering each one's inline content.
 *
 * Consecutive plain lines join into a single paragraph separated by breaks rather than
 * becoming a block each. Fewer blocks keeps the hanging indent simple, and a chat line
 * has no need of semantic paragraphs.
 */
QList<Block> split_blocks(const QString& markdown, Mode mode,
                          const Qdc::RichText::RenderColours& colours)
{
	QList<Block> blocks;

	const QStringList lines = normalise_breaks(markdown).split(QLatin1Char('\n'));

	QStringList paragraph;
	QStringList quote;

	const auto flushParagraph = [&] {
		if (paragraph.isEmpty()) return;

		Block block;
		block.kind = Qdc::RichText::Paragraph;
		block.content = paragraph.join(mode == Mode::Plain ? QStringLiteral("\n")
		                                                   : QStringLiteral("<br>"));
		blocks.append(block);
		paragraph.clear();
	};

	const auto flushQuote = [&] {
		if (quote.isEmpty()) return;

		Block block;
		block.kind = Qdc::RichText::Quote;
		block.content = quote.join(mode == Mode::Plain ? QStringLiteral("\n")
		                                               : QStringLiteral("<br>"));
		blocks.append(block);
		quote.clear();
	};

	for (int n = 0; n < lines.size(); ++n)
	{
		const QString& line = lines.at(n);
		const QString trimmed = line.trimmed();

		/* A fence of three or more backticks opens a literal block. */
		if (trimmed.startsWith(QStringLiteral("```")))
		{
			flushParagraph();
			flushQuote();

			int fence = 0;
			while (fence < trimmed.size() && trimmed.at(fence) == QLatin1Char('`'))
				++fence;

			QStringList body;
			++n;

			for (; n < lines.size(); ++n)
			{
				const QString candidate = lines.at(n).trimmed();
				int closing = 0;
				while (closing < candidate.size()
				       && candidate.at(closing) == QLatin1Char('`'))
					++closing;

				if (closing >= fence && closing == candidate.size()) break;

				body.append(lines.at(n));
			}

			Block block;
			block.kind = Qdc::RichText::CodeBlock;

			if (mode == Mode::Plain)
			{
				block.content = body.join(QLatin1Char('\n'));
			}
			else
			{
				/*
				 * Spaces become non-breaking so that indentation inside the block
				 * survives. Relying on a white-space property would leave the layout
				 * at the mercy of what this Qt build's CSS subset happens to honour.
				 */
				QString escaped = body.join(QLatin1Char('\n')).toHtmlEscaped();
				escaped.replace(QLatin1Char(' '), QStringLiteral("&nbsp;"));
				escaped.replace(QLatin1Char('\t'), QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;"));
				escaped.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
				block.content = escaped;
			}

			blocks.append(block);
			continue;
		}

		if (trimmed.startsWith(QLatin1Char('>')))
		{
			flushParagraph();

			QString body = trimmed.mid(1);
			if (body.startsWith(QLatin1Char(' '))) body = body.mid(1);

			quote.append(scan_inline(body, mode, colours));
			continue;
		}

		flushQuote();
		paragraph.append(scan_inline(line, mode, colours));
	}

	flushParagraph();
	flushQuote();

	return blocks;
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
	if (!url.isValid() || url.host().isEmpty()) return false;

	const QString scheme = url.scheme().toLower();
	return external_schemes().contains(scheme) || hub_schemes().contains(scheme);
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
				if (format.fontItalic()) run = QStringLiteral("*%1*").arg(run);
				if (is_bold(format)) run = QStringLiteral("**%1**").arg(run);
			}

			/*
			 * Only a scheme the far end would be willing to open is worth writing as a
			 * link; anything else is left as its own text rather than sent as a target
			 * nobody can follow.
			 */
			if (format.isAnchor()
			    && Qdc::RichText::isLinkable(QUrl(format.anchorHref(), QUrl::StrictMode)))
			{
				run = QStringLiteral("[%1](%2)").arg(run, format.anchorHref());
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
				out.append(line);
				break;
		}
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
	QStringList out;

	for (const Block& block : split_blocks(markdown, Mode::Plain, RenderColours()))
		out.append(block.content);

	return out.join(QLatin1Char('\n'));
}

QString Qdc::RichText::renderMarkdown(const QString& markdown,
                                      const RenderOptions& options)
{
	QList<Block> blocks = split_blocks(markdown, Mode::Html, options.colours);

	/*
	 * The speaker belongs on the first line of prose. When a message opens with a code
	 * block or a quote there is no such line, so the prefix gets one of its own rather
	 * than being dragged into a monospace panel.
	 */
	if (blocks.isEmpty() || blocks.first().kind != Paragraph)
	{
		Block lead;
		lead.kind = Paragraph;
		blocks.prepend(lead);
	}

	QString html;

	for (int n = 0; n < blocks.size(); ++n)
	{
		const Block& block = blocks.at(n);

		/*
		 * Only the first block pulls its opening line back out to the left. Applied to
		 * every block, a first-line indent would drag the top of a quote or a code
		 * block out under the timestamp.
		 */
		QString style = QStringLiteral("margin-left:%1px;").arg(options.indent);
		if (n == 0)
			style += QStringLiteral(" text-indent:-%1px;").arg(options.indent);

		QString content = block.content;

		switch (block.kind)
		{
			case Quote:
			{
				/*
				 * The rule goes down the whole quote, so every line carries it - a
				 * bar on the opening line alone reads as a stray character.
				 */
				QStringList marked;
				for (const QString& line : content.split(QStringLiteral("<br>")))
				{
					marked.append(QStringLiteral(
						"<span style=\"color:%1\">&#9615;</span> "
						"<span style=\"color:%2\">%3</span>")
						.arg(options.colours.quoteBar.name(),
						     options.colours.quote.name(), line));
				}

				content = marked.join(QStringLiteral("<br>"));
				break;
			}

			case CodeBlock:
				style += QStringLiteral(" background-color:%1;")
					.arg(options.colours.codeBack.name());
				content = QStringLiteral(
					"<span style=\"font-family:'%1'; color:%2\">%3</span>")
					.arg(monospaceFamily(), options.colours.code.name(), content);
				break;

			case Paragraph:
			default:
				break;
		}

		content = style_body(content, options);
		if (n == 0) content = options.prefix + content;

		html += QStringLiteral("<div style=\"%1\">%2</div>").arg(style, content);
	}

	return html;
}

QString Qdc::RichText::renderPlain(const QString& text, const RenderOptions& options)
{
	QString escaped = normalise_breaks(text).toHtmlEscaped();
	escaped.replace(QLatin1Char('\n'), QStringLiteral("<br>"));

	return QStringLiteral("<div style=\"margin-left:%1px; text-indent:-%1px;\">%2%3</div>")
		.arg(options.indent)
		.arg(options.prefix, style_body(escaped, options));
}

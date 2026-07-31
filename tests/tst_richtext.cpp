/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <gui/richtext.h>

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTest>

/**
 * The markup layer, on its own.
 *
 * Worth testing hard and worth testing here rather than through a widget: every chat
 * line a stranger sends passes through renderMarkdown(), and the composer's whole
 * contract is that what it serializes is what the other end renders.
 */
class TestRichText : public QObject {
	Q_OBJECT

private slots:
	void plainTextCarriesNoMarkup();
	void formattingIsDetectedAsMarkup();
	void boldItalicAndCodeRoundTrip();
	void boldAndItalicTogetherRoundTrip();
	void quoteAndCodeBlockRoundTrip();
	void listsRoundTripAndKeepTheirNumbers();
	void proseThatOpensLikeAMarkerStaysProse();
	void literalMarkersSurviveAFormattedMessage();
	void codeSpansContainingBackticksSurvive();
	void linksRoundTripAndAreScopedToKnownSchemes();
	void bareUrlsAreLinkedWithoutTrailingPunctuation();
	void remoteTextCannotInjectMarkup();
	void plainRenderingKeepsLineBreaksAndMarkers();
	void toPlainTextStripsMarkers();
	void theHangingIndentOnlyPullsBackTheFirstBlock();

private:
	static Qdc::RichText::RenderOptions options();

	/** A document holding one run with 'format' applied. */
	static void appendRun(QTextDocument& document, const QString& text,
	                      const QTextCharFormat& format);

	/**
	 * The names of the live tags in 'html'.
	 *
	 * Checking for substrings cannot tell a tag from text that merely looks like one:
	 * an escaped "&lt;script&gt;" is exactly the correct output and still contains
	 * "script". What matters is which tags are real, so that is what is extracted.
	 */
	static QStringList tagNames(const QString& html);

	/** The href of every live anchor in 'html'. */
	static QStringList anchorTargets(const QString& html);
};

QStringList TestRichText::tagNames(const QString& html)
{
	static const QRegularExpression tag(QStringLiteral("<\\s*/?\\s*([A-Za-z0-9]+)"));

	QStringList names;
	QRegularExpressionMatchIterator it = tag.globalMatch(html);

	while (it.hasNext()) names.append(it.next().captured(1).toLower());

	return names;
}

QStringList TestRichText::anchorTargets(const QString& html)
{
	static const QRegularExpression href(
		QStringLiteral("<a[^>]*href=\"([^\"]*)\""));

	QStringList targets;
	QRegularExpressionMatchIterator it = href.globalMatch(html);

	while (it.hasNext()) targets.append(it.next().captured(1));

	return targets;
}

Qdc::RichText::RenderOptions TestRichText::options()
{
	Qdc::RichText::RenderOptions o;
	o.indent = 40;
	o.colours.code = QColor(0x30, 0x30, 0x30);
	o.colours.codeBack = QColor(0xf0, 0xf0, 0xf0);
	o.colours.quote = QColor(0x60, 0x60, 0x60);
	o.colours.quoteBar = QColor(0x90, 0x90, 0x90);
	o.colours.link = QColor(0x20, 0x50, 0xc0);
	return o;
}

void TestRichText::appendRun(QTextDocument& document, const QString& text,
                             const QTextCharFormat& format)
{
	QTextCursor cursor(&document);
	cursor.movePosition(QTextCursor::End);
	cursor.insertText(text, format);
}

void TestRichText::plainTextCarriesNoMarkup()
{
	QTextDocument document;
	document.setPlainText(QStringLiteral("just words"));

	/* No formatting means no RT1, which means the text goes out untouched. */
	QVERIFY(!Qdc::RichText::hasMarkup(document));

	/*
	 * The case that motivates the flag: somebody typing asterisks meant them. With no
	 * markup the composer sends the plain text, so nothing escapes or emphasises it.
	 */
	QTextDocument literal;
	literal.setPlainText(QStringLiteral("weighs *about* 3kg"));
	QVERIFY(!Qdc::RichText::hasMarkup(literal));
}

void TestRichText::formattingIsDetectedAsMarkup()
{
	QTextCharFormat bold;
	bold.setFontWeight(QFont::Bold);

	QTextDocument document;
	appendRun(document, QStringLiteral("look"), bold);
	QVERIFY(Qdc::RichText::hasMarkup(document));

	QTextCharFormat italic;
	italic.setFontItalic(true);

	QTextDocument second;
	appendRun(second, QStringLiteral("look"), italic);
	QVERIFY(Qdc::RichText::hasMarkup(second));

	QTextCharFormat code;
	code.setProperty(Qdc::RichText::CodeProperty, true);

	QTextDocument third;
	appendRun(third, QStringLiteral("ls -l"), code);
	QVERIFY(Qdc::RichText::hasMarkup(third));

	QTextCharFormat anchor;
	anchor.setAnchor(true);
	anchor.setAnchorHref(QStringLiteral("https://example.org/"));

	QTextDocument fourth;
	appendRun(fourth, QStringLiteral("here"), anchor);
	QVERIFY(Qdc::RichText::hasMarkup(fourth));

	/* A block kind counts even with no character formatting anywhere. */
	QTextDocument fifth;
	fifth.setPlainText(QStringLiteral("as you said"));

	QTextCursor cursor(&fifth);
	QTextBlockFormat quote = cursor.blockFormat();
	quote.setProperty(Qdc::RichText::BlockKindProperty, Qdc::RichText::Quote);
	cursor.setBlockFormat(quote);

	QVERIFY(Qdc::RichText::hasMarkup(fifth));
}

void TestRichText::boldItalicAndCodeRoundTrip()
{
	QTextCharFormat plain;
	QTextCharFormat bold;
	bold.setFontWeight(QFont::Bold);
	QTextCharFormat italic;
	italic.setFontItalic(true);
	QTextCharFormat code;
	code.setProperty(Qdc::RichText::CodeProperty, true);

	QTextDocument document;
	appendRun(document, QStringLiteral("found "), plain);
	appendRun(document, QStringLiteral("it"), bold);
	appendRun(document, QStringLiteral(" in "), plain);
	appendRun(document, QStringLiteral("Music"), italic);
	appendRun(document, QStringLiteral(" as "), plain);
	appendRun(document, QStringLiteral("ls -l"), code);

	const QString markdown = Qdc::RichText::toMarkdown(document);
	QCOMPARE(markdown, QStringLiteral("found **it** in *Music* as `ls -l`"));

	const QString html = Qdc::RichText::renderMarkdown(markdown, options());
	QVERIFY(html.contains(QStringLiteral("<b>it</b>")));
	QVERIFY(html.contains(QStringLiteral("<i>Music</i>")));
	/*
	 * The family is whatever this system calls its fixed-pitch font, so the assertion is
	 * that code is set in that one - not in a name written into the source.
	 */
	QVERIFY(html.contains(QStringLiteral("font-family:'%1'")
	                      .arg(Qdc::RichText::monospaceFamily())));
	QVERIFY(html.contains(QStringLiteral("ls -l")));

	/* And the markers themselves are gone from what is displayed. */
	QCOMPARE(Qdc::RichText::toPlainText(markdown),
	         QStringLiteral("found it in Music as ls -l"));
}

void TestRichText::boldAndItalicTogetherRoundTrip()
{
	QTextDocument document;

	QTextCharFormat both;
	both.setFontWeight(QFont::Bold);
	both.setFontItalic(true);

	QTextCharFormat plain;
	QTextCharFormat bold;
	bold.setFontWeight(QFont::Bold);

	appendRun(document, QStringLiteral("this is "), plain);
	appendRun(document, QStringLiteral("very"), both);
	appendRun(document, QStringLiteral(" important"), plain);

	const QString markdown = Qdc::RichText::toMarkdown(document);
	QCOMPARE(markdown, QStringLiteral("this is ***very*** important"));

	const QString html = Qdc::RichText::renderMarkdown(markdown, options());

	/*
	 * Both, and with nothing left over. A scanner that took "***" for "**" used to emit
	 * the third asterisk as text and lose the italic with it.
	 */
	QVERIFY2(html.contains(QStringLiteral("<b><i>very</i></b>")), qPrintable(html));
	QVERIFY2(!html.contains(QStringLiteral("*")), qPrintable(html));

	QCOMPARE(Qdc::RichText::toPlainText(markdown),
	         QStringLiteral("this is very important"));

	/* Bold next to bold-and-italic, which is five markers in a row on the wire. */
	QTextDocument run;
	appendRun(run, QStringLiteral("loud"), bold);
	appendRun(run, QStringLiteral("louder"), both);

	const QString together = Qdc::RichText::toMarkdown(run);
	QCOMPARE(together, QStringLiteral("**loud*****louder***"));

	const QString rendered = Qdc::RichText::renderMarkdown(together, options());
	QVERIFY2(rendered.contains(QStringLiteral("<b>loud</b>")), qPrintable(rendered));
	QVERIFY2(rendered.contains(QStringLiteral("<b><i>louder</i></b>")),
	         qPrintable(rendered));

	/* And a sender who nested them by hand rather than as one run. */
	const QString nested = Qdc::RichText::renderMarkdown(
		QStringLiteral("**all *of* it**"), options());

	QVERIFY2(nested.contains(QStringLiteral("<i>of</i>")), qPrintable(nested));
	QVERIFY2(nested.contains(QStringLiteral("<b>all ")), qPrintable(nested));

	/*
	 * A marker with whitespace after it opens nothing, so arithmetic and a stray
	 * asterisk stay as they were typed rather than swallowing the rest of the line.
	 */
	const QString lone = Qdc::RichText::renderMarkdown(
		QStringLiteral("2 * 3 and *this* only"), options());

	QVERIFY2(lone.contains(QStringLiteral("2 * 3 and <i>this</i> only")),
	         qPrintable(lone));

	/* Leaning on the key is still emphasis, with nothing spilled either side. */
	const QString heavy = Qdc::RichText::renderMarkdown(
		QStringLiteral("****four****"), options());

	QVERIFY2(heavy.contains(QStringLiteral("<b><i>four</i></b>")), qPrintable(heavy));
	QVERIFY2(!heavy.contains(QStringLiteral("*")), qPrintable(heavy));

	/*
	 * Emphasis on a run that has spaces at its edges: the markers move inside them,
	 * because markdown beside whitespace is not a marker at all.
	 */
	QTextDocument padded;
	appendRun(padded, QStringLiteral("keep "), plain);
	appendRun(padded, QStringLiteral(" spaced "), both);
	appendRun(padded, QStringLiteral("out"), plain);

	const QString spaced = Qdc::RichText::toMarkdown(padded);
	QCOMPARE(spaced, QStringLiteral("keep  ***spaced*** out"));

	QVERIFY(Qdc::RichText::renderMarkdown(spaced, options())
	        .contains(QStringLiteral("<b><i>spaced</i></b>")));
}

void TestRichText::quoteAndCodeBlockRoundTrip()
{
	QTextDocument document;

	QTextCursor cursor(&document);
	cursor.insertText(QStringLiteral("as you said"));

	QTextBlockFormat quote = cursor.blockFormat();
	quote.setProperty(Qdc::RichText::BlockKindProperty, Qdc::RichText::Quote);
	cursor.setBlockFormat(quote);

	cursor.insertBlock();
	QTextBlockFormat body;
	body.setProperty(Qdc::RichText::BlockKindProperty, Qdc::RichText::Paragraph);
	cursor.setBlockFormat(body);
	cursor.insertText(QStringLiteral("agreed"));

	cursor.insertBlock();
	QTextBlockFormat block;
	block.setProperty(Qdc::RichText::BlockKindProperty, Qdc::RichText::CodeBlock);
	cursor.setBlockFormat(block);
	cursor.insertText(QStringLiteral("  indented();"));

	const QString markdown = Qdc::RichText::toMarkdown(document);

	QCOMPARE(markdown, QStringLiteral("> as you said\nagreed\n```\n  indented();\n```"));

	const QString html = Qdc::RichText::renderMarkdown(markdown, options());

	/* Indentation inside a code block has to survive the trip to HTML. */
	QVERIFY(html.contains(QStringLiteral("&nbsp;&nbsp;indented();")));
	QVERIFY(html.contains(options().colours.quoteBar.name()));

	QCOMPARE(Qdc::RichText::toPlainText(markdown),
	         QStringLiteral("as you said\nagreed\n  indented();"));
}

void TestRichText::listsRoundTripAndKeepTheirNumbers()
{
	QTextDocument document;

	QTextCursor cursor(&document);

	const auto item = [&cursor](Qdc::RichText::BlockKind kind, const QString& text,
	                            bool first) {
		if (!first) cursor.insertBlock();

		QTextBlockFormat format = cursor.blockFormat();
		format.setProperty(Qdc::RichText::BlockKindProperty, kind);
		cursor.setBlockFormat(format);

		cursor.insertText(text);
	};

	item(Qdc::RichText::Bullet, QStringLiteral("first"), true);
	item(Qdc::RichText::Bullet, QStringLiteral("second"), false);

	QCOMPARE(Qdc::RichText::toMarkdown(document),
	         QStringLiteral("- first\n- second"));

	QTextDocument numbered;
	cursor = QTextCursor(&numbered);

	item(Qdc::RichText::Number, QStringLiteral("one"), true);
	item(Qdc::RichText::Number, QStringLiteral("two"), false);
	item(Qdc::RichText::Paragraph, QStringLiteral("prose between"), false);
	item(Qdc::RichText::Number, QStringLiteral("one again"), false);

	/* A list further down starts at one again rather than counting on. */
	QCOMPARE(Qdc::RichText::toMarkdown(numbered),
	         QStringLiteral("1. one\n2. two\nprose between\n1. one again"));

	const QString html = Qdc::RichText::renderMarkdown(
		QStringLiteral("- first\n- second"), options());

	/* A bullet per item, and both indented to the same column. */
	QCOMPARE(html.count(QStringLiteral("&#8226;")), 2);

	const QString ordered = Qdc::RichText::renderMarkdown(
		QStringLiteral("3. three\n4. four"), options());

	/*
	 * The numbers as the sender wrote them: a list that arrived split across two
	 * messages still reads in order.
	 */
	QVERIFY(ordered.contains(QStringLiteral("3.")));
	QVERIFY(ordered.contains(QStringLiteral("4.")));

	QCOMPARE(Qdc::RichText::toPlainText(QStringLiteral("- first\n- second")),
	         QStringLiteral("\u2022 first\n\u2022 second"));

	/* Markup inside an item is still markup. */
	const QString emphasised = Qdc::RichText::renderMarkdown(
		QStringLiteral("- **loud** item"), options());
	QVERIFY(emphasised.contains(QStringLiteral("<b>loud</b>")));
}

void TestRichText::proseThatOpensLikeAMarkerStaysProse()
{
	QTextDocument document;

	QTextCursor cursor(&document);
	QTextCharFormat bold;
	bold.setFontWeight(QFont::Bold);

	/*
	 * The message carries formatting, so it goes out as markdown - and every plain line
	 * in it has to survive that. A line somebody began with a dash is a dash.
	 */
	cursor.insertText(QStringLiteral("- not a list"));
	cursor.insertBlock();
	cursor.insertText(QStringLiteral("1. not numbered"));
	cursor.insertBlock();
	cursor.insertText(QStringLiteral("> not a quote"));
	cursor.insertBlock();
	cursor.insertText(QStringLiteral("really"), bold);

	const QString markdown = Qdc::RichText::toMarkdown(document);

	QCOMPARE(markdown,
	         QStringLiteral("\\- not a list\n\\1. not numbered\n"
	                        "\\> not a quote\n**really**"));

	/* And it comes back out the way it went in. */
	QCOMPARE(Qdc::RichText::toPlainText(markdown),
	         QStringLiteral("- not a list\n1. not numbered\n> not a quote\nreally"));
}

void TestRichText::literalMarkersSurviveAFormattedMessage()
{
    QTextCharFormat plain;
	QTextCharFormat bold;
	bold.setFontWeight(QFont::Bold);

	QTextDocument document;
	appendRun(document, QStringLiteral("2 * 3 and [x] and a\\b "), plain);
	appendRun(document, QStringLiteral("really"), bold);

	const QString markdown = Qdc::RichText::toMarkdown(document);

	/*
	 * The message is going out with RT1, so anything in a plain run that the reader
	 * would otherwise take as a marker is escaped. Without this, "2 * 3 and ... really"
	 * would emphasise everything between the asterisk and the bold.
	 */
	QCOMPARE(markdown,
	         QStringLiteral("2 \\* 3 and \\[x\\] and a\\\\b **really**"));

	/* And it comes back out exactly as typed. */
	QCOMPARE(Qdc::RichText::toPlainText(markdown),
	         QStringLiteral("2 * 3 and [x] and a\\b really"));

	const QString html = Qdc::RichText::renderMarkdown(markdown, options());
	QVERIFY(html.contains(QStringLiteral("2 * 3")));
	QVERIFY(html.contains(QStringLiteral("<b>really</b>")));

	/* Underscores are not emphasis here, so they are left alone. */
	QTextDocument snake;
	appendRun(snake, QStringLiteral("call get_user_name()"), bold);
	QCOMPARE(Qdc::RichText::toMarkdown(snake),
	         QStringLiteral("**call get_user_name()**"));
}

void TestRichText::codeSpansContainingBackticksSurvive()
{
	QTextCharFormat code;
	code.setProperty(Qdc::RichText::CodeProperty, true);

	QTextDocument document;
	appendRun(document, QStringLiteral("a ` b"), code);

	const QString markdown = Qdc::RichText::toMarkdown(document);

	/* A longer fence, or the backtick inside would close the span early. */
	QCOMPARE(markdown, QStringLiteral("``a ` b``"));
	QCOMPARE(Qdc::RichText::toPlainText(markdown), QStringLiteral("a ` b"));
}

void TestRichText::linksRoundTripAndAreScopedToKnownSchemes()
{
	using Qdc::RichText::isLinkable;
	using Qdc::RichText::isHubUrl;

	QVERIFY(isLinkable(QUrl(QStringLiteral("https://example.org/rules"))));
	QVERIFY(isLinkable(QUrl(QStringLiteral("http://example.org/"))));
	QVERIFY(isLinkable(QUrl(QStringLiteral("adc://hub.example.org:412"))));
	QVERIFY(isLinkable(QUrl(QStringLiteral("adcs://hub.example.org:412"))));
	QVERIFY(isLinkable(QUrl(QStringLiteral("dchub://hub.example.org:411"))));

	/* Only the hub schemes are ours to open. */
	QVERIFY(isHubUrl(QUrl(QStringLiteral("adcs://hub.example.org:412"))));
	QVERIFY(!isHubUrl(QUrl(QStringLiteral("https://example.org/"))));

	/*
	 * Everything else is refused. A remote user picks the href, so this list is the
	 * whole defence - file: would read the disk and javascript: must never normalise
	 * into something a future widget honours.
	 */
	QVERIFY(!isLinkable(QUrl(QStringLiteral("file:///etc/passwd"))));
	QVERIFY(!isLinkable(QUrl(QStringLiteral("javascript:alert(1)"))));
	QVERIFY(!isLinkable(QUrl(QStringLiteral("data:text/html,<b>x</b>"))));
	QVERIFY(!isLinkable(QUrl(QStringLiteral("qrc:/secret"))));
	QVERIFY(!isLinkable(QUrl()));

	QTextCharFormat anchor;
	anchor.setAnchor(true);
	anchor.setAnchorHref(QStringLiteral("https://example.org/rules"));

	QTextDocument document;
	appendRun(document, QStringLiteral("the rules"), anchor);

	const QString markdown = Qdc::RichText::toMarkdown(document);
	QCOMPARE(markdown,
	         QStringLiteral("[the rules](https://example.org/rules)"));

	const QString html = Qdc::RichText::renderMarkdown(markdown, options());
	QVERIFY(html.contains(QStringLiteral("<a href=\"https://example.org/rules\"")));
	QVERIFY(html.contains(QStringLiteral("the rules")));

	/* A refused scheme is shown as the text it is, not dropped and not linked. */
	const QString hostile = Qdc::RichText::renderMarkdown(
		QStringLiteral("[click](javascript:alert(1))"), options());
	QVERIFY(!hostile.contains(QStringLiteral("<a ")));
	QVERIFY(hostile.contains(QStringLiteral("javascript")));
}

void TestRichText::bareUrlsAreLinkedWithoutTrailingPunctuation()
{
	const QString html = Qdc::RichText::renderMarkdown(
		QStringLiteral("see https://example.org/rules. thanks"), options());

	QVERIFY(html.contains(QStringLiteral("<a href=\"https://example.org/rules\"")));

	/* The full stop ended the sentence, not the address. */
	QVERIFY(!html.contains(QStringLiteral("rules.\"")));
	QVERIFY(html.contains(QStringLiteral(". thanks")));

	/* A hub address in chat is a link too - that is how one gets clicked. */
	const QString hub = Qdc::RichText::renderMarkdown(
		QStringLiteral("join adcs://hub.example.org:412 now"), options());
	QVERIFY(hub.contains(QStringLiteral("<a href=\"adcs://hub.example.org:412\"")));

	/* Something that merely looks like a scheme is not linked. */
	const QString bare = Qdc::RichText::renderMarkdown(
		QStringLiteral("ratio 3://4"), options());
	QVERIFY(!bare.contains(QStringLiteral("<a ")));
}

void TestRichText::remoteTextCannotInjectMarkup()
{
	const Qdc::RichText::RenderOptions o = options();

	/*
	 * The input is escaped before anything else and the only tags in the output are
	 * ones renderMarkdown() wrote. This is the reason for not handing the text to
	 * QTextDocument::setMarkdown(), which would accept all of CommonMark from a
	 * stranger - images included, and an image sends the document to fetch a resource.
	 */
	const QStringList hostile = {
		QStringLiteral("<script>alert(1)</script>"),
		QStringLiteral("</div><div style=\"margin-left:9999px\">shove"),
		QStringLiteral("<img src=\"http://tracker.example/pixel.png\">"),
		QStringLiteral("<a href=\"file:///etc/passwd\">x</a>"),
		QStringLiteral("<b onclick=\"x\">y</b>"),
		QStringLiteral("![image](http://tracker.example/pixel.png)")
	};

	/*
	 * Only tags this module writes itself. Anything a message could name - script,
	 * img, style, iframe - is absent because the text was escaped before any markup
	 * was added.
	 */
	static const QStringList allowed = {
		QStringLiteral("div"), QStringLiteral("span"), QStringLiteral("b"),
		QStringLiteral("i"), QStringLiteral("a"), QStringLiteral("br")
	};

	for (const QString& text : hostile)
	{
		const QString html = Qdc::RichText::renderMarkdown(text, o);

		for (const QString& name : tagNames(html))
		{
			QVERIFY2(allowed.contains(name),
			         qPrintable(QStringLiteral("tag <%1> survived: %2")
			                    .arg(name, html)));
		}

		/* Any anchor that did survive points somewhere we are willing to open. */
		for (const QString& target : anchorTargets(html))
		{
			QVERIFY2(Qdc::RichText::isLinkable(QUrl(target)),
			         qPrintable(QStringLiteral("anchor to '%1' survived: %2")
			                    .arg(target, html)));
		}

		/* The angle brackets are visible text, so nothing has been swallowed. */
		if (text.startsWith(QLatin1Char('<')))
			QVERIFY(html.contains(QStringLiteral("&lt;")));
	}

	/* An unbalanced marker is text, not an open tag. */
	const QString stray = Qdc::RichText::renderMarkdown(
		QStringLiteral("2 ** 3 and `unclosed"), o);
	QVERIFY(stray.contains(QStringLiteral("2 ** 3")));
	QVERIFY(stray.contains(QStringLiteral("`unclosed")));
}

void TestRichText::plainRenderingKeepsLineBreaksAndMarkers()
{
	Qdc::RichText::RenderOptions o = options();
	o.prefix = QStringLiteral("<b>alice</b> ");

	/*
	 * A message that arrived without RT1 is not markdown and is not looked at as such,
	 * which is what lets a literal asterisk stay literal.
	 */
	const QString html = Qdc::RichText::renderPlain(
		QStringLiteral("weighs *about* 3kg\r\nmaybe 4"), o);

	QVERIFY(html.contains(QStringLiteral("*about*")));
	QVERIFY(!html.contains(QStringLiteral("<i>")));
	QVERIFY(html.contains(QStringLiteral("<br>")));
	QVERIFY(html.contains(QStringLiteral("<b>alice</b>")));

	/* And it is still escaped. */
	const QString escaped = Qdc::RichText::renderPlain(
		QStringLiteral("<script>x</script>"), o);
	QVERIFY(!escaped.contains(QStringLiteral("<script")));
	QVERIFY(escaped.contains(QStringLiteral("&lt;script")));
}

void TestRichText::toPlainTextStripsMarkers()
{
	QCOMPARE(Qdc::RichText::toPlainText(
	             QStringLiteral("**bold** and *thin* and `code`")),
	         QStringLiteral("bold and thin and code"));

	QCOMPARE(Qdc::RichText::toPlainText(
	             QStringLiteral("[the rules](https://example.org/)")),
	         QStringLiteral("the rules"));

	QCOMPARE(Qdc::RichText::toPlainText(QStringLiteral("> quoted\nplain")),
	         QStringLiteral("quoted\nplain"));
}

void TestRichText::theHangingIndentOnlyPullsBackTheFirstBlock()
{
	Qdc::RichText::RenderOptions o = options();
	o.prefix = QStringLiteral("<b>alice</b> ");

	const QString html = Qdc::RichText::renderMarkdown(
		QStringLiteral("look:\n```\ncode();\n```"), o);

	/*
	 * A first-line indent applies to the first line of every block it is set on, so
	 * setting it on all of them would drag the top of the code block out under the
	 * timestamp. Exactly one block may carry it.
	 */
	QCOMPARE(html.count(QStringLiteral("text-indent:")), 1);
	QVERIFY(html.indexOf(QStringLiteral("text-indent:"))
	        < html.indexOf(QStringLiteral("code();")));

	/* Every block is still pushed across to the text column. */
	QVERIFY(html.count(QStringLiteral("margin-left:40px")) >= 2);

	/* A message opening with a code block still gets its speaker on a line above. */
	const QString leading = Qdc::RichText::renderMarkdown(
		QStringLiteral("```\ncode();\n```"), o);
	QVERIFY(leading.indexOf(QStringLiteral("<b>alice</b>"))
	        < leading.indexOf(QStringLiteral("code();")));
	QVERIFY(!leading.contains(QStringLiteral("monospace; color:%1\"><b>alice")));
}

QTEST_MAIN(TestRichText)

#include "tst_richtext.moc"

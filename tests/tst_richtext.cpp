/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <gui/richtext.h>

#include <QAbstractTextDocumentLayout>
#include <QImage>
#include <QPainter>
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
	void magnetAttachmentsBecomeChips();
	void aPictureThatIsHereIsDrawnInline();
	void bareUrlsAreLinkedWithoutTrailingPunctuation();
	void plainMessagesLinkTheAddressesInThem();
	void aBareMagnetIsLinkedAlthoughItNamesNoHost();
	void nothingThatMerelyEndsInAColonIsLinked();
	void remoteTextCannotInjectMarkup();
	void plainRenderingKeepsLineBreaksAndMarkers();
	void toPlainTextStripsMarkers();
	void theHangingIndentOnlyPullsBackTheFirstBlock();
	void everyCommonMarkConstructReachesTheFeed();
	void nestingIsDrawnAsNesting();
	void aParagraphBreakIsVisibleAsOne();
	void nothingEscapesTheTextColumn();
	void aHostileNameIsDisarmedBeforeItIsDrawn();
	void aLongNameIsBoundedWithoutSplittingACharacter();
	void anOrdinaryNameIsDrawnAsItArrived();
	void aTooltipIsDrawnAsTextRatherThanAsMarkup();
	void aTooltipKeepsItsOwnStructureAroundTheFieldsInIt();
	void aTooltipIsEscapedAfterTheNameInItIsReduced();
	void anOrdinaryTooltipIsNotVisiblyEscaped();

private:
	static Qdc::RichText::RenderOptions options();

	/**
	 * What a tooltip built from 'text' would actually read as on screen.
	 *
	 * The two steps QLabel takes when it is left on Qt::AutoText, which is where QToolTip
	 * leaves the label it draws in: ask Qt::mightBeRichText(), and parse the string as HTML
	 * when the answer is yes. The hole this is about is in what Qt does with a string
	 * rather than in how the string looks, so this asks Qt.
	 */
	static QString asShown(const QString& text);

	/**
	 * Whether anything in 'document' is a picture or a link.
	 *
	 * Asked of the parsed document and not of the text: an image is a resource the document
	 * goes and fetches and a link is somewhere the reader can be sent, so what matters is
	 * whether either survived being parsed - not whether the text spells one out.
	 */
	static bool hasImageOrAnchor(const QTextDocument& document);

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

QString TestRichText::asShown(const QString& text)
{
	QTextDocument document;

	if (Qt::mightBeRichText(text))
		document.setHtml(text);
	else
		document.setPlainText(text);

	return document.toPlainText();
}

bool TestRichText::hasImageOrAnchor(const QTextDocument& document)
{
	for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
	{
		for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
		{
			const QTextCharFormat format = it.fragment().charFormat();
			if (format.isImageFormat() || format.isAnchor()) return true;
		}
	}

	return false;
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
	o.colours.chip = QColor(0x20, 0x20, 0x20);
	o.colours.chipBack = QColor(0xe8, 0xe8, 0xe8);
	o.fontPointSize = 10.0;
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
	 * Both, and with nothing left over. The emphasis is outside the strong, which is the
	 * nesting CommonMark specifies for a run of three: the closing run pairs with the
	 * innermost opener it can, so the outer pair is the single asterisk.
	 */
	QVERIFY2(html.contains(QStringLiteral("<i><b>very</b></i>")), qPrintable(html));
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
	QVERIFY2(rendered.contains(QStringLiteral("<i><b>louder</b></i>")),
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

	/*
	 * Leaning on the key is still emphasis, with nothing spilled either side. Four
	 * markers pair up two at a time, which is strong inside strong rather than the
	 * strongest thing there is - CommonMark counts them rather than capping them.
	 */
	const QString heavy = Qdc::RichText::renderMarkdown(
		QStringLiteral("****four****"), options());

	QVERIFY2(heavy.contains(QStringLiteral("<b><b>four</b></b>")), qPrintable(heavy));
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
	        .contains(QStringLiteral("<i><b>spaced</b></i>")));
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

	/*
	 * The numbered line is escaped at its full stop rather than in front of the digit: a
	 * backslash before a "1" escapes nothing, so it would arrive as a visible backslash
	 * and the line would still be a list.
	 */
	QCOMPARE(markdown,
	         QStringLiteral("\\- not a list\n1\\. not numbered\n"
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

void TestRichText::magnetAttachmentsBecomeChips()
{
	const QString tth = QStringLiteral("VN6TQGXQ7HYQ5ARWZ2LNFVQ7YQKGXQ7HYQ5ARWZ");

	Qdc::RichText::Attachment made;
	made.tth = tth;
	made.name = QStringLiteral("notes.pdf");
	made.size = 1048576;

	const QString uri = Qdc::RichText::writeMagnet(made);
	QVERIFY2(uri.startsWith(QStringLiteral("magnet:?")), qPrintable(uri));

	/* And back again, with nothing lost on the way. */
	const Qdc::RichText::Attachment read = Qdc::RichText::readMagnet(QUrl(uri));
	QVERIFY(read.isValid());
	QCOMPARE(read.tth, tth);
	QCOMPARE(read.name, QStringLiteral("notes.pdf"));
	QCOMPARE(read.size, 1048576u);

	/* A magnet is linkable although it has no host at all; nonsense in one is not. */
	QVERIFY(Qdc::RichText::isLinkable(QUrl(uri)));
	QVERIFY(!Qdc::RichText::isLinkable(
		QUrl(QStringLiteral("magnet:?xt=urn:sha1:nothing&dn=x"))));
	QVERIFY(!Qdc::RichText::isLinkable(QUrl(QStringLiteral("magnet:?dn=nameless"))));

	const QString html = Qdc::RichText::renderMarkdown(
		QStringLiteral("here: [notes.pdf](%1)").arg(uri), options());

	/*
	 * The chip says what the file is and how big it is. The hash is not shown - nobody
	 * can read one - but it is what the anchor carries, so a click knows what to ask for.
	 */
	QVERIFY2(html.contains(QStringLiteral("notes.pdf")), qPrintable(html));
	QVERIFY2(html.contains(QStringLiteral("1.0 MiB")), qPrintable(html));
	QVERIFY2(html.contains(tth), qPrintable(html));

	/* The image form is the same file offered for showing, and renders as a chip too
	   until somebody asks for it. */
	const QString shown = Qdc::RichText::renderMarkdown(
		QStringLiteral("![a diagram](%1)").arg(uri), options());

	QVERIFY2(shown.contains(QStringLiteral("notes.pdf")), qPrintable(shown));
	QVERIFY2(!shown.contains(QStringLiteral("![")), qPrintable(shown));

	/* Stripped of markup it still reads as a file somebody offered. */
	QCOMPARE(Qdc::RichText::toPlainText(
		QStringLiteral("[notes.pdf](%1)").arg(uri)),
		QStringLiteral("notes.pdf (1.0 MiB)"));

	QCOMPARE(Qdc::RichText::describeSize(0), QStringLiteral("0 bytes"));
	QCOMPARE(Qdc::RichText::describeSize(700), QStringLiteral("700 bytes"));
	QCOMPARE(Qdc::RichText::describeSize(1024), QStringLiteral("1.0 KiB"));
	QCOMPARE(Qdc::RichText::describeSize(3ull * 1024 * 1024 * 1024),
	         QStringLiteral("3.0 GiB"));

	/*
	 * A display name is a name and never a path: a sender who writes one is trying to
	 * choose where the file lands, which is not theirs to choose.
	 */
	const Qdc::RichText::Attachment sneaky = Qdc::RichText::readMagnet(
		QUrl(QStringLiteral("magnet:?xt=urn:tree:tiger:%1&dn=../../.ssh/authorized_keys")
			.arg(tth)));

	QVERIFY(sneaky.isValid());
	QCOMPARE(sneaky.name, QStringLiteral("authorized_keys"));
}

void TestRichText::aPictureThatIsHereIsDrawnInline()
{
	const QString tth = QStringLiteral("VN6TQGXQ7HYQ5ARWZ2LNFVQ7YQKGXQ7HYQ5ARWZ");

	Qdc::RichText::Attachment picture;
	picture.tth = tth;
	picture.name = QStringLiteral("photo.png");
	picture.size = 524288;

	const QString shown = QStringLiteral("![a photo](%1)")
		.arg(Qdc::RichText::writeMagnet(picture));

	/* Nobody has fetched it, so there is something to click rather than something to
	   look at. */
	Qdc::RichText::RenderOptions plain = options();
	const QString asChip = Qdc::RichText::renderMarkdown(shown, plain);

	QVERIFY2(!asChip.contains(QStringLiteral("<img")), qPrintable(asChip));
	QVERIFY2(asChip.contains(QStringLiteral("photo.png")), qPrintable(asChip));

	/* Once it is here, it is drawn - scaled to the room it may have. */
	Qdc::RichText::RenderOptions withMedia = options();
	withMedia.mediaWidth = 400;
	withMedia.mediaHeight = 300;
	withMedia.media = [tth](const QString& asked) {
		Qdc::RichText::LocalMedia here;
		if (asked != tth) return here;

		here.path = QStringLiteral("/tmp/photo.png");
		here.size = QSize(1600, 1200);
		return here;
	};

	const QString drawn = Qdc::RichText::renderMarkdown(shown, withMedia);

	QVERIFY2(drawn.contains(QStringLiteral("<img")), qPrintable(drawn));
	QVERIFY2(drawn.contains(QStringLiteral("src=\"/tmp/photo.png\"")), qPrintable(drawn));

	/*
	 * 1600x1200 into a 400 wide column is 400x300, which is also exactly the height it
	 * may have. Both dimensions are stated so the shape survives.
	 */
	QVERIFY2(drawn.contains(QStringLiteral("width=\"400\"")), qPrintable(drawn));
	QVERIFY2(drawn.contains(QStringLiteral("height=\"300\"")), qPrintable(drawn));

	/* Clicking it goes where clicking the chip would: the file, by its hash. */
	QVERIFY2(drawn.contains(QStringLiteral("<a href=\"magnet:")), qPrintable(drawn));
	QVERIFY2(drawn.contains(tth), qPrintable(drawn));

	/* A tall picture is capped by height instead, and keeps its shape. */
	withMedia.media = [tth](const QString& asked) {
		Qdc::RichText::LocalMedia here;
		if (asked != tth) return here;

		here.path = QStringLiteral("/tmp/tall.png");
		here.size = QSize(600, 3000);
		return here;
	};

	const QString tall = Qdc::RichText::renderMarkdown(shown, withMedia);
	QVERIFY2(tall.contains(QStringLiteral("height=\"300\"")), qPrintable(tall));
	QVERIFY2(tall.contains(QStringLiteral("width=\"60\"")), qPrintable(tall));

	/* An offered file is never drawn, however available it is. */
	const QString offered = Qdc::RichText::renderMarkdown(
		QStringLiteral("[photo.png](%1)").arg(Qdc::RichText::writeMagnet(picture)),
		withMedia);

	QVERIFY2(!offered.contains(QStringLiteral("<img")), qPrintable(offered));
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

void TestRichText::plainMessagesLinkTheAddressesInThem()
{
	Qdc::RichText::RenderOptions o = options();
	o.prefix = QStringLiteral("<b>alice</b> ");

	/*
	 * A message without RT1 is not markdown, but an address in one is still the thing
	 * in it worth clicking - and most hubs cannot carry the flag that would make a
	 * message markdown in the first place.
	 */
	const QString html = Qdc::RichText::renderPlain(
		QStringLiteral("see https://example.org/rules and http://other.example.org"), o);

	QCOMPARE(anchorTargets(html), (QStringList{QStringLiteral("https://example.org/rules"),
	                                   QStringLiteral("http://other.example.org")}));

	/* A hub address too, which is how one gets opened from chat. */
	const QString hub = Qdc::RichText::renderPlain(
		QStringLiteral("join adcs://hub.example.org:412 now"), o);

	QCOMPARE(anchorTargets(hub),
	         (QStringList{QStringLiteral("adcs://hub.example.org:412")}));

	/* The full stop ended the sentence, not the address. */
	const QString stopped = Qdc::RichText::renderPlain(
		QStringLiteral("see https://example.org/rules. thanks"), o);

	QCOMPARE(anchorTargets(stopped),
	         (QStringList{QStringLiteral("https://example.org/rules")}));

	QVERIFY(stopped.contains(QStringLiteral(". thanks")));

	/*
	 * Everything else about a plain message is unchanged: an asterisk is still an
	 * asterisk, a line break is still a break, and markup from a stranger is still
	 * escaped rather than rendered.
	 */
	const QString literal = Qdc::RichText::renderPlain(
		QStringLiteral("weighs *about* 3kg\r\nsee <script>x</script>"), o);

	QVERIFY(literal.contains(QStringLiteral("*about*")));
	QVERIFY(literal.contains(QStringLiteral("<br>")));
	QVERIFY(!literal.contains(QStringLiteral("<script")));
	QVERIFY(literal.contains(QStringLiteral("&lt;script")));
	QCOMPARE(anchorTargets(literal), QStringList());

	/* And an address beside markup that is not markup here does not swallow it. */
	const QString mixed = Qdc::RichText::renderPlain(
		QStringLiteral("*https://example.org/*"), o);

	QCOMPARE(anchorTargets(mixed), (QStringList{QStringLiteral("https://example.org/")}));
}

void TestRichText::aBareMagnetIsLinkedAlthoughItNamesNoHost()
{
	const QString tth = QStringLiteral("VN6TQGXQ7HYQ5ARWZ2LNFVQ7YQKGXQ7HYQ5ARWZ");

	const QString uri =
		QStringLiteral("magnet:?xt=urn:tree:tiger:%1&xl=1024&dn=notes.txt").arg(tth);

	/*
	 * A magnet has a colon and no slashes after it, so the "scheme://" shape every
	 * other address has does not find one. It is the URI a file is offered by, which
	 * makes it the address most worth clicking of all.
	 */
	Qdc::RichText::RenderOptions o = options();

	/*
	 * Read back through a document rather than off the attribute, because a magnet
	 * separates its parameters with "&" and an href holds that escaped - which is
	 * correct HTML and not what a click would follow. What a click follows is what
	 * matters, so that is what is asserted.
	 */
	const auto linkedIn = [](const QString& html) {
		QTextDocument document;
		document.setHtml(html);

		QStringList found;

		for (QTextBlock block = document.begin(); block.isValid();
		     block = block.next())
		{
			for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
			{
				const QTextCharFormat format = it.fragment().charFormat();
				if (format.isAnchor()) found << format.anchorHref();
			}
		}

		return found;
	};

	const QString plain = Qdc::RichText::renderPlain(
		QStringLiteral("have a look at %1 if you like").arg(uri), o);

	QCOMPARE(linkedIn(plain), (QStringList{uri}));

	/* The same in a formatted message, which did not find one either. */
	const QString markdown = Qdc::RichText::renderMarkdown(
		QStringLiteral("have a look at %1 if you like").arg(uri), o);

	QCOMPARE(linkedIn(markdown), (QStringList{uri}));

	/*
	 * And it is drawn as the file it names rather than as the hash that names it: the
	 * URI is the one part of a magnet nobody can read, so what shows is the name and
	 * the size, exactly as a magnet written as a link is shown.
	 */
	QTextDocument shown;
	shown.setHtml(plain);

	const QString read = shown.toPlainText();

	QVERIFY2(read.contains(QStringLiteral("notes.txt")), qPrintable(read));
	QVERIFY2(read.contains(QStringLiteral("1.0 KiB")), qPrintable(read));

	/* The hash is not on screen at all. */
	QVERIFY2(!read.contains(tth), qPrintable(read));

	/* The words around it survive being replaced. */
	QVERIFY2(read.contains(QStringLiteral("have a look at")), qPrintable(read));
	QVERIFY2(read.contains(QStringLiteral("if you like")), qPrintable(read));

	/* A magnet with no usable hash names no file, so there is nothing to click. */
	const QString useless = Qdc::RichText::renderPlain(
		QStringLiteral("magnet:?dn=nameless here"), o);

	QCOMPARE(anchorTargets(useless), QStringList());

	/* An ordinary address is still shown as itself; only a magnet becomes a chip. */
	const QString page = Qdc::RichText::renderPlain(
		QStringLiteral("see https://example.org/rules"), o);

	QTextDocument web;
	web.setHtml(page);

	QVERIFY(web.toPlainText().contains(QStringLiteral("https://example.org/rules")));
}

void TestRichText::nothingThatMerelyEndsInAColonIsLinked()
{
	Qdc::RichText::RenderOptions o = options();

	/*
	 * The bare colon is accepted for magnet alone. Taking any word followed by one
	 * would make a link of the way people write a time, a label or a nickname.
	 */
	for (const QString& said : {QStringLiteral("note: bring cables"),
	                            QStringLiteral("starts at 12:30 sharp"),
	                            QStringLiteral("mailto:someone@example.org"),
	                            QStringLiteral("javascript:alert(1)"),
	                            QStringLiteral("file:///etc/passwd"),
	                            QStringLiteral("ratio 3://4")})
	{
		QCOMPARE(anchorTargets(Qdc::RichText::renderPlain(said, o)), QStringList());
		QCOMPARE(anchorTargets(Qdc::RichText::renderMarkdown(said, o)), QStringList());
	}
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

void TestRichText::aParagraphBreakIsVisibleAsOne()
{
	/*
	 * The complaint this answers: a blank line in a message and a single line break
	 * rendered the same, so the space the sender left was thrown away. What separates
	 * them is a margin, and Qt honours one given in pixels only - a margin in points is
	 * accepted and then ignored, which is why the unit is asserted here and not just the
	 * presence of a gap.
	 */
	const QString apart = Qdc::RichText::renderMarkdown(
		QStringLiteral("one\n\ntwo"), options());

	QVERIFY2(apart.contains(QStringLiteral("margin-top:8px")), qPrintable(apart));
	QCOMPARE(apart.count(QStringLiteral("<div")), 2);
	QVERIFY2(!apart.contains(QStringLiteral("<br>")), qPrintable(apart));

	/* A single break stays inside its paragraph, and gets no gap. */
	const QString together = Qdc::RichText::renderMarkdown(
		QStringLiteral("one\ntwo"), options());

	QVERIFY2(together.contains(QStringLiteral("<br>")), qPrintable(together));
	QVERIFY2(!together.contains(QStringLiteral("margin-top")), qPrintable(together));

	/*
	 * A tight list is one whose items were written without blank lines between them, so
	 * they stay together; a loose one asked for the space and gets it.
	 */
	const QString tight = Qdc::RichText::renderMarkdown(
		QStringLiteral("- one\n- two\n- three"), options());

	QVERIFY2(!tight.contains(QStringLiteral("margin-top")), qPrintable(tight));

	const QString loose = Qdc::RichText::renderMarkdown(
		QStringLiteral("- one\n\n- two\n\n- three"), options());

	QCOMPARE(loose.count(QStringLiteral("margin-top")), 2);

	/*
	 * A tight list still stands apart from the paragraph above it. Only the space between
	 * its items is suppressed, which is the distinction that got this wrong once.
	 */
	const QString after = Qdc::RichText::renderMarkdown(
		QStringLiteral("a paragraph\n\n- one\n- two"), options());

	QCOMPARE(after.count(QStringLiteral("margin-top")), 1);

	/* A quote holding two paragraphs keeps the break between them. */
	const QString quoted = Qdc::RichText::renderMarkdown(
		QStringLiteral("> one\n>\n> two"), options());

	QVERIFY2(quoted.contains(QStringLiteral("margin-top")), qPrintable(quoted));
}

void TestRichText::nothingEscapesTheTextColumn()
{
	/*
	 * Every line of a message sits in the text column, to the right of the timestamp and
	 * the speaker. The defect this pins is what happens when it does not: a block level
	 * tag - a heading, a rule, a table - nested inside the inline markup a quote wraps its
	 * lines in made Qt close the surrounding block out and lose the indent with it, so the
	 * element landed hard against the left edge while the rest of the message stayed put.
	 *
	 * Measured in pixels rather than asserted about the markup, because that is what went
	 * wrong: the html was exactly as intended and the layout disagreed with it.
	 */
	const int indent = 80;

	const QStringList messages = {
		QStringLiteral("intro\n\n# a heading"),
		QStringLiteral("intro\n\nSetext\n======"),
		QStringLiteral("intro\n\n> # a heading in a quote"),
		QStringLiteral("intro\n\n> quoted\n>\n> ---\n>\n> after the rule"),
		QStringLiteral("intro\n\n> | a | b |\n> |---|---|\n> | 1 | 2 |"),
		QStringLiteral("intro\n\n- item\n\n  ---\n\n- another"),
		QStringLiteral("intro\n\n- item\n  - nested\n\n> quoted\n> > deeper"),
		QStringLiteral("intro\n\n```\ncode\n```\n\n| a |\n|---|\n| 1 |")
	};

	for (const QString& markdown : messages)
	{
		Qdc::RichText::RenderOptions o = options();
		o.indent = indent;
		o.prefix = QStringLiteral("<b>alice</b> ");

		QTextDocument document;
		document.setDocumentMargin(0.0);
		document.setHtml(Qdc::RichText::renderMarkdown(markdown, o));
		document.setTextWidth(420);

		QImage image(420, int(document.size().height()) + 2, QImage::Format_RGB32);
		image.fill(Qt::white);

		QPainter painter(&image);
		QAbstractTextDocumentLayout::PaintContext context;
		document.documentLayout()->draw(&painter, context);
		painter.end();

		/*
		 * The first block carries the speaker, which is pulled back out to the left on
		 * purpose - see renderMarkdown(). Everything below it has to stay in the column.
		 */
		const int prefixBottom = int(
			document.documentLayout()->blockBoundingRect(document.begin()).bottom());

		int leftmost = image.width();
		int where = -1;

		for (int y = prefixBottom + 1; y < image.height(); ++y)
		{
			for (int x = 0; x < image.width(); ++x)
			{
				if (qGray(image.pixel(x, y)) >= 200) continue;

				if (x < leftmost) { leftmost = x; where = y; }
				break;
			}
		}

		if (where < 0) continue;

		QVERIFY2(leftmost >= indent - 2,
		         qPrintable(QStringLiteral("row %1 starts at x=%2, column is %3, in: %4")
		                    .arg(where).arg(leftmost).arg(indent)
		                    .arg(QString(markdown).replace(QLatin1Char('\n'),
		                                                   QStringLiteral("\\n")))));
	}
}

void TestRichText::everyCommonMarkConstructReachesTheFeed()
{
	/*
	 * The constructs the parser gained, each drawn as something rather than shown as its
	 * markers. This is the list a reader of a message is entitled to see working; that
	 * the parser understands them is tst_markdown's business.
	 */
	const QString html = Qdc::RichText::renderMarkdown(
		QStringLiteral("# Heading\n\n"
		               "Setext\n======\n\n"
		               "---\n\n"
		               "~~struck~~ and _emphasis_ and __strong__\n\n"
		               "    indented code\n\n"
		               "| a | b |\n|:--|--:|\n| 1 | 2 |\n\n"
		               "&amp; and &#65; and <https://example.org/>\n"),
		options());

	/* A heading is set larger and bold rather than shown with its hashes. */
	QVERIFY2(!html.contains(QStringLiteral("# Heading")), qPrintable(html));
	QVERIFY2(html.contains(QStringLiteral("font-size:14.5pt\">Heading")), qPrintable(html));

	/* An underlined line is the same heading by another spelling. */
	QVERIFY2(html.contains(QStringLiteral("font-size:14.5pt\">Setext")), qPrintable(html));
	QVERIFY2(!html.contains(QStringLiteral("======")), qPrintable(html));

	QVERIFY2(html.contains(QStringLiteral("<hr>")), qPrintable(html));
	QVERIFY2(html.contains(QStringLiteral("<s>struck</s>")), qPrintable(html));
	QVERIFY2(html.contains(QStringLiteral("<i>emphasis</i>")), qPrintable(html));
	QVERIFY2(html.contains(QStringLiteral("<b>strong</b>")), qPrintable(html));

	/* An indented block is code, and gets the panel a fenced one gets. */
	QVERIFY2(html.contains(QStringLiteral("background-color:#f0f0f0")), qPrintable(html));
	QVERIFY2(html.contains(QStringLiteral("indented&nbsp;code")), qPrintable(html));

	QVERIFY2(html.contains(QStringLiteral("<table")), qPrintable(html));
	QVERIFY2(html.contains(QStringLiteral("align=\"right\"")), qPrintable(html));

	/* An entity is the character it names, and an angle-bracketed URL is a link. */
	QVERIFY2(html.contains(QStringLiteral("&amp; and A")), qPrintable(html));
	QVERIFY2(html.contains(QStringLiteral("href=\"https://example.org/\"")),
	         qPrintable(html));

	/* And the accessible form of all of it is prose. */
	const QString prose = Qdc::RichText::toPlainText(
		QStringLiteral("# Heading\n\n~~struck~~ and `code`"));

	QCOMPARE(prose, QStringLiteral("Heading\nstruck and code"));
}

void TestRichText::nestingIsDrawnAsNesting()
{
	const QString html = Qdc::RichText::renderMarkdown(
		QStringLiteral("- outer\n  - inner\n    - deepest\n"), options());

	/* Three items, each one step further in than the last. */
	QCOMPARE(html.count(QStringLiteral("&#8226;")), 3);

	/*
	 * The indent of the three divs that carry a bullet. The message opens with a list
	 * rather than with prose, so there is a fourth div in front of them holding the
	 * speaker's name - see renderMarkdown().
	 */
	static const QRegularExpression margin(
		QStringLiteral("margin-left:(\\d+)px[^>]*>[^<]*&#8226;"));

	QList<int> indents;
	QRegularExpressionMatchIterator each = margin.globalMatch(html);
	while (each.hasNext()) indents.append(each.next().captured(1).toInt());

	QCOMPARE(indents.size(), 3);
	QVERIFY2(indents.at(0) < indents.at(1) && indents.at(1) < indents.at(2),
	         qPrintable(html));

	/* A quote inside a quote carries two rules rather than one. */
	const QString quoted = Qdc::RichText::renderMarkdown(
		QStringLiteral("> outer\n> > inner\n"), options());

	QVERIFY2(quoted.count(QStringLiteral("&#9615;")) >= 3, qPrintable(quoted));

	/* A code block inside a list item stays inside it, and stays monospace. */
	const QString mixed = Qdc::RichText::renderMarkdown(
		QStringLiteral("1. first\n\n   ```\n   cmake --build .\n   ```\n"), options());

	QVERIFY2(mixed.contains(QStringLiteral("cmake&nbsp;--build&nbsp;.")),
	         qPrintable(mixed));
	QVERIFY2(mixed.contains(QStringLiteral("1.&nbsp;")), qPrintable(mixed));
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
	/*
	 * And the speaker is outside the code span rather than set in it. Checked against
	 * what precedes the name rather than against a whole span, whose attributes are
	 * substituted from the options and would have to be rebuilt here to be matched -
	 * a needle carrying an unsubstituted placeholder matches nothing and passes always.
	 */
	const qsizetype speaker = leading.indexOf(QStringLiteral("<b>alice</b>"));
	QVERIFY(speaker > 0);

	const QString before = leading.left(speaker);
	QVERIFY2(!before.contains(QStringLiteral("font-family")),
	         "the speaker was drawn inside the code block's own span");
}

void TestRichText::aHostileNameIsDisarmedBeforeItIsDrawn()
{
	using Qdc::RichText::displayText;

	/*
	 * The code points are spelled out rather than typed in: half of what is being tested
	 * here is invisible, and a test whose intent cannot be read is no test.
	 */
	const QChar rlo(0x202e);
	const QChar zwsp(0x200b);
	const QChar marked = QChar(QChar::ReplacementCharacter);

	/* C0 goes, and the whitespace among it leaves a space where the break was. */
	QCOMPARE(displayText(QStringLiteral("al\x01ice") + QChar(0x1b)
	                     + QStringLiteral("bell")),
	         QStringLiteral("alicebell"));
	QCOMPARE(displayText(QStringLiteral("two\nlines\there")),
	         QStringLiteral("two lines here"));

	/* Qt breaks a line on the separators as readily as on a newline. */
	QCOMPARE(displayText(QStringLiteral("two") + QChar(0x2028)
	                     + QStringLiteral("lines") + QChar(0x2029)
	                     + QStringLiteral("here")),
	         QStringLiteral("two lines here"));

	/* C1 is as invisible as C0 and as little use in a row of a list. */
	QCOMPARE(displayText(QStringLiteral("a") + QChar(0x0085) + QChar(0x009f)
	                     + QStringLiteral("b")),
	         QStringLiteral("ab"));

	/*
	 * The attack itself: an override with nothing closing it, which reverses the rest of
	 * the name and makes an executable read as a picture. Marked rather than dropped, so
	 * that a name tampered with reads as tampered with instead of quietly reading as some
	 * third file that is not there either.
	 */
	const QString reversed = displayText(QStringLiteral("photo") + rlo
	                                     + QStringLiteral("gpj.exe"));

	QCOMPARE(reversed, QStringLiteral("photo") + marked + QStringLiteral("gpj.exe"));

	/* Every embedding, every override, every isolate, and the same answer for each. */
	for (char16_t control = 0x202a; control <= 0x202e; ++control)
	{
		QCOMPARE(displayText(QStringLiteral("a") + QChar(control) + QStringLiteral("b")),
		         QStringLiteral("a") + marked + QStringLiteral("b"));
	}

	for (char16_t control = 0x2066; control <= 0x2069; ++control)
	{
		QCOMPARE(displayText(QStringLiteral("a") + QChar(control) + QStringLiteral("b")),
		         QStringLiteral("a") + marked + QStringLiteral("b"));
	}

	/*
	 * The invisible ones are dropped instead. A zero-width space is how one name is made
	 * to look like another, and the direction marks reorder only the neutrals beside them
	 * - in neither case is there anything worth marking the place of.
	 */
	QCOMPARE(displayText(QStringLiteral("al") + zwsp + QStringLiteral("ice")),
	         QStringLiteral("alice"));
	QCOMPARE(displayText(QChar(0x200e) + QStringLiteral("al") + QChar(0x2060)
	                     + QStringLiteral("i") + QChar(0xfeff) + QStringLiteral("ce")
	                     + QChar(0x00ad) + QChar(0x200f)),
	         QStringLiteral("alice"));

	QCOMPARE(displayText(QString()), QString());

	/* Nothing but controls is nothing, and a row drawing nothing is the honest answer. */
	QCOMPARE(displayText(QStringLiteral("\x01\x02\x03\x7f") + zwsp), QString());

	/* Twice changes nothing: some of these values pass through more than one view. */
	QCOMPARE(displayText(reversed), reversed);
}

void TestRichText::aLongNameIsBoundedWithoutSplittingACharacter()
{
	using Qdc::RichText::displayText;

	const QChar ellipsis(0x2026);

	const QString bounded = displayText(QString(400, QLatin1Char('m')));

	/* The ellipsis is inside the bound rather than a unit past it. */
	QCOMPARE(bounded.size(), 128);
	QVERIFY(bounded.endsWith(ellipsis));
	QVERIFY(bounded.startsWith(QStringLiteral("mmm")));

	/* A name that fits is not marked as cut. */
	const QString fits = QString(128, QLatin1Char('m'));
	QCOMPARE(displayText(fits), fits);
	QVERIFY(!fits.contains(ellipsis));

	QCOMPARE(displayText(fits + QLatin1Char('m')).size(), 128);

	/* The bound is on what is drawn, so what was stripped does not count against it. */
	QCOMPARE(displayText(QString(200, QChar(0x200b)) + QStringLiteral("alice")),
	         QStringLiteral("alice"));

	/*
	 * QString is UTF-16, so anything above the BMP arrives as two units and the cut may
	 * land between them. A lone surrogate is not a character: it draws as a box and does
	 * not survive being converted to anything else, which is what this checks.
	 */
	QString emoji;
	for (int n = 0; n < 200; ++n) emoji += QStringLiteral("\U0001f600");

	const QString cut = displayText(emoji);

	QVERIFY2(cut.size() <= 128, qPrintable(QString::number(cut.size())));
	QVERIFY(cut.endsWith(ellipsis));

	/* Nothing unpaired left in front of the ellipsis, and none anywhere else. */
	QVERIFY(!cut.at(cut.size() - 2).isHighSurrogate());
	QCOMPARE(QString::fromUtf8(cut.toUtf8()), cut);

	int surrogates = 0;
	for (const QChar c : cut) if (c.isSurrogate()) ++surrogates;

	QCOMPARE(surrogates % 2, 0);
}

void TestRichText::anOrdinaryNameIsDrawnAsItArrived()
{
	using Qdc::RichText::displayText;

	/*
	 * The requirement this has to meet as much as any of the others: right-to-left text
	 * is not an attack, and a name written in it is nobody else's to rewrite. Nor is a
	 * name whose spelling needs a joiner - ZWNJ is orthography in Persian, and ZWJ is the
	 * joint in every emoji built out of a sequence.
	 */
	const QStringList names = {
		QStringLiteral("alice"),
		QStringLiteral("Ann-Marie O'Brien (away)"),
		QStringLiteral("\u0639\u0628\u062f \u0627\u0644\u0644\u0647"),
		QStringLiteral("\u05e9\u05dc\u05d5\u05dd \u05e2\u05dc\u05d9\u05db\u05dd"),
		QStringLiteral("\u0645\u06cc") + QChar(0x200c)
			+ QStringLiteral("\u062e\u0648\u0627\u0647\u0645"),
		QStringLiteral("\u042f\u043c\u0430\u043b"),
		QStringLiteral("\u65e5\u672c\u8a9e\u306e\u30cb\u30c3\u30af"),
		QStringLiteral("\U0001f469") + QChar(0x200d) + QStringLiteral("\U0001f4bb"),
		QStringLiteral("The Hub \u2014 films & arguments about them"),
		QStringLiteral("Symphony No. 5 [FLAC].zip")
	};

	for (const QString& name : names) QCOMPARE(displayText(name), name);
}

void TestRichText::aTooltipIsDrawnAsTextRatherThanAsMarkup()
{
	using Qdc::RichText::literalHtml;

	/*
	 * The hole itself, asserted first, so that what follows stays a test of Qt's behaviour
	 * rather than of an assumption about it: handed over as it arrived, a hub description
	 * of this shape is markup to the label a tooltip is drawn in, and the bold tag is gone
	 * from what is read because it was obeyed.
	 */
	QVERIFY(Qt::mightBeRichText(QStringLiteral("<b>Best</b> Hub")));
	QCOMPARE(asShown(QStringLiteral("<b>Best</b> Hub")), QStringLiteral("Best Hub"));

	const QStringList hostile = {
		QStringLiteral("<b>Best</b> Hub"),
		QStringLiteral("<img src=\"http://tracker.example/pixel.png\"> Hub"),
		QStringLiteral("<a href=\"file:///etc/passwd\">a friendly hub</a>"),
		QStringLiteral("films & arguments <about> them"),
		QStringLiteral("say \"<i>hello</i>\" & mean it"),

		/* Already spelled as entities, which is text and has to stay text. */
		QStringLiteral("&lt;b&gt;Best&lt;/b&gt; &amp; so on"),

		/* An attempt to close the wrapper and write markup outside it. */
		QStringLiteral("</p></html><img src=\"http://tracker.example/pixel.png\">"),

		/* The other thing the heuristic answers yes to on sight. */
		QStringLiteral("<!doctype html><b>x")
	};

	for (const QString& text : hostile)
	{
		const QString tooltip = literalHtml(text);

		/*
		 * Deliberately on the rich text path rather than hoping to stay off it. The
		 * string says outright what it is, so the heuristic is not consulted and cannot
		 * change its mind about some later name that trips it.
		 */
		QVERIFY2(Qt::mightBeRichText(tooltip), qPrintable(tooltip));

		/* And every character of what the stranger sent is read back as itself. */
		QCOMPARE(asShown(tooltip), text);

		QTextDocument document;
		document.setHtml(tooltip);

		QVERIFY2(!hasImageOrAnchor(document), qPrintable(tooltip));
	}
}

void TestRichText::aTooltipKeepsItsOwnStructureAroundTheFieldsInIt()
{
	using Qdc::RichText::displayText;
	using Qdc::RichText::literalHtml;

	/*
	 * A user's tooltip as the model builds one: lines the person chose, a line of ours
	 * among them, and a newline of ours between each pair. The structure has to survive as
	 * structure while none of the fields is allowed to add to it.
	 */
	const QStringList lines = {
		displayText(QStringLiteral("<b>alice</b>")),
		displayText(QStringLiteral("films & <i>arguments</i> about them")),
		QStringLiteral("Sharing 1.0 GiB"),
		displayText(QStringLiteral("<img src=\"http://tracker.example/pixel.png\">"))
	};

	const QString tooltip = literalHtml(lines.join(QLatin1Char('\n')));

	/* Three joins for four lines, and they are the only tags of ours in the body. */
	QCOMPARE(tooltip.count(QStringLiteral("<br>")), 3);
	QCOMPARE(asShown(tooltip).split(QLatin1Char('\n')), lines);

	QTextDocument document;
	document.setHtml(tooltip);
	QVERIFY(!hasImageOrAnchor(document));

	/* A field that tries to become a line of the structure stays inside its own. */
	const QString injected = literalHtml(
		displayText(QStringLiteral("alice</p><p>Operator")) + QLatin1Char('\n')
			+ QStringLiteral("Away"));

	QCOMPARE(asShown(injected).count(QLatin1Char('\n')), 1);
	QCOMPARE(asShown(injected),
	         QStringLiteral("alice</p><p>Operator\nAway"));

	/* A blank line the model asked for is still a blank line. */
	const QString gap = QStringLiteral("/tmp/x\n\nThis file is no longer there.");
	QCOMPARE(asShown(literalHtml(gap)), gap);
}

void TestRichText::aTooltipIsEscapedAfterTheNameInItIsReduced()
{
	using Qdc::RichText::displayText;
	using Qdc::RichText::literalHtml;

	const QChar ellipsis(0x2026);

	/*
	 * Reduced first and escaped second, so that the bound falls on the characters somebody
	 * reads rather than on the entities they are written as. An ampersand is one of the
	 * first and five of the second: in the other order this name would be cut to a fifth of
	 * what it is allowed, and the cut could land inside "&amp;" and draw as "&am".
	 */
	const QString shown = asShown(literalHtml(displayText(
		QString(400, QLatin1Char('&')))));

	QCOMPARE(shown.size(), 128);
	QCOMPARE(shown, QString(127, QLatin1Char('&')) + ellipsis);

	/* The two characters the reduction leaves behind are text and stay text. */
	const QChar marked(QChar::ReplacementCharacter);

	QCOMPARE(asShown(literalHtml(displayText(QStringLiteral("photo") + QChar(0x202e)
	                                         + QStringLiteral("gpj.exe")))),
	         QStringLiteral("photo") + marked + QStringLiteral("gpj.exe"));

	/*
	 * A path is escaped and not reduced. A user reads one in order to go and find the file
	 * with it, so it is shown however long it is - the leaf of it being a name a stranger
	 * chose is what the escaping is for.
	 */
	const QString path = QStringLiteral("/Users/x/Downloads/")
		+ QString(300, QLatin1Char('m')) + QStringLiteral("/a & b <c>.zip");

	QCOMPARE(asShown(literalHtml(path)), path);
	QVERIFY(!asShown(literalHtml(path)).contains(ellipsis));
}

void TestRichText::anOrdinaryTooltipIsNotVisiblyEscaped()
{
	using Qdc::RichText::literalHtml;

	/*
	 * The requirement that is easiest to lose sight of: almost every tooltip is ordinary,
	 * and one reading "films &amp; arguments" would be a worse fault than the one all this
	 * is about.
	 */
	const QStringList ordinary = {
		QStringLiteral("alice"),
		QStringLiteral("Ann-Marie O'Brien (away)"),
		QStringLiteral("The Hub — films & arguments about them"),
		QStringLiteral("Symphony No. 5 [FLAC].zip"),
		QStringLiteral("عبد الله"),
		QStringLiteral("TTH: FZTHYWQOHUMTAQKKQEBSAYQXNQK5FVWKEHXIWNQ"),

		/* Two spaces are not one: a run of them is part of the name that has them. */
		QStringLiteral("/Users/x/Downloads/Symphony  No. 5.zip")
	};

	for (const QString& text : ordinary) QCOMPARE(asShown(literalHtml(text)), text);

	/* Nothing to say and so nothing shown, rather than an empty box saying it. */
	QCOMPARE(literalHtml(QString()), QString());
}

QTEST_MAIN(TestRichText)

#include "tst_richtext.moc"

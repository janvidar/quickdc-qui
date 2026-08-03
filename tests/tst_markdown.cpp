/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <gui/markdown.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTest>

/**
 * CommonMark conformance, measured rather than claimed.
 *
 * The specification ships its examples as data, and this runs them: each one is parsed
 * and rendered back to standard CommonMark HTML, and compared with the HTML the
 * specification says it must produce. That is why Markdown::toCanonicalHtml() exists at
 * all - the chat renderer's output could not be compared with anything.
 *
 * The two sections about raw HTML are not in the data file. They are not a shortfall to
 * be fixed later: rendering markup that a stranger on a hub wrote is the thing this
 * parser exists to avoid, and an implementation that passed those examples would have
 * given that up. Everything else is expected to pass, and the examples that do not are
 * listed in DEVIATIONS with a reason each.
 */
class TestMarkdown : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();

	void conformsToTheSpecification();
	void rawHtmlIsTextRatherThanMarkup();
	void tablesAndStrikethroughAreUnderstood();
	void aDocumentIsNeverRejected();

private:
	struct Example {
		int number = 0;
		QString section;
		QString markdown;
		QString html;
	};

	QList<Example> m_examples;
};

/**
 * Every example that does not produce the specification's HTML, and why.
 *
 * Kept as a list rather than a count so that fixing one is a test failure too: an
 * example that starts passing has to be taken off here, which is what stops the list
 * from quietly growing into an excuse.
 */
namespace {

struct Deviation {
	int example;
	const char* reason;
};

/*
 * Fifteen of these are the same deviation wearing different hats: because a tag is text
 * rather than markup, it cannot shield what is inside it. CommonMark says the quotes in
 * <a href="**"> hold an attribute, so the asterisks in there are not emphasis; here the
 * whole tag is characters, and asterisks among characters are emphasis. Both the input
 * and the output of every one of these contains a tag, which is the tell.
 *
 * The last is Qt's: QString::toCaseFolded does simple case folding, so "ẞ" does not fold
 * to "SS" and a label written one way does not find a definition written the other.
 */
const Deviation DEVIATIONS[] = {
	{ 21,  "a tag is text, so a backslash in one is an escape like any other" },
	{ 31,  "a tag is text, so an entity in one is resolved like any other" },
	{ 201, "<bar> is a destination in angle brackets here, not an HTML tag" },
	{ 308, "an HTML comment does not separate two lists, having no block of its own" },
	{ 309, "an HTML comment does not separate a list from the code block after it" },
	{ 344, "a backtick inside a tag opens a code span, the tag being text" },
	{ 475, "asterisks inside a tag's attribute are emphasis, the tag being text" },
	{ 476, "asterisks inside a tag's attribute are emphasis, the tag being text" },
	{ 477, "underscores inside a tag's attribute are emphasis, the tag being text" },
	{ 491, "angle brackets around a destination are not a tag, so the link is text" },
	{ 494, "<b> is text here rather than a tag, so it is escaped in the output" },
	{ 524, "a bracket inside a tag's attribute closes a link, the tag being text" },
	{ 536, "a bracket inside a tag's attribute closes a link, the tag being text" },
	{ 540, "Qt folds case simply, so \"ẞ\" does not match a label written \"SS\"" },
	{ 642, "two spaces inside a tag are a hard break, the tag being text" },
	{ 643, "a backslash inside a tag is a hard break, the tag being text" }
};

}

void TestMarkdown::initTestCase()
{
	QFile file(QStringLiteral(QDC_TEST_DATA "/commonmark.json"));
	QVERIFY2(file.open(QIODevice::ReadOnly), qUtf8Printable(file.errorString()));

	QJsonParseError problem;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &problem);
	QVERIFY2(!document.isNull(), qUtf8Printable(problem.errorString()));

	for (const QJsonValue& one : document.array())
	{
		const QJsonObject object = one.toObject();

		Example example;
		example.number = object.value(QStringLiteral("n")).toInt();
		example.section = object.value(QStringLiteral("s")).toString();
		example.markdown = object.value(QStringLiteral("m")).toString();
		example.html = object.value(QStringLiteral("h")).toString();

		m_examples.append(example);
	}

	QVERIFY(m_examples.size() > 500);
}

void TestMarkdown::conformsToTheSpecification()
{
	QSet<int> expected;
	for (const Deviation& one : DEVIATIONS)
		if (one.example) expected.insert(one.example);

	QSet<int> failed;
	QHash<QString, int> bySection;

	for (const Example& example : m_examples)
	{
		const QString got =
			Qdc::Markdown::toCanonicalHtml(Qdc::Markdown::parse(example.markdown));

		if (got == example.html) continue;

		failed.insert(example.number);
		bySection[example.section]++;

		if (!expected.contains(example.number))
		{
			qDebug().noquote()
				<< QStringLiteral("example %1 (%2)\n  in:       %3\n  expected: %4\n"
				                  "  got:      %5")
					.arg(example.number)
					.arg(example.section,
					     QString(example.markdown).replace(QLatin1Char('\n'),
					                                       QStringLiteral("\\n")),
					     QString(example.html).replace(QLatin1Char('\n'),
					                                   QStringLiteral("\\n")),
					     QString(got).replace(QLatin1Char('\n'), QStringLiteral("\\n")));
		}
	}

	qInfo().noquote() << QStringLiteral("%1 of %2 examples pass")
		.arg(m_examples.size() - failed.size()).arg(m_examples.size());

	for (auto one = bySection.constBegin(); one != bySection.constEnd(); ++one)
		qInfo().noquote() << QStringLiteral("  %1 failing in %2").arg(one.value()).arg(one.key());

	/* Both directions: a new failure is a regression, and a fixed one is a stale entry. */
	const QSet<int> unexpected = failed - expected;
	const QSet<int> fixed = expected - failed;

	QVERIFY2(unexpected.isEmpty(),
	         qPrintable(QStringLiteral("%1 examples fail that should not")
	                    .arg(unexpected.size())));

	QVERIFY2(fixed.isEmpty(),
	         qPrintable(QStringLiteral("%1 examples in DEVIATIONS now pass")
	                    .arg(fixed.size())));
}

void TestMarkdown::rawHtmlIsTextRatherThanMarkup()
{
	/*
	 * The one place this parser deliberately parts company with CommonMark. A hub user
	 * writes these strings, so a tag has to come out as the characters it is made of -
	 * and the canonical renderer escaping them is what proves the tree holds text and
	 * not markup.
	 */
	const QStringList attempts = {
		QStringLiteral("<script>alert(1)</script>"),
		QStringLiteral("<div>\nblock\n</div>"),
		QStringLiteral("<img src=x onerror=alert(1)>"),
		QStringLiteral("a <b>bold</b> claim"),
		QStringLiteral("<!-- comment -->"),
		QStringLiteral("<?php echo 1; ?>"),
		QStringLiteral("<![CDATA[x]]>")
	};

	for (const QString& one : attempts)
	{
		const QString html =
			Qdc::Markdown::toCanonicalHtml(Qdc::Markdown::parse(one));

		QVERIFY2(!html.contains(QStringLiteral("<script")),
		         qUtf8Printable(one));
		QVERIFY2(!html.contains(QStringLiteral("<div")), qUtf8Printable(one));
		QVERIFY2(!html.contains(QStringLiteral("<img")), qUtf8Printable(one));
		QVERIFY2(!html.contains(QStringLiteral("<b>")), qUtf8Printable(one));
		QVERIFY2(html.contains(QStringLiteral("&lt;")), qUtf8Printable(one));
	}
}

void TestMarkdown::tablesAndStrikethroughAreUnderstood()
{
	const Qdc::Markdown::Node table = Qdc::Markdown::parse(
		QStringLiteral("| left | middle | right |\n"
		               "|:-----|:------:|------:|\n"
		               "| a    | b      | c     |\n"));

	QCOMPARE(table.children.size(), 1);
	QCOMPARE(table.children.at(0).kind, Qdc::Markdown::Kind::Table);

	const Qdc::Markdown::Node& rows = table.children.at(0);
	QCOMPARE(rows.children.size(), 2);

	QVERIFY(rows.children.at(0).header);
	QVERIFY(!rows.children.at(1).header);
	QCOMPARE(rows.children.at(0).children.size(), 3);

	QCOMPARE(rows.children.at(1).children.at(0).align, Qdc::Markdown::Align::Left);
	QCOMPARE(rows.children.at(1).children.at(1).align, Qdc::Markdown::Align::Centre);
	QCOMPARE(rows.children.at(1).children.at(2).align, Qdc::Markdown::Align::Right);

	/* A row with fewer cells than the header is padded rather than misaligned. */
	const Qdc::Markdown::Node ragged = Qdc::Markdown::parse(
		QStringLiteral("| a | b |\n|---|---|\n| one |\n"));

	QCOMPARE(ragged.children.at(0).children.at(1).children.size(), 2);

	const QString struck =
		Qdc::Markdown::toCanonicalHtml(Qdc::Markdown::parse(
			QStringLiteral("~~gone~~ and ~kept~")));

	QVERIFY2(struck.contains(QStringLiteral("<del>gone</del>")), qUtf8Printable(struck));
	QVERIFY2(struck.contains(QStringLiteral("<del>kept</del>")), qUtf8Printable(struck));
}

void TestMarkdown::aDocumentIsNeverRejected()
{
	/*
	 * parse() has no failure to report, so what matters is that nothing here reaches an
	 * assertion or runs away. These are the shapes that do it: unbalanced delimiters,
	 * deep nesting, and a reference that names itself.
	 */
	const QStringList awkward = {
		QString(),
		QStringLiteral("\n\n\n"),
		QStringLiteral("["),
		QStringLiteral("]"),
		QStringLiteral("[]("),
		QStringLiteral("![["),
		QStringLiteral("***********"),
		QStringLiteral("`````"),
		QStringLiteral("[a]: [a]\n[a]"),
		QStringLiteral("> > > > > > > > > > deep"),
		QStringLiteral("- - - - - - - - - - deep"),
		QString(500, QLatin1Char('*')) + QStringLiteral("x"),
		QStringLiteral("|||\n|---|\n|||"),
		QStringLiteral("&#0; &#xFFFFFF; &notanentity;"),
		QStringLiteral("\t\t\ttabs"),
		QStringLiteral("a\r\nb\rc")
	};

	for (const QString& one : awkward)
	{
		const QString html =
			Qdc::Markdown::toCanonicalHtml(Qdc::Markdown::parse(one));
		Q_UNUSED(html);
	}

	QVERIFY(true);
}

QTEST_MAIN(TestMarkdown)

#include "tst_markdown.moc"

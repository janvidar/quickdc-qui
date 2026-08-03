/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <gui/theme.h>

#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>

#include <algorithm>

/**
 * The theme, as data.
 *
 * The scheme itself is a matter of taste and is not tested. What is tested is the
 * machinery that turns a file into one: that the stylesheet is found at all, that
 * every token in it resolves, and that a theme file layers onto another rather than
 * having to restate it.
 */
class TestTheme final : public QObject {
	Q_OBJECT

private slots:
	void sheetIsFound();
	void everyTokenResolves();
	void schemesAreOffered();
	void jsonLayersOntoItsBase();
	void jsonKeepsItsBaseName();
	void alphaSurvivesARoundTrip();
	void badValuesAreReportedNotApplied();
};

void TestTheme::sheetIsFound()
{
	/*
	 * The resource, in other words. A static library's resources are dropped by the
	 * linker unless something registers them, so this failing means the interface
	 * would have come up as unstyled Fusion widgets.
	 */
	const QString sheet = Qdc::styleSheet(Qdc::defaultTheme());

	QVERIFY(!sheet.isEmpty());
	QVERIFY(sheet.contains(QStringLiteral("QWidget#navigationRail")));
}

void TestTheme::everyTokenResolves()
{
	/*
	 * A name in the sheet that no theme sets is dropped with a warning, and a warning
	 * nobody is watching for is a warning on every start. This is the watching.
	 */
	QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));

	const QString sheet = Qdc::styleSheet(Qdc::defaultTheme());

	/*
	 * A name the theme does not set is left out of the sheet, and Qt answers the
	 * broken declaration by discarding the whole rule silently. So the check is that
	 * the substitution left nothing of its own syntax behind.
	 */
	QVERIFY(!sheet.contains(QRegularExpression(QStringLiteral(R"(%\(\w+\))"))));

	/* And that the substitution happened at all rather than the sheet being empty. */
	QVERIFY(sheet.contains(Qdc::defaultTheme().frame.name(QColor::HexRgb)));
}

void TestTheme::schemesAreOffered()
{
	const QList<Qdc::Theme> themes = Qdc::availableThemes();

	QVERIFY(!themes.isEmpty());
	QCOMPARE(themes.constFirst().name, Qdc::defaultTheme().name);

	/*
	 * By name rather than by count: this reads whatever is in ~/.quickdc/themes as
	 * well, and a developer with files of their own there is not a failing test.
	 */
	const auto named = [&themes](const QString& name) {
		return std::any_of(themes.begin(), themes.end(),
			[&name](const Qdc::Theme& theme) { return theme.name == name; });
	};

	QVERIFY(named(QStringLiteral("Indigo")));

	/* And an unknown name is the default rather than an empty theme. */
	QCOMPARE(Qdc::themeByName(QStringLiteral("no such theme")).name,
	         Qdc::defaultTheme().name);
}

void TestTheme::jsonLayersOntoItsBase()
{
	const QJsonObject object{
		{QStringLiteral("name"), QStringLiteral("Test")},
		{QStringLiteral("accent"), QStringLiteral("#ff0000")},
		{QStringLiteral("radiusLarge"), 20}
	};

	QStringList problems;
	const Qdc::Theme theme = Qdc::themeFromJson(object, Qdc::defaultTheme(), &problems);

	QVERIFY2(problems.isEmpty(), qUtf8Printable(problems.join(QStringLiteral("; "))));

	QCOMPARE(theme.name, QStringLiteral("Test"));
	QCOMPARE(theme.accent, QColor(0xff, 0x00, 0x00));
	QCOMPARE(theme.radiusLarge, 20);

	/* Everything it did not mention is what it was layered onto. */
	QCOMPARE(theme.frame, Qdc::defaultTheme().frame);
	QCOMPARE(theme.radiusSmall, Qdc::defaultTheme().radiusSmall);
}

void TestTheme::jsonKeepsItsBaseName()
{
	/*
	 * A file that does not name itself is a set of changes to what it extends, and so
	 * replaces it in the list instead of appearing beside it under the same name.
	 */
	const QJsonObject object{{QStringLiteral("star"), QStringLiteral("#123456")}};

	const Qdc::Theme theme = Qdc::themeFromJson(object, Qdc::defaultTheme());

	QCOMPARE(theme.name, Qdc::defaultTheme().name);
	QCOMPARE(theme.star, QColor(0x12, 0x34, 0x56));
}

void TestTheme::alphaSurvivesARoundTrip()
{
	/*
	 * The rgba() form is what the stylesheet prints for a colour that is not opaque,
	 * so it has to be a form a theme file can be written in.
	 */
	const QJsonObject object{
		{QStringLiteral("railHover"), QStringLiteral("rgba(255,255,255,0.5)")},
		{QStringLiteral("sidebarHover"), QStringLiteral("#40ffffff")}
	};

	QStringList problems;
	const Qdc::Theme theme = Qdc::themeFromJson(object, Qdc::defaultTheme(), &problems);

	QVERIFY2(problems.isEmpty(), qUtf8Printable(problems.join(QStringLiteral("; "))));

	QCOMPARE(theme.railHover.red(), 255);
	QCOMPARE(theme.railHover.alpha(), 128);

	QCOMPARE(theme.sidebarHover.alpha(), 0x40);
}

void TestTheme::badValuesAreReportedNotApplied()
{
	const QJsonObject object{
		{QStringLiteral("accent"), QStringLiteral("not a colour")},
		{QStringLiteral("radiusSmall"), QStringLiteral("four")},
		{QStringLiteral("acccent"), QStringLiteral("#ff0000")}
	};

	QStringList problems;
	const Qdc::Theme theme = Qdc::themeFromJson(object, Qdc::defaultTheme(), &problems);

	QCOMPARE(problems.size(), 3);

	/* A typo costs one wrong colour at worst, and here it costs nothing. */
	QCOMPARE(theme.accent, Qdc::defaultTheme().accent);
	QCOMPARE(theme.radiusSmall, Qdc::defaultTheme().radiusSmall);
}

QTEST_MAIN(TestTheme)

#include "tst_theme.moc"

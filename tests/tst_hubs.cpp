/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/configbridge.h>
#include <core/corethread.h>
#include <core/favourites.h>
#include <core/filelistbridge.h>
#include <core/transferbridge.h>
#include <core/hubbridge.h>
#include <gui/chatfeed.h>
#include <gui/chatmodel.h>
#include <gui/chatpane.h>
#include <gui/composer.h>
#include <gui/filelistview.h>
#include <gui/hubpane.h>
#include <gui/hubsview.h>
#include <gui/richtext.h>
#include <gui/theme.h>
#include <gui/usermodel.h>

#include <QApplication>
#include <QDir>
#include <QStyleOptionViewItem>
#include <QImage>
#include <QPainter>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextList>
#include <QToolButton>
#include <QFileDialog>
#include <QMenu>
#include <QTimer>
#include <QFileDialog>
#include <QMenu>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTimer>
#include <QTemporaryDir>
#include <QTest>

/**
 * Hubs and messaging: the models behind the feed and the user list, the
 * favourites store, the file list tree, and the routing that decides which
 * conversation a line belongs to.
 *
 * Runs against a real core from a temporary directory. Nothing connects to a hub
 * - the routing is driven by emitting on a bridge, which is what the core would
 * do, and is the only way to test it without a hub to talk to.
 */
class TestHubs : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();

	void chatModelKeepsRowsInOrder();
	void chatModelDropsOldestPastCapacity();

	void userModelOrdersOperatorsFirst();
	void userModelReplacesOnUpdate();
	void userModelForgetsUsersWhoLeave();
	void userModelListsAdcUsersOnce();
	void chatFeedGivesMultiLineMessagesMoreHeight();
	void chatFeedGivesAMarkdownMessageRoomForItsBlocks();
	void chatFeedForgetsItsLayoutsWhenRowsMove();
	void chatFeedClicksALinkAndIgnoresADrag();
	void chatFeedDrawsAPictureItHas();
	void chatModelReadsARichMessageAsProse();

	void favouritesRoundTripThroughTheStore();
	void favouritesShrinkWithoutLeavingATail();

	void urlNormalisationFillsInSchemeAndPort();
	void noHubCarriesRichTextUntilItAnnouncesTheExtension();

	void composerFlagsOnlyWhatItActuallyFormatted();
	void composerSerializesEachConstructToMarkdown();
	void composerWritesListsAsMarkdown();
	void emojiPickerGivesEveryEmojiRoomToDraw();
	void anAttachedFileIsSentAsALinkToItsHash();
	void aPictureIsSentAsMediaRatherThanAsAFile();
	void composerFlattensFormattingWhereItCannotBeSent();

	void filelistModelExposesTheTree();
	void filelistModelFilterKeepsAncestors();

	void openingTheSameHubTwiceReusesThePane();
	void privateMessagesOpenAConversation();
	void unreadCountsReachTheRailBadge();
	void aRichTextFlagReachesThePane();
	void hubPasswordIsAskedForOnce();
	void loopback();
	void nothingCanBeSentUntilTheHubHasLoggedUsIn();
	void formattingIsOnlyOfferedWhereItCanBeSent();
	void aClickedHubLinkOpensTheHubItself();
	void picturesAreFetchedOnlyWhereThatWasAskedFor();
	void theAttachButtonAsksForAFile();
	void everyControlLooksDisabledWhenItIs();
	void anEchoOfOurOwnPrivateMessageOpensTheirConversation();

private:
	Qdc::UserInfo user(const QString& nick, bool op = false) const;

	/** The formatting button whose accessible name is 'name', or nullptr. */
	static QToolButton* formatButton(Qdc::Composer* composer, const QString& name);

	/** Type into the composer's box and press Return, as a person would. */
	static void writeAndSend(Qdc::Composer* composer, const QString& text);

	QTemporaryDir m_dir;
	Qdc::CoreThread* m_thread = nullptr;
	Qdc::FilelistBridge* m_filelists = nullptr;
};

Qdc::UserInfo TestHubs::user(const QString& nick, bool op) const
{
	Qdc::UserInfo info;
	info.id = nick;
	info.nick = nick;
	info.sourceId = nick;
	info.op = op;
	return info;
}

QToolButton* TestHubs::formatButton(Qdc::Composer* composer, const QString& name)
{
	const QList<QToolButton*> buttons = composer->findChildren<QToolButton*>();

	for (QToolButton* button : buttons)
		if (button->accessibleName() == name) return button;

	return nullptr;
}

void TestHubs::writeAndSend(Qdc::Composer* composer, const QString& text)
{
	QTextEdit* edit = composer->findChild<QTextEdit*>();
	QVERIFY(edit);

	if (!text.isEmpty()) QTest::keyClicks(edit, text);
	QTest::keyClick(edit, Qt::Key_Return);
}

void TestHubs::initTestCase()
{
	QVERIFY2(m_dir.isValid(), "could not create a temporary directory");

	const QDir dir(m_dir.path());
	m_thread = new Qdc::CoreThread(this);
	m_thread->setPreferenceFiles(dir.filePath(QStringLiteral("quickdc.conf")),
	                             dir.filePath(QStringLiteral("session.conf")),
	                             dir.filePath(QStringLiteral("queue.xml")));

	QVERIFY2(m_thread->startAndWait(),
	         qPrintable(QStringLiteral("the core did not start: ")
	                    + m_thread->failureReason()));

	m_filelists = new Qdc::FilelistBridge(m_thread, this);
}

void TestHubs::cleanupTestCase()
{
	if (m_thread) m_thread->shutdown();
}

void TestHubs::chatModelKeepsRowsInOrder()
{
	Qdc::ChatModel model;
	QCOMPARE(model.rowCount(), 0);

	for (int n = 0; n < 5; ++n)
	{
		Qdc::ChatMessage message;
		message.from = user(QStringLiteral("alice"));
		message.text = QString::number(n);
		model.append(message);
	}

	QCOMPARE(model.rowCount(), 5);

	for (int n = 0; n < 5; ++n)
	{
		const Qdc::ChatMessage message =
			model.data(model.index(n), Qdc::ChatModel::MessageRole)
				.value<Qdc::ChatMessage>();

		QCOMPARE(message.text, QString::number(n));
	}

	model.clear();
	QCOMPARE(model.rowCount(), 0);
}

void TestHubs::chatModelDropsOldestPastCapacity()
{
	Qdc::ChatModel model;
	model.setCapacity(3);

	for (int n = 0; n < 10; ++n)
	{
		Qdc::ChatMessage message;
		message.text = QString::number(n);
		model.append(message);
	}

	QCOMPARE(model.rowCount(), 3);

	/* The three most recent, in order: the oldest go, not the newest. */
	QCOMPARE(model.at(0).text, QStringLiteral("7"));
	QCOMPARE(model.at(2).text, QStringLiteral("9"));

	/* Lowering the capacity trims what is already there. */
	model.setCapacity(1);
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(model.at(0).text, QStringLiteral("9"));
}

void TestHubs::userModelOrdersOperatorsFirst()
{
	Qdc::UserModel model;
	QSignalSpy counts(&model, &Qdc::UserModel::countChanged);

	/* Inserted in an order that is wrong in both respects. */
	model.upsert(user(QStringLiteral("zoe")));
	model.upsert(user(QStringLiteral("Bob")));
	model.upsert(user(QStringLiteral("opal"), true));
	model.upsert(user(QStringLiteral("alice")));
	model.upsert(user(QStringLiteral("Admin"), true));

	QCOMPARE(model.count(), 5);
	QCOMPARE(counts.count(), 5);

	/* Operators first, then everyone else, each group case-insensitively. */
	QCOMPARE(model.nicknames(), QStringList({
		QStringLiteral("Admin"), QStringLiteral("opal"),
		QStringLiteral("alice"), QStringLiteral("Bob"), QStringLiteral("zoe")
	}));
}

void TestHubs::userModelReplacesOnUpdate()
{
	Qdc::UserModel model;

	model.noteNickname(QStringLiteral("alice"));
	model.upsert(user(QStringLiteral("bob")));
	QCOMPARE(model.count(), 2);

	/* A join for someone already listed must not add a second row. */
	model.noteNickname(QStringLiteral("alice"));
	QCOMPARE(model.count(), 2);

	/* Becoming an operator moves the row rather than duplicating it. */
	model.upsert(user(QStringLiteral("bob"), true));
	QCOMPARE(model.count(), 2);
	QCOMPARE(model.nicknames().first(), QStringLiteral("bob"));

	/* A detail that does not affect the order updates in place. */
	Qdc::UserInfo detailed = user(QStringLiteral("bob"), true);
	detailed.description = QStringLiteral("somewhere");
	model.upsert(detailed);

	QCOMPARE(model.count(), 2);
	QCOMPARE(model.at(0).description, QStringLiteral("somewhere"));
}

void TestHubs::userModelForgetsUsersWhoLeave()
{
	Qdc::UserModel model;

	model.upsert(user(QStringLiteral("alice")));
	model.upsert(user(QStringLiteral("bob")));
	model.upsert(user(QStringLiteral("carol")));

	model.remove(QStringLiteral("bob"));
	QCOMPARE(model.count(), 2);
	QCOMPARE(model.nicknames(), QStringList({QStringLiteral("alice"),
	                                         QStringLiteral("carol")}));

	/* Removing the same user twice, or one who was never there, is harmless. */
	model.remove(QStringLiteral("bob"));
	model.remove(QStringLiteral("nobody"));
	QCOMPARE(model.count(), 2);

	/*
	 * The rows shift when one is removed, so the id index has to be rebuilt -
	 * without that, removing 'carol' after 'bob' would take out the wrong row.
	 */
	model.remove(QStringLiteral("carol"));
	QCOMPARE(model.nicknames(), QStringList({QStringLiteral("alice")}));

	model.clear();
	QCOMPARE(model.count(), 0);
}

void TestHubs::userModelListsAdcUsersOnce()
{
	Qdc::UserModel model;

	/*
	 * What the core actually does for a new user: UserManager::info() raises a
	 * join carrying the nickname and an info carrying the record. On ADC the
	 * record's id is a session id, not the nickname.
	 */
	model.noteNickname(QStringLiteral("alice"));
	QCOMPARE(model.count(), 1);

	Qdc::UserInfo real;
	real.id = QStringLiteral("AAAB");
	real.nick = QStringLiteral("alice");
	real.sourceId = QStringLiteral("ACSTOWQXZASA");

	model.upsert(real);

	/* One row, and it is the real record rather than the bare nickname. */
	QCOMPARE(model.count(), 1);
	QCOMPARE(model.at(0).id, QStringLiteral("AAAB"));
	QCOMPARE(model.at(0).nick, QStringLiteral("alice"));
	QCOMPARE(model.at(0).sourceId, QStringLiteral("ACSTOWQXZASA"));

	/* Leaving is reported by session id, which is what the surviving row uses. */
	model.remove(QStringLiteral("AAAB"));
	QCOMPARE(model.count(), 0);

	/* On NMDC the id is the nickname, so the placeholder is simply filled in. */
	model.noteNickname(QStringLiteral("bob"));
	Qdc::UserInfo nmdc = user(QStringLiteral("bob"));
	nmdc.description = QStringLiteral("somewhere");
	model.upsert(nmdc);

	QCOMPARE(model.count(), 1);
	QCOMPARE(model.at(0).description, QStringLiteral("somewhere"));

	/*
	 * And a later record for someone whose placeholder is long gone must not
	 * evict an unrelated row that happens to be named like a session id.
	 */
	Qdc::UserInfo other;
	other.id = QStringLiteral("bob");
	other.nick = QStringLiteral("bob");
	model.upsert(other);
	QCOMPARE(model.count(), 1);
}

void TestHubs::chatFeedGivesMultiLineMessagesMoreHeight()
{
	Qdc::ChatModel model;

	Qdc::ChatMessage single;
	single.from = user(QStringLiteral("alice"));
	single.text = QStringLiteral("one line");
	single.received = QDateTime::currentDateTime();
	model.append(single);

	Qdc::ChatMessage several = single;
	several.text = QStringLiteral("first\nsecond\nthird");
	model.append(several);

	Qdc::ChatMessage crlf = single;
	crlf.text = QStringLiteral("first\r\nsecond\r\nthird");
	model.append(crlf);

	/*
	 * The delegate is measured directly rather than through the view. Asking the
	 * view means asking before its viewport has been laid out, and the delegate
	 * answers a single line's height for any row when it is given no width - so
	 * the test would pass whatever the layout did.
	 */
	Qdc::ChatDelegate delegate;

	QStyleOptionViewItem option;
	option.rect = QRect(0, 0, 420, 0);
	option.font = QApplication::font();
	option.palette = QApplication::palette();

	const int one = delegate.sizeHint(option, model.index(0)).height();
	const int three = delegate.sizeHint(option, model.index(1)).height();
	const int viaCrlf = delegate.sizeHint(option, model.index(2)).height();

	QVERIFY(one > 0);

	/*
	 * A newline is whitespace in HTML: before the fix all three rows were one line
	 * tall and the message read "first second third".
	 */
	QVERIFY2(three > 2 * one,
	         qPrintable(QStringLiteral("three lines measured %1, one line %2")
	                    .arg(three).arg(one)));

	/* Carriage returns off the wire count as line breaks too. */
	QCOMPARE(viaCrlf, three);
}

void TestHubs::chatFeedGivesAMarkdownMessageRoomForItsBlocks()
{
	Qdc::ChatModel model;

	Qdc::ChatMessage plain;
	plain.from = user(QStringLiteral("alice"));
	plain.received = QDateTime::currentDateTime();
	plain.text = QStringLiteral("try `make` first");
	model.append(plain);

	/* The same text, but flagged - so the marked-up runs are read as markup. */
	Qdc::ChatMessage rich = plain;
	rich.richText = true;
	model.append(rich);

	Qdc::ChatMessage block = plain;
	block.richText = true;
	block.text = QStringLiteral("run this:\n```\ncmake --build .\n```");
	model.append(block);

	/* The same two lines with no markup at all, as the yardstick for the row above. */
	Qdc::ChatMessage twoLines = plain;
	twoLines.text = QStringLiteral("run this:\ncmake --build .");
	model.append(twoLines);

	Qdc::ChatDelegate delegate;

	QStyleOptionViewItem option;
	option.rect = QRect(0, 0, 420, 0);
	option.font = QApplication::font();
	option.palette = QApplication::palette();

	const int asText = delegate.sizeHint(option, model.index(0)).height();
	const int asMarkup = delegate.sizeHint(option, model.index(1)).height();
	const int withBlock = delegate.sizeHint(option, model.index(2)).height();
	const int twoRows = delegate.sizeHint(option, model.index(3)).height();

	/* One line either way; the backticks come off rather than wrapping to two. */
	QCOMPARE(asMarkup, asText);

	/*
	 * The fence is a block of its own rather than two more lines of prose, so the row is
	 * at least as tall as the same two lines unformatted, and taller than one.
	 */
	QVERIFY2(withBlock >= twoRows && withBlock > asText,
	         qPrintable(QStringLiteral("code block measured %1, two plain lines %2, "
	                                   "one line %3")
	                    .arg(withBlock).arg(twoRows).arg(asText)));
}

void TestHubs::chatFeedForgetsItsLayoutsWhenRowsMove()
{
	Qdc::ChatFeed feed;
	Qdc::ChatModel model;

	feed.setModel(&model);
	feed.resize(420, 300);

	Qdc::ChatMessage message;
	message.from = user(QStringLiteral("alice"));
	message.received = QDateTime::currentDateTime();

	/*
	 * Row 0 is the tall one. Trimming the backlog removes it and shifts every row down
	 * by one, so a cache keyed by row number would go on serving its height to whatever
	 * row 0 became.
	 */
	message.text = QStringLiteral("first\nsecond\nthird\nfourth");
	model.append(message);

	message.text = QStringLiteral("short");
	model.append(message);

	Qdc::ChatDelegate* delegate = qobject_cast<Qdc::ChatDelegate*>(feed.itemDelegate());
	QVERIFY(delegate);

	QStyleOptionViewItem option;
	option.rect = QRect(0, 0, 420, 0);
	option.font = QApplication::font();
	option.palette = QApplication::palette();

	const int tall = delegate->sizeHint(option, model.index(0)).height();
	const int shortRow = delegate->sizeHint(option, model.index(1)).height();
	QVERIFY(tall > shortRow);

	model.setCapacity(1);
	QCOMPARE(model.rowCount(), 1);

	/* What was row 1 is row 0 now, and must measure as the short line it is. */
	QCOMPARE(delegate->sizeHint(option, model.index(0)).height(), shortRow);
}

void TestHubs::chatFeedClicksALinkAndIgnoresADrag()
{
	Qdc::ChatFeed feed;
	Qdc::ChatModel model;

	feed.setModel(&model);
	feed.resize(600, 300);
	feed.show();

	Qdc::ChatMessage message;
	message.from = user(QStringLiteral("alice"));
	message.received = QDateTime::currentDateTime();
	message.richText = true;
	message.text = QStringLiteral("[the hub](adc://hub.example.org:412) is back");
	model.append(message);

	QVERIFY(QTest::qWaitForWindowExposed(&feed));

	const QRect row = feed.visualRect(model.index(0));
	QVERIFY(row.isValid());

	/*
	 * Swept across the row rather than aimed at one pixel: where the link falls depends
	 * on the width of the timestamp and the name in whatever font this machine uses.
	 */
	QPoint onLink;
	for (int x = row.left(); x < row.right(); ++x)
	{
		const QPoint point(x, row.center().y());
		if (feed.anchorAt(point) == QStringLiteral("adc://hub.example.org:412"))
		{
			onLink = point;
			break;
		}
	}

	QVERIFY2(!onLink.isNull(), "no link was found anywhere along the row");

	QSignalSpy followed(&feed, &Qdc::ChatFeed::linkActivated);

	QTest::mousePress(feed.viewport(), Qt::LeftButton, Qt::NoModifier, onLink);
	QTest::mouseRelease(feed.viewport(), Qt::LeftButton, Qt::NoModifier, onLink);

	QCOMPARE(followed.count(), 1);
	QCOMPARE(followed.at(0).at(0).toUrl(),
	         QUrl(QStringLiteral("adc://hub.example.org:412")));

	/* Pressed on the link but released off it: a selection, not a click. */
	QTest::mousePress(feed.viewport(), Qt::LeftButton, Qt::NoModifier, onLink);
	QTest::mouseRelease(feed.viewport(), Qt::LeftButton, Qt::NoModifier,
	                    QPoint(row.right() - 1, row.center().y()));

	QCOMPARE(followed.count(), 1);
}

void TestHubs::chatFeedDrawsAPictureItHas()
{
	/* A real picture on disk, because the feed reads it to draw it. */
	const QString path = m_dir.path() + QStringLiteral("/wide.png");

	QImage made(800, 200, QImage::Format_RGB32);
	made.fill(Qt::darkCyan);
	QVERIFY(made.save(path));

	const QString tth = QStringLiteral("VN6TQGXQ7HYQ5ARWZ2LNFVQ7YQKGXQ7HYQ5ARWZ");

	Qdc::RichText::Attachment picture;
	picture.tth = tth;
	picture.name = QStringLiteral("wide.png");
	picture.size = 1234;

	Qdc::ChatModel model;

	Qdc::ChatMessage message;
	message.from = user(QStringLiteral("alice"));
	message.received = QDateTime::currentDateTime();
	message.richText = true;
	message.text = QStringLiteral("![a picture](%1)")
		.arg(Qdc::RichText::writeMagnet(picture));
	model.append(message);

	Qdc::ChatFeed feed;
	feed.setModel(&model);
	feed.resize(420, 400);

	Qdc::ChatDelegate* delegate = qobject_cast<Qdc::ChatDelegate*>(feed.itemDelegate());
	QVERIFY(delegate);

	QStyleOptionViewItem option;
	option.rect = QRect(0, 0, 420, 0);
	option.font = QApplication::font();
	option.palette = QApplication::palette();

	const int asChip = delegate->sizeHint(option, model.index(0)).height();

	/* Now say the file is here. The row has to grow to make room for it. */
	feed.setMediaResolver([path, tth](const QString& asked) {
		Qdc::RichText::LocalMedia here;
		if (asked != tth) return here;

		here.path = path;
		here.size = QSize(800, 200);
		return here;
	});

	const int drawn = delegate->sizeHint(option, model.index(0)).height();

	QVERIFY2(drawn > asChip + 20,
	         qPrintable(QStringLiteral("a chip measured %1, the picture %2")
	                    .arg(asChip).arg(drawn)));

	/*
	 * And it is scaled into the text column rather than drawn at its own 800 pixels,
	 * which would push the message off the side.
	 */
	QVERIFY2(drawn < 420, qPrintable(QStringLiteral("the row is %1 tall").arg(drawn)));
}

void TestHubs::chatModelReadsARichMessageAsProse()
{
	Qdc::ChatModel model;

	Qdc::ChatMessage message;
	message.from = user(QStringLiteral("alice"));
	message.received = QDateTime::currentDateTime();
	message.richText = true;
	message.text = QStringLiteral("**really** try `make` [here](https://example.org)");
	model.append(message);

	/* What a screen reader says and what a copy puts on the clipboard. */
	QCOMPARE(model.index(0).data(Qt::DisplayRole).toString(),
	         QStringLiteral("alice: really try make here"));

	/* Unflagged text is nobody's markup, so it is left exactly as it arrived. */
	Qdc::ChatMessage literal = message;
	literal.richText = false;
	model.append(literal);

	QCOMPARE(model.index(1).data(Qt::DisplayRole).toString(),
	         QStringLiteral("alice: **really** try `make` [here](https://example.org)"));
}

void TestHubs::favouritesRoundTripThroughTheStore()
{
	Qdc::ConfigBridge config(m_thread);

	const QString hub = QStringLiteral("adcs://hub.example.org:412");
	const QString other = QStringLiteral("dchub://other.example.org:411");

	{
		Qdc::Favourites favourites(&config);
		QVERIFY(favourites.urls().isEmpty());

		QSignalSpy changes(&favourites, &Qdc::Favourites::changed);

		QVERIFY(favourites.add(hub));
		QVERIFY(favourites.contains(hub));
		QCOMPARE(changes.count(), 1);

		/* Adding one that is already there is not a change. */
		QVERIFY(!favourites.add(hub));
		QCOMPARE(changes.count(), 1);

		QVERIFY(favourites.add(other));
		QCOMPARE(favourites.urls().size(), 2);

		QVERIFY(!favourites.toggle(hub));
		QVERIFY(!favourites.contains(hub));
		QVERIFY(favourites.toggle(hub));
		QVERIFY(favourites.contains(hub));
	}

	/* A second store reads back what the first wrote. */
	Qdc::Favourites reloaded(&config);
	QVERIFY(reloaded.contains(hub));
	QVERIFY(reloaded.contains(other));
	QCOMPARE(reloaded.urls().size(), 2);
}

void TestHubs::favouritesShrinkWithoutLeavingATail()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::Favourites favourites(&config);

	/* Start from whatever the previous test left behind. */
	for (const QString& url : favourites.urls()) favourites.remove(url);
	QVERIFY(favourites.urls().isEmpty());

	for (int n = 0; n < 4; ++n)
		favourites.add(QStringLiteral("dchub://h%1.example.org:411").arg(n));

	QCOMPARE(favourites.urls().size(), 4);

	favourites.remove(QStringLiteral("dchub://h0.example.org:411"));
	favourites.remove(QStringLiteral("dchub://h1.example.org:411"));

	/*
	 * The group is numbered, so a shorter list has to clear the keys the longer
	 * one used. Without that, reloading finds the old entries still there.
	 */
	Qdc::Favourites reloaded(&config);
	QCOMPARE(reloaded.urls().size(), 2);
	QVERIFY(!reloaded.contains(QStringLiteral("dchub://h0.example.org:411")));
	QVERIFY(reloaded.contains(QStringLiteral("dchub://h2.example.org:411")));
}

void TestHubs::noHubCarriesRichTextUntilItAnnouncesTheExtension()
{
	/*
	 * Negotiated, not assumed. A client offers the RTF0 extension in its SUP and only a
	 * hub that answers with the same has agreed to carry a message flagged as markdown -
	 * which is how a hub operator decides whether it happens here at all. Nothing is
	 * known about a hub that has not been spoken to yet, so the answer starts out no.
	 */
	Qdc::HubBridge adc(m_thread, QStringLiteral("adc://192.0.2.1:412"), this);
	QVERIFY(!adc.supportsRichText());

	QSignalSpy agreed(&adc, &Qdc::HubBridge::richTextSupportChanged);

	emit adc.richTextSupportChanged(true);
	QCOMPARE(agreed.count(), 1);

	/*
	 * NMDC never gets the chance: there is nowhere in the protocol to put the flag, so
	 * the extension cannot be offered and the answer stays no for the whole session.
	 */
	Qdc::HubBridge nmdc(m_thread, QStringLiteral("dchub://192.0.2.1:411"), this);
	QVERIFY(!nmdc.supportsRichText());

	Qdc::HubBridge shorthand(m_thread, QStringLiteral("192.0.2.1"), this);
	QVERIFY(!shorthand.supportsRichText());
}

void TestHubs::composerFlagsOnlyWhatItActuallyFormatted()
{
	Qdc::Composer composer;
	QSignalSpy sent(&composer, &Qdc::Composer::sendRequested);

	/*
	 * Asterisks somebody typed are theirs. Nothing was formatted, so no flag is set and
	 * the text goes out exactly as written - which is what stops the far end reading it
	 * as emphasis nobody asked for.
	 */
	writeAndSend(&composer, QStringLiteral("look at *this*"));

	QCOMPARE(sent.count(), 1);
	QCOMPARE(sent.at(0).at(0).toString(), QStringLiteral("look at *this*"));
	QCOMPARE(sent.at(0).at(1).toBool(), false);

	QToolButton* bold = formatButton(&composer, QStringLiteral("Bold"));
	QVERIFY(bold);

	QTextEdit* edit = composer.findChild<QTextEdit*>();
	QVERIFY(edit);

	QTest::keyClicks(edit, QStringLiteral("really"));
	edit->selectAll();
	bold->click();

	writeAndSend(&composer, QString());

	QCOMPARE(sent.count(), 2);
	QCOMPARE(sent.at(1).at(0).toString(), QStringLiteral("**really**"));
	QCOMPARE(sent.at(1).at(1).toBool(), true);

	/* Both at once on one word, which is one run carrying both formats. */
	QToolButton* italic = formatButton(&composer, QStringLiteral("Italic"));
	QVERIFY(italic);

	QTest::keyClicks(edit, QStringLiteral("very"));
	edit->selectAll();
	bold->click();
	edit->selectAll();
	italic->click();

	QVERIFY(bold->isChecked());
	QVERIFY(italic->isChecked());

	writeAndSend(&composer, QString());

	QCOMPARE(sent.count(), 3);
	QCOMPARE(sent.at(2).at(0).toString(), QStringLiteral("***very***"));
}

void TestHubs::composerSerializesEachConstructToMarkdown()
{
	Qdc::Composer composer;
	QSignalSpy sent(&composer, &Qdc::Composer::sendRequested);

	QTextEdit* edit = composer.findChild<QTextEdit*>();
	QVERIFY(edit);

	/* Italic and inline code, each applied to one word of the same line. */
	QTest::keyClicks(edit, QStringLiteral("run make first"));

	QTextCursor cursor = edit->textCursor();
	cursor.setPosition(4);
	cursor.setPosition(8, QTextCursor::KeepAnchor);
	edit->setTextCursor(cursor);

	QToolButton* code = formatButton(&composer, QStringLiteral("Code"));
	QVERIFY(code);
	code->click();

	cursor.setPosition(9);
	cursor.setPosition(14, QTextCursor::KeepAnchor);
	edit->setTextCursor(cursor);

	QToolButton* italic = formatButton(&composer, QStringLiteral("Italic"));
	QVERIFY(italic);
	italic->click();

	writeAndSend(&composer, QString());

	QCOMPARE(sent.count(), 1);
	QCOMPARE(sent.at(0).at(0).toString(), QStringLiteral("run `make` *first*"));
	QCOMPARE(sent.at(0).at(1).toBool(), true);

	/* A whole block turned into a quote, and then into a code block. */
	QTest::keyClicks(edit, QStringLiteral("as you were"));

	QToolButton* quote = formatButton(&composer, QStringLiteral("Quote"));
	QVERIFY(quote);
	quote->click();

	writeAndSend(&composer, QString());

	QCOMPARE(sent.count(), 2);
	QCOMPARE(sent.at(1).at(0).toString(), QStringLiteral("> as you were"));

	QTest::keyClicks(edit, QStringLiteral("cmake --build ."));

	QToolButton* block = formatButton(&composer, QStringLiteral("Code block"));
	QVERIFY(block);
	block->click();

	writeAndSend(&composer, QString());

	QCOMPARE(sent.count(), 3);
	QCOMPARE(sent.at(2).at(0).toString(),
	         QStringLiteral("```\ncmake --build .\n```"));
}

void TestHubs::composerWritesListsAsMarkdown()
{
	Qdc::Composer composer;
	QSignalSpy sent(&composer, &Qdc::Composer::sendRequested);

	QTextEdit* edit = composer.findChild<QTextEdit*>();
	QVERIFY(edit);

	QToolButton* bullets = formatButton(&composer, QStringLiteral("Bulleted list"));
	QToolButton* numbers = formatButton(&composer, QStringLiteral("Numbered list"));
	QVERIFY(bullets);
	QVERIFY(numbers);

	/*
	 * Typed the way a person would: turn the list on, then write the items. Shift+Return
	 * is a new block in the composer, because plain Return sends.
	 */
	bullets->click();
	QTest::keyClicks(edit, QStringLiteral("bread"));
	QTest::keyClick(edit, Qt::Key_Return, Qt::ShiftModifier);
	QTest::keyClicks(edit, QStringLiteral("milk"));

	/* The items are a real list in the box, so they look like items while typing. */
	QVERIFY(edit->textCursor().block().textList());
	QVERIFY(bullets->isChecked());

	writeAndSend(&composer, QString());

	QCOMPARE(sent.count(), 1);
	QCOMPARE(sent.at(0).at(0).toString(), QStringLiteral("- bread\n- milk"));
	QCOMPARE(sent.at(0).at(1).toBool(), true);

	numbers->click();
	QTest::keyClicks(edit, QStringLiteral("first"));
	QTest::keyClick(edit, Qt::Key_Return, Qt::ShiftModifier);
	QTest::keyClicks(edit, QStringLiteral("second"));

	writeAndSend(&composer, QString());

	QCOMPARE(sent.count(), 2);
	QCOMPARE(sent.at(1).at(0).toString(), QStringLiteral("1. first\n2. second"));

	/* Pressing the same control again takes the list off and leaves the words. */
	QTest::keyClicks(edit, QStringLiteral("plain again"));
	bullets->click();
	QVERIFY(bullets->isChecked());
	bullets->click();
	QVERIFY(!bullets->isChecked());

	writeAndSend(&composer, QString());

	QCOMPARE(sent.count(), 3);
	QCOMPARE(sent.at(2).at(0).toString(), QStringLiteral("plain again"));
	QCOMPARE(sent.at(2).at(1).toBool(), false);
}

void TestHubs::emojiPickerGivesEveryEmojiRoomToDraw()
{
	Qdc::Composer composer;
	composer.show();
	QVERIFY(QTest::qWaitForWindowExposed(&composer));

	QToolButton* opener = formatButton(&composer, QStringLiteral("Emoji"));

	QVERIFY(opener);

	int cells = 0;
	int cramped = 0;
	bool ragged = false;
	QSize uniform;

	/* The picker is a menu and runs its own event loop, so the looking happens inside. */
	QTimer::singleShot(100, [&] {
		QMenu* menu = nullptr;

		for (QWidget* top : QApplication::topLevelWidgets())
		{
			QMenu* candidate = qobject_cast<QMenu*>(top);
			if (candidate && candidate->isVisible()) menu = candidate;
		}

		if (!menu) return;

		for (const QToolButton* button : menu->findChildren<QToolButton*>())
		{
			++cells;

			if (!uniform.isValid()) uniform = button->size();
			else if (button->size() != uniform) ragged = true;

			/*
			 * A cell smaller than the style asked for is where an ellipsis comes from:
			 * QToolButton elides text that will not fit its content rect, and an emoji
			 * comes from a colour font whose glyphs are wider than the interface font's.
			 * Sizing the cells by hand is what used to leave most of the picker showing
			 * "..." on macOS while the few narrow ones drew fine.
			 */
			if (button->size().width() < button->sizeHint().width()
			    || button->size().height() < button->sizeHint().height())
			{
				++cramped;
			}
		}

		menu->close();
	});

	/* So the test cannot hang in the menu's loop if the picker never appeared. */
	QTimer::singleShot(4000, [] {
		for (QWidget* top : QApplication::topLevelWidgets())
			if (QMenu* menu = qobject_cast<QMenu*>(top)) menu->close();
	});

	opener->click();

	QVERIFY2(cells > 0, "the picker did not open");
	QCOMPARE(cramped, 0);
	QVERIFY2(!ragged, "the grid is not made of equal cells");
}

void TestHubs::anAttachedFileIsSentAsALinkToItsHash()
{
	Qdc::Composer composer;
	QSignalSpy sent(&composer, &Qdc::Composer::sendRequested);
	QSignalSpy asked(&composer, &Qdc::Composer::attachRequested);

	QToolButton* attach = formatButton(&composer, QStringLiteral("Attach a file"));
	QVERIFY(attach);

	/*
	 * The button only asks. Choosing the file and hashing it belong to whoever owns the
	 * connection, because neither is anything a text box can do.
	 */
	attach->click();
	QCOMPARE(asked.count(), 1);
	QCOMPARE(sent.count(), 0);

	Qdc::RichText::Attachment file;
	file.tth = QStringLiteral("VN6TQGXQ7HYQ5ARWZ2LNFVQ7YQKGXQ7HYQ5ARWZ");
	file.name = QStringLiteral("diagram.png");
	file.size = 524288;

	QTextEdit* edit = composer.findChild<QTextEdit*>();
	QVERIFY(edit);

	QTest::keyClicks(edit, QStringLiteral("have a look: "));
	composer.insertAttachment(file);
	QTest::keyClicks(edit, QStringLiteral("tell me what you think"));

	writeAndSend(&composer, QString());

	QCOMPARE(sent.count(), 1);

	/* The file is named by its hash, and the message says it is markdown. */
	QCOMPARE(sent.at(0).at(1).toBool(), true);
	QCOMPARE(sent.at(0).at(0).toString(),
	         QStringLiteral("have a look: [diagram.png](magnet:?xt=urn:tree:tiger:%1"
	                        "&dn=diagram.png&xl=524288) tell me what you think")
			.arg(file.tth));

	/* What arrives is read back as the same file. */
	const Qdc::RichText::Attachment read = Qdc::RichText::readMagnet(
		QUrl(Qdc::RichText::writeMagnet(file)));

	QCOMPARE(read.tth, file.tth);
	QCOMPARE(read.size, file.size);

	/* Attaching is formatting, so a hub that cannot carry the flag cannot carry a
	   file either. */
	composer.setRichTextEnabled(false);
	QVERIFY(!attach->isEnabled());
}

void TestHubs::aPictureIsSentAsMediaRatherThanAsAFile()
{
	Qdc::Composer composer;
	QSignalSpy sent(&composer, &Qdc::Composer::sendRequested);
	QSignalSpy asked(&composer, &Qdc::Composer::attachRequested);

	QToolButton* attach = formatButton(&composer, QStringLiteral("Attach a file"));
	QToolButton* picture = formatButton(&composer, QStringLiteral("Send a picture"));
	QVERIFY(attach);
	QVERIFY(picture);

	/* Both ask the same question; which one asked decides what the message says. */
	attach->click();
	QCOMPARE(asked.count(), 1);
	QVERIFY(!composer.wantsMedia());

	picture->click();
	QCOMPARE(asked.count(), 2);
	QVERIFY(composer.wantsMedia());

	Qdc::RichText::Attachment shown;
	shown.tth = QStringLiteral("VN6TQGXQ7HYQ5ARWZ2LNFVQ7YQKGXQ7HYQ5ARWZ");
	shown.name = QStringLiteral("diagram.png");
	shown.size = 524288;
	shown.inlineMedia = true;

	composer.insertAttachment(shown);
	writeAndSend(&composer, QString());

	QCOMPARE(sent.count(), 1);

	/* The '!' is the whole difference: shown where it was written, not offered. */
	QCOMPARE(sent.at(0).at(0).toString(),
	         QStringLiteral("![diagram.png](magnet:?xt=urn:tree:tiger:%1"
	                        "&dn=diagram.png&xl=524288)").arg(shown.tth));

	/* And it reads back as media rather than as an attachment. */
	const QString markdown = sent.at(0).at(0).toString();
	QVERIFY(markdown.startsWith(QStringLiteral("![")));

	Qdc::RichText::Attachment offered = shown;
	offered.inlineMedia = false;

	composer.insertAttachment(offered);
	writeAndSend(&composer, QString());

	QCOMPARE(sent.count(), 2);
	QVERIFY(sent.at(1).at(0).toString().startsWith(QStringLiteral("[diagram.png]")));
}

void TestHubs::composerFlattensFormattingWhereItCannotBeSent()
{
	Qdc::Composer composer;
	QSignalSpy sent(&composer, &Qdc::Composer::sendRequested);

	QTextEdit* edit = composer.findChild<QTextEdit*>();
	QVERIFY(edit);

	QToolButton* bold = formatButton(&composer, QStringLiteral("Bold"));
	QVERIFY(bold);
	QVERIFY(bold->isEnabled());

	QTest::keyClicks(edit, QStringLiteral("shouting"));
	edit->selectAll();
	bold->click();

	/*
	 * Moving the conversation to a hub that cannot carry the flag takes the formatting
	 * with it rather than leaving bold text that would be sent as markers.
	 */
	QVERIFY(Qdc::RichText::hasMarkup(*edit->document()));

	composer.setRichTextEnabled(false);
	QVERIFY(!composer.isRichTextEnabled());
	QVERIFY(!bold->isEnabled());
	QVERIFY(!bold->isChecked());

	/* The words stay; nothing about them is formatted any more. */
	QVERIFY(!Qdc::RichText::hasMarkup(*edit->document()));
	QCOMPARE(edit->toPlainText(), QStringLiteral("shouting"));

	writeAndSend(&composer, QString());

	QCOMPARE(sent.count(), 1);
	QCOMPARE(sent.at(0).at(0).toString(), QStringLiteral("shouting"));
	QCOMPARE(sent.at(0).at(1).toBool(), false);
}

void TestHubs::urlNormalisationFillsInSchemeAndPort()
{
	using Qdc::HubBridge;

	QCOMPARE(HubBridge::normalisedUrl(QStringLiteral("example.org")),
	         QStringLiteral("dchub://example.org:411"));

	/* An address that already says everything is left exactly as it is. */
	QCOMPARE(HubBridge::normalisedUrl(QStringLiteral("adc://example.org:7817")),
	         QStringLiteral("adc://example.org:7817"));

	QCOMPARE(HubBridge::normalisedUrl(QStringLiteral("adcs://example.org")),
	         QStringLiteral("adcs://example.org:411"));

	/*
	 * The port goes at the end of the authority, before the path - and an ADCS
	 * keyprint lives in the query, so the query has to survive.
	 */
	QCOMPARE(HubBridge::normalisedUrl(
			QStringLiteral("adcs://example.org/?kp=SHA256/ABC")),
		QStringLiteral("adcs://example.org:411/?kp=SHA256/ABC"));

	/* A colon inside brackets is part of an IPv6 literal, not a port. */
	QCOMPARE(HubBridge::normalisedUrl(QStringLiteral("dchub://[::1]")),
	         QStringLiteral("dchub://[::1]:411"));
	QCOMPARE(HubBridge::normalisedUrl(QStringLiteral("dchub://[::1]:412")),
	         QStringLiteral("dchub://[::1]:412"));

	QVERIFY(HubBridge::normalisedUrl(QString()).isEmpty());
	QVERIFY(HubBridge::normalisedUrl(QStringLiteral("   ")).isEmpty());
}

void TestHubs::filelistModelExposesTheTree()
{
	Qdc::RemoteDirInfo root;
	root.fileCount = 3;
	root.size = 600;

	Qdc::RemoteDirInfo music;
	music.name = QStringLiteral("Music");
	music.size = 500;

	Qdc::RemoteFileInfo song;
	song.name = QStringLiteral("song.flac");
	song.size = 500;
	song.tth = QStringLiteral("MWXFLX5NO4T54FMVTGZ6UXER2QKN6J2IGGL3W7A");
	music.files.push_back(song);

	Qdc::RemoteFileInfo readme;
	readme.name = QStringLiteral("readme.txt");
	readme.size = 100;
	root.files.push_back(readme);
	root.dirs.push_back(music);

	Qdc::FilelistModel model;
	model.setRoot(root);

	/* The root itself is not a row; its children are the top level. */
	QCOMPARE(model.rowCount(), 2);

	const QModelIndex dir = model.index(0, 0);
	QVERIFY(model.isDirectory(dir));
	QCOMPARE(model.data(dir, Qt::DisplayRole).toString(), QStringLiteral("Music"));
	QCOMPARE(model.rowCount(dir), 1);

	const QModelIndex file = model.index(0, 0, dir);
	QVERIFY(!model.isDirectory(file));
	QCOMPARE(model.data(file, Qt::DisplayRole).toString(),
	         QStringLiteral("song.flac"));
	QCOMPARE(model.sizeFor(file), 500u);
	QCOMPARE(model.tthFor(file), song.tth);

	/* A child's parent has to lead back to the same index, or the view breaks. */
	QCOMPARE(model.parent(file), dir);
	QVERIFY(!model.parent(dir).isValid());

	/* Directories sort before files, which is how the tree was built. */
	const QModelIndex loose = model.index(1, 0);
	QVERIFY(!model.isDirectory(loose));
	QCOMPARE(model.data(loose, Qt::DisplayRole).toString(),
	         QStringLiteral("readme.txt"));
}

void TestHubs::filelistModelFilterKeepsAncestors()
{
	Qdc::RemoteDirInfo root;

	Qdc::RemoteDirInfo outer;
	outer.name = QStringLiteral("outer");

	Qdc::RemoteDirInfo inner;
	inner.name = QStringLiteral("inner");

	Qdc::RemoteFileInfo wanted;
	wanted.name = QStringLiteral("needle.bin");
	inner.files.push_back(wanted);

	Qdc::RemoteFileInfo ignored;
	ignored.name = QStringLiteral("haystack.bin");
	inner.files.push_back(ignored);

	outer.dirs.push_back(inner);

	Qdc::RemoteDirInfo elsewhere;
	elsewhere.name = QStringLiteral("elsewhere");
	Qdc::RemoteFileInfo other;
	other.name = QStringLiteral("nothing.bin");
	elsewhere.files.push_back(other);

	root.dirs.push_back(outer);
	root.dirs.push_back(elsewhere);

	Qdc::FilelistModel model;
	model.setRoot(root);
	QCOMPARE(model.rowCount(), 2);

	model.setFilter(QStringLiteral("needle"));

	/*
	 * 'elsewhere' held nothing matching and is gone; 'outer' is kept only because
	 * the match is somewhere beneath it.
	 */
	QCOMPARE(model.rowCount(), 1);

	const QModelIndex kept = model.index(0, 0);
	QCOMPARE(model.data(kept, Qt::DisplayRole).toString(), QStringLiteral("outer"));

	const QModelIndex innerIndex = model.index(0, 0, kept);
	QCOMPARE(model.rowCount(innerIndex), 1);
	QCOMPARE(model.data(model.index(0, 0, innerIndex), Qt::DisplayRole).toString(),
	         QStringLiteral("needle.bin"));

	/* Matching a folder's own name keeps that folder whole. */
	model.setFilter(QStringLiteral("elsewhere"));
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(model.rowCount(model.index(0, 0)), 1);

	model.setFilter(QString());
	QCOMPARE(model.rowCount(), 2);
}

void TestHubs::openingTheSameHubTwiceReusesThePane()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);

	QVERIFY(!hubs.openHub(QString()));
	QCOMPARE(hubs.paneCount(), 0);

	QVERIFY(hubs.openHub(QStringLiteral("dchub://192.0.2.1:411")));
	QCOMPARE(hubs.openHubs().size(), 1);
	QCOMPARE(hubs.paneCount(), 1);

	/*
	 * The same hub by its shorthand. Recognised through the normalised form, so
	 * this must not open a second pane.
	 */
	QVERIFY(hubs.openHub(QStringLiteral("192.0.2.1")));
	QCOMPARE(hubs.openHubs().size(), 1);
	QCOMPARE(hubs.paneCount(), 1);

	QVERIFY(hubs.openHub(QStringLiteral("dchub://192.0.2.2:411")));
	QCOMPARE(hubs.openHubs().size(), 2);

	hubs.closeHub(QStringLiteral("dchub://192.0.2.1:411"));
	QCOMPARE(hubs.openHubs(), QStringList({QStringLiteral("dchub://192.0.2.2:411")}));

	/* Closing one that is not open does nothing. */
	hubs.closeHub(QStringLiteral("dchub://192.0.2.9:411"));
	QCOMPARE(hubs.openHubs().size(), 1);
}

void TestHubs::privateMessagesOpenAConversation()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);
	QVERIFY(hubs.openHub(QStringLiteral("dchub://192.0.2.1:411")));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);

	Qdc::HubBridge* bridge = pane->hub();
	QVERIFY(bridge);

	QCOMPARE(hubs.paneCount(), 1);

	/* A public line stays on the hub pane. */
	Qdc::ChatMessage open;
	open.from = user(QStringLiteral("alice"));
	open.text = QStringLiteral("hello everyone");
	emit bridge->chatReceived(open);

	QCOMPARE(hubs.paneCount(), 1);

	/*
	 * A private line from someone with no conversation open starts one. This is
	 * the only way an incoming private message can be seen at all.
	 */
	Qdc::ChatMessage direct;
	direct.from = user(QStringLiteral("bob"));
	direct.context = QStringLiteral("bob");
	direct.privateMessage = true;
	direct.text = QStringLiteral("are you there?");
	emit bridge->chatReceived(direct);

	QCOMPARE(hubs.paneCount(), 2);

	/* A second line from the same person reuses it. */
	emit bridge->chatReceived(direct);
	QCOMPARE(hubs.paneCount(), 2);

	/* Someone else gets their own. */
	Qdc::ChatMessage another = direct;
	another.from = user(QStringLiteral("carol"));
	another.context = QStringLiteral("carol");
	emit bridge->chatReceived(another);

	QCOMPARE(hubs.paneCount(), 3);
}

void TestHubs::aRichTextFlagReachesThePane()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);
	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.1:412")));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);

	Qdc::HubBridge* bridge = pane->hub();
	QVERIFY(bridge);

	Qdc::ChatModel* model = pane->findChild<Qdc::ChatModel*>();
	QVERIFY(model);

	Qdc::ChatMessage rich;
	rich.from = user(QStringLiteral("alice"));
	rich.text = QStringLiteral("**found it**");
	rich.richText = true;
	emit bridge->chatReceived(rich);

	Qdc::ChatMessage literal;
	literal.from = user(QStringLiteral("bob"));
	literal.text = QStringLiteral("weighs *about* 3kg");
	emit bridge->chatReceived(literal);

	QCOMPARE(model->rowCount(), 2);

	/*
	 * The flag is what decides how the line is read, so it has to survive the trip
	 * rather than being inferred from the text at the far end.
	 */
	QVERIFY(model->at(0).richText);
	QVERIFY(!model->at(1).richText);
}

void TestHubs::unreadCountsReachTheRailBadge()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);
	QSignalSpy badges(&hubs, &Qdc::View::badgeCountChanged);

	QVERIFY(hubs.openHub(QStringLiteral("dchub://192.0.2.1:411")));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);
	QCOMPARE(hubs.badgeCount(), 0);

	Qdc::HubBridge* bridge = pane->hub();
	QVERIFY(bridge);

	/*
	 * The view was never shown, so no pane is visible and every line counts as
	 * unread - which is the same condition as the window being on another view.
	 */
	Qdc::ChatMessage line;
	line.from = user(QStringLiteral("alice"));
	line.text = QStringLiteral("anyone about?");

	emit bridge->chatReceived(line);
	emit bridge->chatReceived(line);

	QCOMPARE(pane->unreadCount(), 2);
	QCOMPARE(hubs.badgeCount(), 2);
	QVERIFY(badges.count() >= 1);

	/* A private message adds its own pane's count to the same total. */
	Qdc::ChatMessage direct;
	direct.from = user(QStringLiteral("bob"));
	direct.context = QStringLiteral("bob");
	direct.privateMessage = true;
	direct.text = QStringLiteral("hello");
	emit bridge->chatReceived(direct);

	QCOMPARE(hubs.badgeCount(), 3);

	pane->clearUnread();
	QCOMPARE(hubs.badgeCount(), 1);
}

void TestHubs::hubPasswordIsAskedForOnce()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);

	QStringList asked;

	/*
	 * The prompt is injected rather than shown, because a modal dialog cannot be
	 * driven from a test - which is the reason it is a seam at all.
	 */
	hubs.setPasswordPrompt([&asked](const QString& hubUrl)
		-> std::optional<QString> {
		asked.append(hubUrl);
		return QStringLiteral("hunter2");
	});

	QVERIFY(hubs.openHub(QStringLiteral("dchub://192.0.2.1:411")));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);

	Qdc::HubBridge* bridge = pane->hub();
	QVERIFY(bridge);

	emit bridge->authenticationRequired();
	QCOMPARE(asked, QStringList({QStringLiteral("dchub://192.0.2.1:411")}));

	/*
	 * A hub asks more than once - the core sends an empty attempt and reports the
	 * refusal - and each request is a separate queued signal. Asking again after
	 * the first has been answered is a fresh question, so this is two.
	 */
	emit bridge->authenticationRequired();
	QCOMPARE(asked.size(), 2);

	/* Declining says so in the conversation rather than failing silently. */
	QStringList declined;
	hubs.setPasswordPrompt([&declined](const QString& hubUrl)
		-> std::optional<QString> {
		declined.append(hubUrl);
		return std::nullopt;
	});

	emit bridge->authenticationRequired();
	QCOMPARE(declined.size(), 1);

	/* And the view is still working afterwards. */
	QVERIFY(m_thread->invoke([] { }));
}

QTEST_MAIN(TestHubs)

void TestHubs::nothingCanBeSentUntilTheHubHasLoggedUsIn()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);
	QVERIFY(hubs.openHub(QStringLiteral("dchub://192.0.2.1:411")));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);

	Qdc::HubBridge* bridge = pane->hub();
	QVERIFY(bridge);

	Qdc::Composer* composer = pane->findChild<Qdc::Composer*>();
	QVERIFY(composer);

	QVERIFY(!composer->isSendable());

	/*
	 * Reaching the hub is not being logged in to it. A hub that accepts the
	 * connection and then says nothing leaves the socket exactly like this, and a
	 * message sent now is refused by the core - so the composer stays shut.
	 */
	emit bridge->netStatusChanged(Qdc::NetStatus::Connected);
	QVERIFY(!composer->isSendable());

	emit bridge->hubStatusChanged(Qdc::HubStatus::LoggedIn);
	QVERIFY(composer->isSendable());

	/* And shuts again when the hub goes away. */
	emit bridge->netStatusChanged(Qdc::NetStatus::Disconnected);
	QVERIFY(!composer->isSendable());
}

/*
 * The whole path against a real hub: flag set on send, relayed, read, rendered.
 *
 * Skipped unless it is asked for, because it needs a hub to talk to - the core built
 * here has no listening socket, so QuickDC's own hub is not one of the options. Run it
 * as two processes, the reader first:
 *
 *   QDC_LOOPBACK=recv QDC_LOOPBACK_URL=adc://localhost:1511 \
 *     QDC_LOOPBACK_NICK=qdc-reader tst_hubs loopback &
 *   QDC_LOOPBACK=send QDC_LOOPBACK_URL=adc://localhost:1511 \
 *     QDC_LOOPBACK_NICK=qdc-writer QDC_LOOPBACK_PEER=qdc-reader tst_hubs loopback
 *
 * Each process wants preference files of its own, which QTemporaryDir gives it, so the
 * two arrive with different client ids and the hub takes both.
 */
void TestHubs::loopback()
{
	const QString role = qEnvironmentVariable("QDC_LOOPBACK");
	if (role.isEmpty()) QSKIP("QDC_LOOPBACK is not set");

	const QString url = qEnvironmentVariable("QDC_LOOPBACK_URL");
	const QString nick = qEnvironmentVariable("QDC_LOOPBACK_NICK");

	Qdc::ConfigBridge config(m_thread);
	config.setString(Qdc::ConfigStore::Application, QStringLiteral("User"),
	                 QStringLiteral("Nickname"), nick);
	QVERIFY(config.save(Qdc::ConfigStore::Application));

	Qdc::HubBridge hub(m_thread, url);

	QSignalSpy chats(&hub, &Qdc::HubBridge::chatReceived);
	QSignalSpy errors(&hub, &Qdc::HubBridge::systemError);

	/* The id a private message has to be addressed by, learned as people arrive. */
	QHash<QString, QString> idFor;
	connect(&hub, &Qdc::HubBridge::userUpdated, this,
	        [&idFor](const Qdc::UserInfo& who) { idFor.insert(who.nick, who.id); });

	hub.connectToHub();

	QTRY_COMPARE_WITH_TIMEOUT(hub.hubStatus(), Qdc::HubStatus::LoggedIn, 20000);

	/*
	 * What the hub answered about RTF0 decides everything below: on a hub that announced
	 * it the flag travels, and on one that did not the same message goes out as plain
	 * text. Both are correct, so the test asserts whichever the hub chose rather than
	 * insisting on one.
	 */
	const bool agreed = hub.supportsRichText();

	qDebug() << role << "logged in as" << nick << "on" << hub.hubName()
	         << "- rich text agreed:" << agreed;

	const QString rich =
		QStringLiteral("**really** *quite* important, run `make -j8` first\n"
		               "> and read the [notes](https://example.org/notes)\n"
		               "1. first\n2. second\n- and a bullet");
	const QString literal = QStringLiteral("plain text with *asterisks* kept");
	const QString emoji = qEnvironmentVariable("QDC_LOOPBACK_EMOJI");

	const QString direct = QStringLiteral("privately, **this** matters");

	if (role == QStringLiteral("send"))
	{
		const QString other = qEnvironmentVariable("QDC_LOOPBACK_PEER");

		/* Time for the other side to finish logging in and be announced. */
		QTRY_VERIFY_WITH_TIMEOUT(idFor.contains(other), 20000);

		hub.sendChat(rich, true);
		hub.sendChat(literal, false);
		if (!emoji.isEmpty()) hub.sendChat(emoji, false);
		hub.sendPrivateChat(idFor.value(other), direct, true);

		QTest::qWait(3000);
	}
	else
	{
		QTRY_VERIFY_WITH_TIMEOUT(chats.count() >= 3, 25000);
	}

	QCOMPARE(errors.count(), 0);

	int sawRich = 0;
	int sawLiteral = 0;
	int sawDirect = 0;

	for (const QList<QVariant>& emitted : chats)
	{
		const Qdc::ChatMessage message = emitted.at(0).value<Qdc::ChatMessage>();
		if (!message.fromHub.isEmpty()) continue;

		qDebug().noquote() << "  from" << message.from.nick
		                   << "richText" << message.richText
		                   << "text" << message.text;

		if (message.text == rich)
		{
			++sawRich;

			QCOMPARE(message.richText, agreed);
			if (!agreed) continue;

			Qdc::RichText::RenderOptions options;
			options.prefix = QStringLiteral("<b>%1</b> ").arg(message.from.nick);
			options.indent = 40;

			qDebug().noquote() << "  rendered:"
			                   << Qdc::RichText::renderMarkdown(message.text, options);
		}

		if (message.text == literal)
		{
			++sawLiteral;
			QVERIFY2(!message.richText,
			         "an unflagged message arrived claiming to be markdown");
		}

		if (!emoji.isEmpty() && message.text == emoji)
		{
			qDebug().noquote() << "  emoji round trip ok, utf8 ="
			                   << message.text.toUtf8().toHex(' ');
		}

		if (message.text == direct)
		{
			++sawDirect;
			QVERIFY2(message.privateMessage, "a private message arrived as public chat");
			QCOMPARE(message.richText, agreed);
		}
	}

	/*
	 * Both sides see all three: the sender by the hub's echo of its own messages, the
	 * other by relay. The private one travels as a different command, so it is worth
	 * seeing separately from public chat.
	 */
	QCOMPARE(sawRich, 1);
	QCOMPARE(sawLiteral, 1);
	QCOMPARE(sawDirect, 1);
}

void TestHubs::picturesAreFetchedOnlyWhereThatWasAskedFor()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::TransferBridge transfers(m_thread, this);

	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, &transfers, &config);

	const QString hubUrl = QStringLiteral("adc://192.0.2.1:412");
	QVERIFY(hubs.openHub(hubUrl));

	const QString tth = QStringLiteral("VN6TQGXQ7HYQ5ARWZ2LNFVQ7YQKGXQ7HYQ5ARWZ");

	const auto sendPicture = [&](quint64 size) {
		Qdc::RichText::Attachment picture;
		picture.tth = tth;
		picture.name = QStringLiteral("photo.png");
		picture.size = size;

		Qdc::ChatMessage message;
		message.hubUrl = hubs.currentPane()->hub()->url();
		message.from = user(QStringLiteral("alice"));
		message.received = QDateTime::currentDateTime();
		message.richText = true;
		message.text = QStringLiteral("![a photo](%1)")
			.arg(Qdc::RichText::writeMagnet(picture));

		emit hubs.currentPane()->hub()->chatReceived(message);
	};

	/*
	 * Asked for and waited for, because the bridge reports the figures it was last given
	 * rather than reading the core when asked - which is what keeps a view from stalling
	 * on the core's thread.
	 */
	const auto queued = [&transfers] {
		QSignalSpy fresh(&transfers, &Qdc::TransferBridge::snapshotChanged);
		transfers.refresh();
		fresh.wait(5000);

		return transfers.queueSnapshot().size();
	};

	/*
	 * Off by default. Fetching means asking a stranger's client for a file the moment
	 * their message arrives, so it does not happen because nobody said anything.
	 */
	config.setBool(Qdc::ConfigStore::Application, QStringLiteral("Chat media"),
	               QStringLiteral("Fetch automatically"), false);

	const int before = queued();
	sendPicture(1024);
	QCOMPARE(queued(), before);

	/* Turned on generally, a small picture is fetched. */
	config.setBool(Qdc::ConfigStore::Application, QStringLiteral("Chat media"),
	               QStringLiteral("Fetch automatically"), true);
	config.setNumber(Qdc::ConfigStore::Application, QStringLiteral("Chat media"),
	                 QStringLiteral("Maximum size"), 2048);

	sendPicture(1024);
	QCOMPARE(queued(), before + 1);

	/* One over the limit is left for a person to decide about. */
	sendPicture(4096);
	QCOMPARE(queued(), before + 1);

	/* A size the sender did not give cannot be checked, so it is not fetched either. */
	Qdc::RichText::Attachment sizeless;
	sizeless.tth = QStringLiteral("QKGXQ7HYQ5ARWZ2LNFVQ7YVN6TQGXQ7HYQ5ARWZ");
	sizeless.name = QStringLiteral("mystery.png");

	Qdc::ChatMessage vague;
	vague.hubUrl = hubUrl;
	vague.from = user(QStringLiteral("alice"));
	vague.received = QDateTime::currentDateTime();
	vague.richText = true;
	vague.text = QStringLiteral("![?](magnet:?xt=urn:tree:tiger:%1&dn=mystery.png)")
		.arg(sizeless.tth);

	emit hubs.currentPane()->hub()->chatReceived(vague);
	QCOMPARE(queued(), before + 1);

	/* This hub says no, whatever the general setting is. */
	config.setBool(Qdc::ConfigStore::Session, hubs.currentPane()->hub()->url(),
	               QStringLiteral("Fetch automatically"), false);

	Qdc::RichText::Attachment second;
	second.tth = QStringLiteral("F6ED42THAKQCOCNHJOUCMOEOX4GOCT25D5WM7SY");
	second.name = QStringLiteral("another.png");
	second.size = 1024;

	Qdc::ChatMessage refused;
	refused.hubUrl = hubs.currentPane()->hub()->url();
	refused.from = user(QStringLiteral("alice"));
	refused.received = QDateTime::currentDateTime();
	refused.richText = true;
	refused.text = QStringLiteral("![no](%1)").arg(Qdc::RichText::writeMagnet(second));

	emit hubs.currentPane()->hub()->chatReceived(refused);
	QCOMPARE(queued(), before + 1);

	/* And a file that was offered rather than shown is never fetched by itself. */
	config.remove(Qdc::ConfigStore::Session, hubs.currentPane()->hub()->url(),
	              QStringLiteral("Fetch automatically"));

	Qdc::ChatMessage offered = refused;
	offered.text = QStringLiteral("[another.png](%1)")
		.arg(Qdc::RichText::writeMagnet(second));

	emit hubs.currentPane()->hub()->chatReceived(offered);
	QCOMPARE(queued(), before + 1);
}

void TestHubs::formattingIsOnlyOfferedWhereItCanBeSent()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);

	QVERIFY(hubs.openHub(QStringLiteral("dchub://192.0.2.1:411")));

	Qdc::Composer* nmdc = hubs.currentPane()->findChild<Qdc::Composer*>();
	QVERIFY(nmdc);

	/* NMDC has nowhere to put the flag, so the controls are shut for good. */
	QVERIFY(!nmdc->isRichTextEnabled());

	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.2:412")));

	Qdc::ChatPane* pane = hubs.currentPane();
	Qdc::Composer* adc = pane->findChild<Qdc::Composer*>();
	QVERIFY(adc);

	/* An ADC hub that has not answered yet is no different: nothing has been agreed. */
	QVERIFY(!adc->isRichTextEnabled());

	emit pane->hub()->richTextSupportChanged(true);
	QVERIFY(adc->isRichTextEnabled());

	/*
	 * A private conversation on the same hub answers to the same agreement. Driven by
	 * the signal here rather than by the bridge's own state, which only a hub that has
	 * really answered can set - the loopback test covers that half against a live hub.
	 */
	Qdc::ChatMessage direct;
	direct.hubUrl = QStringLiteral("adc://192.0.2.2:412");
	direct.from = user(QStringLiteral("bob"));
	direct.context = QStringLiteral("bob");
	direct.text = QStringLiteral("hello");
	direct.privateMessage = true;
	direct.received = QDateTime::currentDateTime();

	emit pane->hub()->chatReceived(direct);

	Qdc::DmPane* dm = hubs.findChild<Qdc::DmPane*>();
	QVERIFY(dm);

	Qdc::Composer* dmComposer = dm->findChild<Qdc::Composer*>();
	QVERIFY(dmComposer);

	emit pane->hub()->richTextSupportChanged(true);
	QVERIFY(dmComposer->isRichTextEnabled());

	/*
	 * The session ending takes the agreement with it: the next connection negotiates
	 * again, and what this hub allowed says nothing about what the next one will.
	 */
	emit pane->hub()->richTextSupportChanged(false);
	QVERIFY(!adc->isRichTextEnabled());
	QVERIFY(!dmComposer->isRichTextEnabled());
}

void TestHubs::theAttachButtonAsksForAFile()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);
	hubs.show();
	QVERIFY(QTest::qWaitForWindowExposed(&hubs));
	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.1:412")));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);

	Qdc::Composer* composer = pane->findChild<Qdc::Composer*>();
	QVERIFY(composer);

	QToolButton* attach = formatButton(composer, QStringLiteral("Attach a file"));
	QVERIFY(attach);

	/* Shut until the hub has agreed to carry a message that can name a file. */
	QVERIFY(!attach->isEnabled());

	emit pane->hub()->richTextSupportChanged(true);
	composer->setSendable(true);
	QVERIFY(attach->isEnabled());

	QSignalSpy asked(pane, &Qdc::ChatPane::attachRequested);

	/*
	 * Looked at from a timer, because the chooser runs its own event loop: nothing in
	 * this function gets another turn until the chooser is closed again, so whatever is
	 * to be checked has to be checked from inside it.
	 *
	 * What is checked is that a chooser is really on screen and big enough to use. Asking
	 * only whether one exists was not enough: this has failed three ways, and two of them
	 * left an object behind that answered yes - one dismissed by the press that opened it,
	 * and one an empty placeholder a hundred pixels wide because the platform declined to
	 * draw the real thing.
	 */
	bool visible = false;
	QSize seen;

	QTimer::singleShot(500, [&visible, &seen] {
		for (QWidget* top : QApplication::topLevelWidgets())
		{
			QFileDialog* chooser = qobject_cast<QFileDialog*>(top);
			if (!chooser || !chooser->isVisible()) continue;

			visible = true;
			seen = chooser->size();
			chooser->reject();
		}
	});

	attach->click();
	QCOMPARE(asked.count(), 1);

	QTest::qWait(2000);

	QVERIFY2(visible, "no chooser was on screen after the button was pressed");
	QVERIFY2(seen.width() > 200 && seen.height() > 150,
	         qPrintable(QStringLiteral("the chooser was only %1x%2")
	                    .arg(seen.width()).arg(seen.height())));

	/* Choosing nothing leaves the conversation alone. */
	QSignalSpy sent(composer, &Qdc::Composer::sendRequested);
	QCOMPARE(sent.count(), 0);
	QCOMPARE(composer->findChild<QTextEdit*>()->toPlainText(), QString());
}

void TestHubs::everyControlLooksDisabledWhenItIs()
{
	Qdc::applyTheme(Qdc::defaultTheme());

	Qdc::Composer composer;
	composer.setRichTextEnabled(true);
	composer.setSendable(true);
	composer.show();
	QVERIFY(QTest::qWaitForWindowExposed(&composer));

	const auto ink = [](QToolButton* button) {
		/* The darkest pixel drawn: how strongly the glyph is inked. */
		const QImage shot = button->grab().toImage();

		int darkest = 255;
		for (int y = 0; y < shot.height(); ++y)
			for (int x = 0; x < shot.width(); ++x)
			{
				const QColor at = shot.pixelColor(x, y);
				darkest = qMin(darkest, qGray(at.rgb()));
			}

		return darkest;
	};

	const QStringList controls = {
		QStringLiteral("Bold"), QStringLiteral("Italic"), QStringLiteral("Code"),
		QStringLiteral("Quote"), QStringLiteral("Code block"),
		QStringLiteral("Bulleted list"), QStringLiteral("Numbered list"),
		QStringLiteral("Link"), QStringLiteral("Attach a file")
	};

	for (const QString& name : controls)
	{
		QToolButton* button = formatButton(&composer, name);
		QVERIFY2(button, qPrintable(name));

		button->setEnabled(true);
		QTest::qWait(10);
		const int on = ink(button);

		button->setEnabled(false);
		QTest::qWait(10);
		const int off = ink(button);

		/*
		 * A control that cannot be used has to look like it. Measured rather than
		 * assumed, because a colour emoji is drawn from its own font and ignores the
		 * pen the style would grey it with - so a glyph that happens to be one gives
		 * no sign at all that pressing it will do nothing.
		 */
		QVERIFY2(off > on + 20,
		         qPrintable(QStringLiteral("%1 inks %2 enabled and %3 disabled")
		                    .arg(name).arg(on).arg(off)));
	}
}

void TestHubs::aClickedHubLinkOpensTheHubItself()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);
	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.1:412")));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);

	/*
	 * A hub address is opened here rather than handed to the desktop, which would at
	 * best start a second copy of QuickDC.
	 */
	emit pane->linkActivated(QUrl(QStringLiteral("adc://192.0.2.3:412")));

	QCOMPARE(hubs.openHubs().size(), 2);
	QVERIFY(hubs.openHubs().contains(QStringLiteral("adc://192.0.2.3:412")));

	/* And the newly opened one is what is shown, since the click asked to go there. */
	QVERIFY(hubs.currentPane());
	QCOMPARE(hubs.currentPane()->hub()->url(), QStringLiteral("adc://192.0.2.3:412"));

	/*
	 * A scheme nothing may follow is dropped rather than passed on. Nothing renders one
	 * as a link in the first place; this is the last gate before the desktop.
	 */
	emit pane->linkActivated(QUrl(QStringLiteral("file:///etc/passwd")));
	QCOMPARE(hubs.openHubs().size(), 2);
}

void TestHubs::anEchoOfOurOwnPrivateMessageOpensTheirConversation()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);
	QVERIFY(hubs.openHub(QStringLiteral("dchub://192.0.2.1:411")));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);

	Qdc::HubBridge* bridge = pane->hub();
	QVERIFY(bridge);

	emit bridge->userUpdated(user(QStringLiteral("bob")));
	QCOMPARE(hubs.paneCount(), 1);

	/*
	 * A private message of ours, echoed back by the hub: we are named as the
	 * sender, and only the context says who it was addressed to. It belongs in a
	 * conversation with them - filing it under the sender puts our own messages
	 * in a conversation with ourselves.
	 */
	Qdc::ChatMessage echoed;
	echoed.from = user(QStringLiteral("me"));
	echoed.context = QStringLiteral("bob");
	echoed.privateMessage = true;
	echoed.text = QStringLiteral("hello bob");
	emit bridge->chatReceived(echoed);

	QCOMPARE(hubs.paneCount(), 2);

	const QList<Qdc::DmPane*> conversations = hubs.findChildren<Qdc::DmPane*>();
	QCOMPARE(conversations.size(), 1);

	/* Named for them, not for their id - the context carries only the id. */
	QCOMPARE(conversations.first()->paneTitle(), QStringLiteral("bob"));

	/* And their reply joins it rather than starting a second. */
	Qdc::ChatMessage reply;
	reply.from = user(QStringLiteral("bob"));
	reply.context = QStringLiteral("bob");
	reply.privateMessage = true;
	reply.text = QStringLiteral("hi back");
	emit bridge->chatReceived(reply);

	QCOMPARE(hubs.paneCount(), 2);
}

#include "tst_hubs.moc"

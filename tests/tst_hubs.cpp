/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/boardbridge.h>
#include <core/configbridge.h>
#include <core/corethread.h>
#include <core/favourites.h>
#include <core/filelistbridge.h>
#include <core/transferbridge.h>
#include <core/hubbridge.h>
#include <core/recenthubs.h>
#include <gui/boardmodel.h>
#include <gui/boardpane.h>
#include <gui/chatfeed.h>
#include <gui/chatmodel.h>
#include <gui/chatpane.h>
#include <gui/composer.h>
#include <gui/connectionbar.h>
#include <gui/filelistview.h>
#include <gui/hubheader.h>
#include <gui/hubpane.h>
#include <gui/hubsview.h>
#include <gui/richtext.h>
#include <gui/theme.h>
#include <gui/glyphs.h>
#include <gui/usermodel.h>

#include <QApplication>
#include <QHBoxLayout>
#include <QContextMenuEvent>
#include <QDir>
#include <QStyleOptionViewItem>
#include <QBuffer>
#include <QMimeData>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QIcon>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextList>
#include <QToolButton>
#include <QTreeWidget>
#include <QFileDialog>
#include <QMenu>
#include <QTimer>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QAbstractButton>
#include <QSignalSpy>
#include <QTimer>
#include <QTemporaryDir>
#include <QThread>
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
	void userModelDrawsAnAwayUserAsAnOutline();
	void userModelColoursAnOperatorApartEvenWhenAway();
	void chatFeedGivesMultiLineMessagesMoreHeight();
	void chatFeedGivesAMarkdownMessageRoomForItsBlocks();
	void chatFeedForgetsItsLayoutsWhenRowsMove();
	void chatFeedClicksALinkAndIgnoresADrag();
	void chatFeedLinksAnAddressInAPlainMessage();
	void chatFeedSweepsOutPartOfALine();
	void chatFeedSweepsAcrossRowsAndCopiesWhatItCovers();
	void chatFeedSweepsBackwardsJustTheSame();
	void chatFeedShowsTheTextItHasSwept();
	void chatFeedForgetsASelectionWhoseRowsMoved();
	void chatFeedSweepsOnPastTheBottomEdge();
	void aNicknameInTheFeedAsksForItsOwnMenu();
	void chatFeedDrawsAPictureItHas();
	void chatFeedKeepsAnAnimationMoving();
	void chatFeedBandsAHubNotice();
	void chatModelReadsARichMessageAsProse();

	void favouritesRoundTripThroughTheStore();
	void favouritesNormaliseWhatTheyAreGiven();
	void favouritesMigrateWhatWasWrittenBefore();
	void favouritesShrinkWithoutLeavingATail();
	void anOpenHubIsRecordedAndFollowsTheFavouritesStore();

	void urlNormalisationFillsInSchemeAndPort();
	void noHubCarriesRichTextUntilItAnnouncesTheExtension();

	void settingsUrlDropsWhatDoesNotNameTheHub();
	void aHubKeepsAnIdentityOfItsOwn();
	void aKeyprintInTheAddressIsStillTheSameHubsSettings();
	void clearingAHubsIdentityHandsItBackToTheGeneralOne();
	void aUserOffersMoreThanAConversationOnASecondClick();

	void composerFlagsOnlyWhatItActuallyFormatted();
	void composerSerializesEachConstructToMarkdown();
	void composerWritesListsAsMarkdown();
	void emojiPickerGivesEveryEmojiRoomToDraw();
	void anAttachedFileIsSentAsALinkToItsHash();
	void aPictureIsSentAsMediaRatherThanAsAFile();
	void aPastedPictureIsOfferedLikeAChosenFile();
	void aPastedPictureIsWrittenDownWhereItCanBeServed();
	void aDroppedFileIsOfferedRatherThanNamed();
	void oldPastedPicturesAreSweptAway();
	void composerFlattensFormattingWhereItCannotBeSent();
	void aPastedPageArrivesAsPlainTextWhereFormattingCannotBeSent();

	void filelistModelExposesTheTree();
	void filelistModelFilterKeepsAncestors();

	void openingTheSameHubTwiceReusesThePane();
	void privateMessagesOpenAConversation();
	void aConversationShutsWhenTheOtherPartyLeaves();
	void aConversationFollowsThemBackUnderANewNickname();
	void unreadCountsReachTheRailBadge();
	void aRichTextFlagReachesThePane();
	void hubPasswordIsAskedForOnce();
	void loopback();
	void nothingCanBeSentUntilTheHubHasLoggedUsIn();
	void formattingIsOnlyOfferedWhereItCanBeSent();
	void aClickedHubLinkOpensTheHubItself();
	void picturesAreFetchedOnlyWhereThatWasAskedFor();
	void anArrivingMessageDoesNotWaitForTheCore();
	void theAttachButtonAsksForAFile();
	void everyControlLooksDisabledWhenItIs();
	void anEchoOfOurOwnPrivateMessageOpensTheirConversation();
	void theConnectionBandOnlyShowsWhileAHubCannotBeUsed();
	void theConnectionBandCountsTheWaitDownAndForgetsIt();
	void aScheduledReconnectReachesTheBand();
	void theBandOffersToStopWaitingOnlyWhileThereIsAWait();
	void pressingReconnectNowAsksTheHub();
	void thePadlockSaysNothingUntilThereIsAConnection();
	void theHeaderNamesTheHubAndKeepsTheAddress();
	void aHubsOwnDescriptionReachesThePane();
	void theUserListCanBeFilteredDownToAName();
	void hidingTheFilterPutsEverybodyBack();
	void aNameThatHasLeftStillResolvesToWhatItWas();
	void theHubsOwnSettingsHideBehindTheGears();
	void aConversationShowsThePersonWhereAHubShowsItsStar();
	void everyRowIsNamedByTheKindOfThingItStandsFor();
	void theBoardGlyphIsAThirdFigureInTheSameSpace();

	void aDescribedBoardBecomesARowUnderItsHub();
	void aBoardRowShowsItsOwnPane();
	void aBoardThatIsGoneTakesItsRowWithIt();
	void aBoardRowOffersTheBoardsOwnMenu();
	void openingABoardIsWhatSubscribesToIt();
	void aHubWithoutBoardsForgetsWhatWasSubscribed();
	void postsNobodyHasOpenedReachTheRailBadge();
	void theBoardsOpenOnAHubComeBackWithIt();
	void closingAHubLeavesNoBoardRowBehind();
	void aComposedPostIsTheDocumentItsHashCovers();
	void whatWasTypedSurvivesLeavingTheSettings();
	void aDoctoredHubNameCannotReverseItsRow();
	void aNicknameIsReducedInItsRowAndItsTooltip();
	void aRemoteNameInOneOfOurSentencesIsReduced();
	void aFileNameInAMessageBoxIsReduced();
	void theFeedMeasuresTheSpeakerItDraws();

private:
	Qdc::UserInfo user(const QString& nick, bool op = false) const;

	/** A descriptor as a hub sends one: every permission but withdrawing another's. */
	static Qdc::BoardInfo board(const QString& name);

	/** One index entry, with a hash a tiger root can be told from. */
	static Qdc::BoardEntry entry(const QString& tth, quint64 timestamp,
	                             const QString& board = QStringLiteral("general"));

	/**
	 * A directory of this test's own for the boards.
	 *
	 * The bridge writes under "~/.quickdc" unless it is told otherwise, and that is the
	 * user's own directory rather than somewhere a test may leave an index behind.
	 */
	QString boardScratch(const QString& name) const;

	static QTreeWidget* hubTree(Qdc::HubsView& hubs);

	/** The board pane, and the stack it was put in, which is what showing one moves. */
	static Qdc::BoardPane* boardPane(Qdc::HubsView& hubs);
	static QStackedWidget* stackOf(QWidget* pane);

	/** The formatting button whose accessible name is 'name', or nullptr. */
	static QToolButton* formatButton(Qdc::Composer* composer, const QString& name);

	/** Type into the composer's box and press Return, as a person would. */
	static void writeAndSend(Qdc::Composer* composer, const QString& text);

	/** Drag from one point to another in steps, as a pointer does. */
	static void sweep(QWidget* on, const QPoint& from, const QPoint& to);

	QTemporaryDir m_dir;
	Qdc::CoreThread* m_thread = nullptr;
	Qdc::FilelistBridge* m_filelists = nullptr;
};

void TestHubs::sweep(QWidget* on, const QPoint& from, const QPoint& to)
{
	QTest::mousePress(on, Qt::LeftButton, Qt::NoModifier, from);

	/*
	 * In steps rather than one jump: a selection follows the moves, and a press
	 * followed straight by a release at the far end is a click, not a sweep.
	 */
	constexpr int STEPS = 10;

	for (int n = 1; n <= STEPS; ++n)
	{
		QTest::mouseMove(on, QPoint(from.x() + (to.x() - from.x()) * n / STEPS,
		                            from.y() + (to.y() - from.y()) * n / STEPS));
	}

	QTest::mouseRelease(on, Qt::LeftButton, Qt::NoModifier, to);
}

Qdc::UserInfo TestHubs::user(const QString& nick, bool op) const
{
	Qdc::UserInfo info;
	info.id = nick;
	info.nick = nick;
	info.sourceId = nick;
	info.op = op;
	return info;
}

Qdc::BoardInfo TestHubs::board(const QString& name)
{
	Qdc::BoardInfo described;
	described.name = name;
	described.title = QStringLiteral("General discussion");
	described.description = QStringLiteral("Anything about this hub");
	described.permissions = 1 + 2 + 4 + 8;
	described.maxSize = 262144;
	described.newest = 1786439662;
	described.oldest = 0;
	described.posts = 2;

	return described;
}

Qdc::BoardEntry TestHubs::entry(const QString& tth, quint64 timestamp,
                                const QString& board)
{
	Qdc::BoardEntry held;
	held.tth = tth;
	held.size = 412;
	held.board = board;
	held.cid = QStringLiteral("IPJJWEPPPLCA3PF2ZCRRYO4F2ZX2EV2JMW2KC3I");
	held.nick = QStringLiteral("janvidar");
	held.thread = tth;
	held.subject = QStringLiteral("Hub upgrade on Saturday");
	held.timestamp = timestamp;

	return held;
}

QString TestHubs::boardScratch(const QString& name) const
{
	return QDir(m_dir.path()).filePath(QStringLiteral("boards-") + name);
}

QTreeWidget* TestHubs::hubTree(Qdc::HubsView& hubs)
{
	return hubs.findChild<QTreeWidget*>(QStringLiteral("hubTree"));
}

Qdc::BoardPane* TestHubs::boardPane(Qdc::HubsView& hubs)
{
	return hubs.findChildren<Qdc::BoardPane*>().value(0);
}

QStackedWidget* TestHubs::stackOf(QWidget* pane)
{
	return pane ? qobject_cast<QStackedWidget*>(pane->parentWidget()) : nullptr;
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

void TestHubs::userModelDrawsAnAwayUserAsAnOutline()
{
	Qdc::UserModel model;

	Qdc::UserInfo here;
	here.id = QStringLiteral("AAAB");
	here.nick = QStringLiteral("alice");

	Qdc::UserInfo gone;
	gone.id = QStringLiteral("AAAC");
	gone.nick = QStringLiteral("bob");
	gone.away = true;

	model.upsert(here);
	model.upsert(gone);

	const QModelIndex first = model.index(0);
	const QModelIndex second = model.index(1);

	QCOMPARE(model.at(0).nick, QStringLiteral("alice"));
	QCOMPARE(model.at(1).nick, QStringLiteral("bob"));

	const QIcon present = qvariant_cast<QIcon>(model.data(first, Qt::DecorationRole));
	const QIcon away = qvariant_cast<QIcon>(model.data(second, Qt::DecorationRole));

	QVERIFY(!present.isNull());
	QVERIFY(!away.isNull());

	/*
	 * Two different figures, not two colourings of one: the filled form and the
	 * outline are what makes the two states tellable apart by somebody who cannot
	 * distinguish the inks. Compared as images, which is the only thing a QIcon will
	 * answer about what it looks like.
	 */
	const QSize size(13, 13);
	QVERIFY(present.pixmap(size).toImage() != away.pixmap(size).toImage());

	/* And it is said in words as well as drawn. */
	QVERIFY(model.data(second, Qt::ToolTipRole).toString().contains(
		QStringLiteral("Away")));

	QVERIFY(!model.data(first, Qt::ToolTipRole).toString().contains(
		QStringLiteral("Away")));

	/* Coming back is an update like any other, and the row redraws as present. */
	gone.away = false;
	model.upsert(gone);

	QVERIFY(model.data(second, Qt::DecorationRole).isValid());
	QVERIFY(!model.data(second, Qt::ToolTipRole).toString().contains(
		QStringLiteral("Away")));
}

void TestHubs::userModelColoursAnOperatorApartEvenWhenAway()
{
	Qdc::UserModel model;

	const auto figureFor = [](bool op, bool away) {
		Qdc::UserModel one;

		Qdc::UserInfo user;
		user.id = QStringLiteral("AAAB");
		user.nick = QStringLiteral("someone");
		user.op = op;
		user.away = away;

		one.upsert(user);

		return qvariant_cast<QIcon>(one.data(one.index(0), Qt::DecorationRole))
			.pixmap(QSize(13, 13)).toImage();
	};

	const QImage user = figureFor(false, false);
	const QImage op = figureFor(true, false);

	QVERIFY(!user.isNull());
	QVERIFY(!op.isNull());

	/* An operator is a different colour from everybody else. */
	QVERIFY(user != op);

	/*
	 * And still is when they are away. The shape carries being away and the colour
	 * carries being an operator, so neither state can hide the other - an away
	 * operator drawn in a neutral grey would be indistinguishable from anybody else
	 * who had stepped out.
	 */
	const QImage userAway = figureFor(false, true);
	const QImage opAway = figureFor(true, true);

	QVERIFY(userAway != opAway);

	/* Away is still visibly away for both of them. */
	QVERIFY(userAway != user);
	QVERIFY(opAway != op);

	/* And the model puts the operators at the top regardless. */
	Qdc::UserInfo plain;
	plain.id = QStringLiteral("AAAC");
	plain.nick = QStringLiteral("alice");

	Qdc::UserInfo boss;
	boss.id = QStringLiteral("AAAD");
	boss.nick = QStringLiteral("zoe");
	boss.op = true;

	model.upsert(plain);
	model.upsert(boss);

	QCOMPARE(model.at(0).nick, QStringLiteral("zoe"));
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

void TestHubs::aNicknameInTheFeedAsksForItsOwnMenu()
{
	Qdc::ChatFeed feed;
	Qdc::ChatModel model;

	feed.setModel(&model);
	feed.resize(600, 300);
	feed.show();

	Qdc::ChatMessage said;
	said.from = user(QStringLiteral("alice"), true);
	said.received = QDateTime::currentDateTime();
	said.text = QStringLiteral("hello everybody");
	model.append(said);

	Qdc::ChatMessage notice;
	notice.fromHub = QStringLiteral("The Example Hub");
	notice.received = QDateTime::currentDateTime();
	notice.text = QStringLiteral("the hub is going down");
	model.append(notice);

	QVERIFY(QTest::qWaitForWindowExposed(&feed));

	/*
	 * Swept across the row, as the link test does: where the name falls depends on
	 * the width of the timestamp in whatever font this machine uses.
	 */
	const auto sweep = [&feed, &model](int row) {
		const QRect box = feed.visualRect(model.index(row));
		QPoint found;

		for (int x = box.left(); x < box.right(); ++x)
		{
			const QPoint point(x, box.center().y());

			if (feed.anchorAt(point).startsWith(QStringLiteral("qdc-user:")))
			{
				found = point;
				break;
			}
		}

		return found;
	};

	const QPoint onNick = sweep(0);
	QVERIFY2(!onNick.isNull(), "the speaker's name is not an anchor");

	/* A line the hub generated names nobody, so there is nothing to right-click. */
	QVERIFY(sweep(1).isNull());

	QSignalSpy asked(&feed, &Qdc::ChatFeed::userMenuRequested);
	QVERIFY(asked.isValid());

	QContextMenuEvent event(QContextMenuEvent::Mouse, onNick,
	                        feed.viewport()->mapToGlobal(onNick));

	QApplication::sendEvent(feed.viewport(), &event);

	QCOMPARE(asked.count(), 1);

	const Qdc::UserInfo who = asked.at(0).at(0).value<Qdc::UserInfo>();
	QCOMPARE(who.nick, QStringLiteral("alice"));

	/* The record the message carried, standing and all. */
	QVERIFY(who.op);

	/*
	 * A name is an anchor so that it can be found, not so that it can be followed:
	 * a left click on one must not reach the desktop.
	 */
	QSignalSpy followed(&feed, &Qdc::ChatFeed::linkActivated);

	QTest::mousePress(feed.viewport(), Qt::LeftButton, Qt::NoModifier, onNick);
	QTest::mouseRelease(feed.viewport(), Qt::LeftButton, Qt::NoModifier, onNick);

	QCOMPARE(followed.count(), 0);
}

void TestHubs::chatFeedLinksAnAddressInAPlainMessage()
{
	Qdc::ChatFeed feed;
	Qdc::ChatModel model;

	feed.setModel(&model);
	feed.resize(600, 300);
	feed.show();

	/*
	 * No RT1 on this one, which is every message on a hub that cannot carry the flag -
	 * and most of them cannot. The address in it is still the thing worth clicking.
	 */
	Qdc::ChatMessage message;
	message.from = user(QStringLiteral("alice"));
	message.received = QDateTime::currentDateTime();
	message.richText = false;
	message.text = QStringLiteral("the rules are at https://example.org/rules ok");
	model.append(message);

	QVERIFY(QTest::qWaitForWindowExposed(&feed));

	const QRect row = feed.visualRect(model.index(0));
	QVERIFY(row.isValid());

	QPoint onLink;
	for (int x = row.left(); x < row.right(); ++x)
	{
		const QPoint point(x, row.center().y());

		if (feed.anchorAt(point) == QStringLiteral("https://example.org/rules"))
		{
			onLink = point;
			break;
		}
	}

	QVERIFY2(!onLink.isNull(), "the address in a plain message is not a link");

	QSignalSpy followed(&feed, &Qdc::ChatFeed::linkActivated);

	QTest::mousePress(feed.viewport(), Qt::LeftButton, Qt::NoModifier, onLink);
	QTest::mouseRelease(feed.viewport(), Qt::LeftButton, Qt::NoModifier, onLink);

	QCOMPARE(followed.count(), 1);
	QCOMPARE(followed.at(0).at(0).toUrl(),
	         QUrl(QStringLiteral("https://example.org/rules")));

	/* And the asterisks around it are still asterisks, not formatting. */
	Qdc::ChatMessage literal;
	literal.from = user(QStringLiteral("bob"));
	literal.received = QDateTime::currentDateTime();
	literal.text = QStringLiteral("weighs *about* 3kg");
	model.append(literal);

	QVERIFY(model.index(1).data(Qdc::ChatModel::MessageRole)
	        .value<Qdc::ChatMessage>().text.contains(QStringLiteral("*about*")));
}

namespace {

/** A feed of three short lines, laid out and exposed, ready to be swept. */
struct SweepableFeed {
	Qdc::ChatModel model;
	Qdc::ChatFeed feed;

	bool ready()
	{
		feed.setModel(&model);
		feed.resize(700, 220);

		for (const auto& one : {std::make_pair("alice", "the build is at revision 4471"),
		                        std::make_pair("bob", "thanks, pulling it now"),
		                        std::make_pair("carol", "mine says 4470 though")})
		{
			Qdc::ChatMessage message;
			message.from.id = QString::fromLatin1(one.first);
			message.from.nick = QString::fromLatin1(one.first);
			message.from.sourceId = message.from.nick;
			message.received = QDateTime::currentDateTime();
			message.text = QString::fromLatin1(one.second);
			model.append(message);
		}

		feed.show();
		return QTest::qWaitForWindowExposed(&feed);
	}

	/** The middle of row 'n', 'across' pixels in from its left edge. */
	QPoint at(int n, int across)
	{
		const QRect row = feed.visualRect(model.index(n));
		return QPoint(row.left() + across, row.center().y());
	}
};

}

void TestHubs::chatFeedSweepsOutPartOfALine()
{
	SweepableFeed one;
	QVERIFY(one.ready());

	QVERIFY(!one.feed.hasSelectedText());
	QCOMPARE(one.feed.selectedText(), QString());

	sweep(one.feed.viewport(), one.at(0, 120), one.at(0, 300));

	QVERIFY(one.feed.hasSelectedText());

	const QString swept = one.feed.selectedText();

	/*
	 * A run out of the middle of the line and nothing else: no timestamp, no
	 * nickname, no neighbouring message. That is the whole point of the change - a
	 * row at a time could not do it.
	 */
	QVERIFY2(!swept.isEmpty(), "sweeping across a line selected nothing");
	QVERIFY2(!swept.contains(QLatin1Char('\n')), qPrintable(swept));

	const QString whole =
		QStringLiteral("the build is at revision 4471");

	QVERIFY2(whole.contains(swept), qPrintable(swept));
	QVERIFY2(swept.length() < whole.length(), qPrintable(swept));

	/* Pressing without dragging puts it away again. */
	QTest::mouseClick(one.feed.viewport(), Qt::LeftButton, Qt::NoModifier,
	                  one.at(2, 40));

	QVERIFY(!one.feed.hasSelectedText());
}

void TestHubs::chatFeedSweepsAcrossRowsAndCopiesWhatItCovers()
{
	SweepableFeed one;
	QVERIFY(one.ready());

	sweep(one.feed.viewport(), one.at(0, 200), one.at(2, 220));

	const QString swept = one.feed.selectedText();

	/* Three rows touched, so two newlines between them. */
	QCOMPARE(swept.count(QLatin1Char('\n')), 2);

	const QStringList lines = swept.split(QLatin1Char('\n'));
	QCOMPARE(lines.size(), 3);

	/* The row in the middle is covered end to end, timestamp and name included. */
	QVERIFY2(lines.at(1).contains(QStringLiteral("thanks, pulling it now")),
	         qPrintable(lines.at(1)));
	QVERIFY2(lines.at(1).contains(QStringLiteral("bob")), qPrintable(lines.at(1)));

	/* The first and last are partial, which is why they are not the whole line. */
	QVERIFY2(!lines.at(0).contains(QStringLiteral("alice")), qPrintable(lines.at(0)));
	QVERIFY2(lines.at(2).contains(QStringLiteral("carol")), qPrintable(lines.at(2)));

	/*
	 * And Ctrl+C reaches the clipboard, which nothing in this application could do
	 * before: there was no copy shortcut anywhere in it.
	 */
	QGuiApplication::clipboard()->clear();

	QTest::keyClick(&one.feed, Qt::Key_C, Qt::ControlModifier);
	QCOMPARE(QGuiApplication::clipboard()->text(), swept);

	/* Ctrl+A takes every row, first character to last. */
	QTest::keyClick(&one.feed, Qt::Key_A, Qt::ControlModifier);

	const QString all = one.feed.selectedText();
	QVERIFY2(all.contains(QStringLiteral("alice")), qPrintable(all));
	QVERIFY2(all.contains(QStringLiteral("mine says 4470 though")), qPrintable(all));
	QCOMPARE(all.count(QLatin1Char('\n')), 2);
}

void TestHubs::chatFeedSweepsBackwardsJustTheSame()
{
	SweepableFeed one;
	QVERIFY(one.ready());

	sweep(one.feed.viewport(), one.at(0, 200), one.at(2, 220));
	const QString forwards = one.feed.selectedText();
	QVERIFY(!forwards.isEmpty());

	/* Up the way rather than down it; a selection has no preferred direction. */
	SweepableFeed other;
	QVERIFY(other.ready());

	sweep(other.feed.viewport(), other.at(2, 220), other.at(0, 200));

	QCOMPARE(other.feed.selectedText(), forwards);
}

void TestHubs::chatFeedShowsTheTextItHasSwept()
{
	SweepableFeed one;
	QVERIFY(one.ready());

	const QPixmap before = one.feed.grab();

	sweep(one.feed.viewport(), one.at(0, 120), one.at(0, 300));
	QVERIFY(one.feed.hasSelectedText());

	const QPixmap after = one.feed.grab();

	/*
	 * Drawn, not merely remembered. This is the complaint the whole change answers -
	 * a selection nothing on screen reported - so it is asserted against the pixels
	 * rather than against the state behind them.
	 */
	QVERIFY2(before.toImage() != after.toImage(),
	         "sweeping text changed nothing on screen");
}

void TestHubs::chatFeedForgetsASelectionWhoseRowsMoved()
{
	SweepableFeed one;
	QVERIFY(one.ready());

	sweep(one.feed.viewport(), one.at(0, 200), one.at(1, 200));
	QVERIFY(one.feed.hasSelectedText());

	/*
	 * A selection names its rows by number, and dropping the oldest renumbers the
	 * rest - so what was swept would quietly become a sweep across other messages.
	 * Trimmed the way the feed really trims: a capacity, and one more line arriving.
	 */
	one.model.setCapacity(3);

	Qdc::ChatMessage late;
	late.from = user(QStringLiteral("dave"));
	late.received = QDateTime::currentDateTime();
	late.text = QStringLiteral("late to this");
	one.model.append(late);

	QCOMPARE(one.model.rowCount(), 3);

	QVERIFY(!one.feed.hasSelectedText());
	QCOMPARE(one.feed.selectedText(), QString());
}

void TestHubs::chatFeedSweepsOnPastTheBottomEdge()
{
	Qdc::ChatModel model;
	Qdc::ChatFeed feed;

	feed.setModel(&model);
	feed.resize(700, 160);

	for (int n = 0; n < 60; ++n)
	{
		Qdc::ChatMessage message;
		message.from = user(QStringLiteral("alice"));
		message.received = QDateTime::currentDateTime();
		message.text = QStringLiteral("line number %1 of the conversation").arg(n);
		model.append(message);
	}

	feed.show();
	QVERIFY(QTest::qWaitForWindowExposed(&feed));

	feed.verticalScrollBar()->setValue(0);

	const int before = feed.verticalScrollBar()->value();

	/*
	 * Held below the last visible row, which is how anybody selects more of a
	 * conversation than fits on screen. The view scrolls itself; what this pins is
	 * that the selection keeps up with it rather than stopping at whichever row was
	 * the last one visible when the sweep began.
	 */
	QTest::mousePress(feed.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(200, 8));

	const QPoint past(300, feed.viewport()->height() + 30);

	for (int n = 0; n < 30 && feed.verticalScrollBar()->value()
	                          < feed.verticalScrollBar()->maximum(); ++n)
	{
		QTest::mouseMove(feed.viewport(), past);
		QTest::qWait(20);
	}

	QTest::mouseRelease(feed.viewport(), Qt::LeftButton, Qt::NoModifier, past);

	QVERIFY2(feed.verticalScrollBar()->value() > before, "the sweep did not scroll");

	/* Far more than the handful of rows that were on screen to begin with. */
	QVERIFY2(feed.selectedText().count(QLatin1Char('\n')) > 10,
	         qPrintable(QString::number(
			 feed.selectedText().count(QLatin1Char('\n')))));
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

void TestHubs::chatFeedKeepsAnAnimationMoving()
{
	/*
	 * A two frame GIF, written out byte for byte: Qt reads animations but cannot write
	 * one, so there is no way to make this from a QImage.
	 */
	static const char* const TWO_FRAMES =
		"R0lGODlhBAAEAIAAAOAgICBA4CH/C05FVFNDQVBFMi4wAwEAAAAh+QQEMgAAACwAAAAABAAEAAACBwQ"
		"AAAAAACgAIfkEBDIAAAAsAAAAAAQABAAAAgdMkiRJkiQpADs=";

	const QString path = m_dir.path() + QStringLiteral("/moving.gif");

	QFile gif(path);
	QVERIFY(gif.open(QIODevice::WriteOnly));
	QVERIFY(gif.write(QByteArray::fromBase64(TWO_FRAMES)) > 0);
	gif.close();

	QImageReader reader(path);
	QVERIFY2(reader.supportsAnimation() && reader.imageCount() > 1,
	         "the test's own GIF is not animated");

	const QString tth = QStringLiteral("VN6TQGXQ7HYQ5ARWZ2LNFVQ7YQKGXQ7HYQ5ARWZ");

	Qdc::RichText::Attachment picture;
	picture.tth = tth;
	picture.name = QStringLiteral("moving.gif");
	picture.size = 95;

	Qdc::ChatModel model;

	Qdc::ChatMessage message;
	message.from = user(QStringLiteral("alice"));
	message.received = QDateTime::currentDateTime();
	message.richText = true;
	message.text = QStringLiteral("![it moves](%1)")
		.arg(Qdc::RichText::writeMagnet(picture));
	model.append(message);

	Qdc::ChatFeed feed;
	feed.setModel(&model);
	feed.setMediaResolver([path, tth](const QString& asked) {
		Qdc::RichText::LocalMedia here;
		if (asked != tth) return here;

		here.path = path;
		here.size = QSize(4, 4);
		return here;
	});

	Qdc::ChatDelegate* delegate = qobject_cast<Qdc::ChatDelegate*>(feed.itemDelegate());
	QVERIFY(delegate);

	QSignalSpy moved(delegate, &Qdc::ChatDelegate::rowNeedsRepaint);

	feed.resize(300, 200);
	feed.show();
	QVERIFY(QTest::qWaitForWindowExposed(&feed));

	/*
	 * Three frames from a picture that has two, which is the looping: an animation that
	 * stopped at the end of the file would have asked twice and no more.
	 */
	QTRY_VERIFY_WITH_TIMEOUT(moved.count() >= 3, 5000);

	/* Every frame asks for the row it belongs to, not for the whole feed. */
	QCOMPARE(moved.at(0).at(0).toInt(), 0);
}

void TestHubs::chatFeedBandsAHubNotice()
{
	Qdc::ChatModel model;

	Qdc::ChatMessage said;
	said.from = user(QStringLiteral("alice"));
	said.received = QDateTime::currentDateTime();
	said.text = QStringLiteral("hello everyone");
	model.append(said);

	Qdc::ChatMessage notice;
	notice.fromHub = QStringLiteral("Example Hub");
	notice.received = said.received;
	notice.text = QStringLiteral("the hub is going down for maintenance");
	model.append(notice);

	Qdc::ChatDelegate delegate;
	const Qdc::Theme& scheme = Qdc::theme();

	/* How much of a row the delegate filled with the scheme's notice colour. */
	const auto banded = [&](int row) {
		QStyleOptionViewItem option;
		option.font = QApplication::font();
		option.palette = QApplication::palette();
		option.rect = QRect(0, 0, 420, 0);

		const int height = delegate.sizeHint(option, model.index(row)).height();
		option.rect = QRect(0, 0, 420, height);

		QImage shot(420, height, QImage::Format_RGB32);
		shot.fill(scheme.content);

		QPainter painter(&shot);
		delegate.paint(&painter, option, model.index(row));
		painter.end();

		int found = 0;
		for (int y = 0; y < shot.height(); ++y)
			for (int x = 0; x < shot.width(); ++x)
				if (shot.pixelColor(x, y) == scheme.notice) ++found;

		return found;
	};

	/*
	 * The band is what tells a hub's line apart from somebody's, so it has to be a
	 * colour on the row and not only a different ink. Muting alone was the whole style
	 * before, and a muted line is what an ordinary message looks like in a quote.
	 */
	QVERIFY2(banded(1) > 0, "a hub notice was drawn with no band behind it");
	QCOMPARE(banded(0), 0);
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

void TestHubs::anOpenHubIsRecordedAndFollowsTheFavouritesStore()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::Favourites favourites(&config);
	for (const QString& url : favourites.urls()) favourites.remove(url);

	Qdc::RecentHubs recent(&config);
	recent.clear();

	Qdc::HubsView hubs(m_thread, &favourites, m_filelists, nullptr, &config, nullptr,
	                   &recent);

	const QString hub = QStringLiteral("adc://192.0.2.1:412");
	QVERIFY(hubs.openHub(hub));

	/* Recorded as it is opened, so a hub that never answers can still be found
	   again. Nothing here connects to anything. */
	QVERIFY(recent.contains(hub));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);
	QVERIFY(!pane->header()->isFavourite());

	/*
	 * The star follows the store, not only the click on the star: the same hub can be
	 * unbookmarked from the list on the Home view while this conversation is open.
	 */
	QVERIFY(favourites.add(hub));
	QVERIFY(pane->header()->isFavourite());

	QVERIFY(favourites.remove(hub));
	QVERIFY(!pane->header()->isFavourite());

	recent.clear();
}

namespace {

/** The Save button of the per-hub identity form, found by its text. */
QPushButton* session_save(Qdc::ChatPane* pane)
{
	for (QPushButton* button : pane->findChildren<QPushButton*>())
		if (button->text() == QStringLiteral("Save")) return button;

	return nullptr;
}

}

void TestHubs::settingsUrlDropsWhatDoesNotNameTheHub()
{
	using Qdc::HubBridge;

	const QString bare = QStringLiteral("adcs://adcs.uhub.org:1511");

	/* The keyprint is how a hub is verified, not which hub it is. */
	QCOMPARE(HubBridge::settingsUrl(bare + QStringLiteral("/?kp=SHA256/ABCDEF")), bare);
	QCOMPARE(HubBridge::settingsUrl(bare + QStringLiteral("?kp=SHA256/ABCDEF")), bare);
	QCOMPARE(HubBridge::settingsUrl(bare + QStringLiteral("/")), bare);
	QCOMPARE(HubBridge::settingsUrl(bare + QStringLiteral("#anchor")), bare);
	QCOMPARE(HubBridge::settingsUrl(bare), bare);

	/* The scheme and the port are filled in first, as they are for any address. */
	QCOMPARE(HubBridge::settingsUrl(QStringLiteral("example.org/?kp=x")),
	         QStringLiteral("dchub://example.org:411"));

	/* An IPv6 literal keeps the colons inside its brackets. */
	QCOMPARE(HubBridge::settingsUrl(QStringLiteral("adc://[2001:db8::1]:412/?kp=x")),
	         QStringLiteral("adc://[2001:db8::1]:412"));

	QVERIFY(HubBridge::settingsUrl(QString()).isEmpty());
}

void TestHubs::aKeyprintInTheAddressIsStillTheSameHubsSettings()
{
	using Qdc::ConfigStore;

	Qdc::ConfigBridge config(m_thread);

	const QString bare = QStringLiteral("adc://192.0.2.11:412");
	const QString withKeyprint = bare + QStringLiteral("/?kp=SHA256/ABCDEFGH");

	/*
	 * The same hub, reached two ways. Somebody who pastes an address carrying a
	 * keyprint must not find the nickname they set up for that hub gone - which is
	 * what happened while the settings were keyed on the address as it was typed.
	 */
	{
		Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);
		QVERIFY(hubs.openHub(bare));

		Qdc::ChatPane* pane = hubs.currentPane();
		QVERIFY(pane);

		QLineEdit* nickname =
			pane->findChild<QLineEdit*>(QStringLiteral("sessionNickname"));
		QVERIFY(nickname);

		nickname->setText(QStringLiteral("known-here"));
		emit nickname->textEdited(QStringLiteral("known-here"));

		QPushButton* save = session_save(pane);
		QVERIFY(save);
		save->click();
	}

	/* Opened by the longer address, the form arrives already filled in. */
	{
		Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);
		QVERIFY(hubs.openHub(withKeyprint));

		Qdc::ChatPane* pane = hubs.currentPane();
		QVERIFY(pane);

		QLineEdit* nickname =
			pane->findChild<QLineEdit*>(QStringLiteral("sessionNickname"));
		QVERIFY(nickname);

		QCOMPARE(nickname->text(), QStringLiteral("known-here"));
	}

	/* And there is one group, under the address without the keyprint. */
	QCOMPARE(config.getString(ConfigStore::Session, bare, QStringLiteral("Nickname")),
	         QStringLiteral("known-here"));

	QVERIFY(!config.contains(ConfigStore::Session, withKeyprint,
	                         QStringLiteral("Nickname")));

	config.remove(ConfigStore::Session, bare, QStringLiteral("Nickname"));
	config.save(ConfigStore::Session);
}

void TestHubs::aHubKeepsAnIdentityOfItsOwn()
{
	using Qdc::ConfigStore;

	Qdc::ConfigBridge config(m_thread);
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);

	const QString hub = QStringLiteral("adc://192.0.2.7:412");
	QVERIFY(hubs.openHub(hub));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);

	QLineEdit* nickname = pane->findChild<QLineEdit*>(QStringLiteral("sessionNickname"));
	QVERIFY(nickname);

	/*
	 * Typed rather than set, because setText() is not editing and the form only offers
	 * to save what somebody actually changed.
	 */
	nickname->setText(QStringLiteral("someone-else"));
	emit nickname->textEdited(QStringLiteral("someone-else"));

	QPushButton* save = session_save(pane);
	QVERIFY(save);
	QVERIFY(save->isEnabled());

	save->click();

	/*
	 * Under the hub's address, which is the group the core reads its identity from
	 * when it builds a session - so this is the value the next connection uses.
	 */
	QCOMPARE(config.getString(ConfigStore::Session, hub, QStringLiteral("Nickname")),
	         QStringLiteral("someone-else"));

	config.remove(ConfigStore::Session, hub, QStringLiteral("Nickname"));
	config.save(ConfigStore::Session);
}

void TestHubs::clearingAHubsIdentityHandsItBackToTheGeneralOne()
{
	using Qdc::ConfigStore;

	Qdc::ConfigBridge config(m_thread);

	const QString hub = QStringLiteral("adc://192.0.2.8:412");

	config.setString(ConfigStore::Session, hub, QStringLiteral("Nickname"),
	                 QStringLiteral("per-hub"));
	config.save(ConfigStore::Session);

	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);
	QVERIFY(hubs.openHub(hub));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);

	QLineEdit* nickname = pane->findChild<QLineEdit*>(QStringLiteral("sessionNickname"));
	QVERIFY(nickname);

	/* Loaded from the hub's own group, so the field arrives filled in. */
	QCOMPARE(nickname->text(), QStringLiteral("per-hub"));

	nickname->clear();
	emit nickname->textEdited(QString());

	QPushButton* save = session_save(pane);
	QVERIFY(save);
	save->click();

	/*
	 * Gone rather than stored empty, and that distinction is the whole feature: the
	 * core falls back to the identity in Settings only for a key that is absent, and
	 * reads one that is present and empty as a nickname of nothing.
	 */
	QVERIFY(!config.contains(ConfigStore::Session, hub, QStringLiteral("Nickname")));
}

void TestHubs::aUserOffersMoreThanAConversationOnASecondClick()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);

	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.9:412")));

	Qdc::HubPane* pane = qobject_cast<Qdc::HubPane*>(hubs.currentPane());
	QVERIFY(pane);

	Qdc::UserInfo user;
	user.id = QStringLiteral("SID1");
	user.nick = QStringLiteral("alice");
	user.sourceId = QStringLiteral("CID-ALICE");

	pane->users()->upsert(user);

	/*
	 * Browsing opens the same conversation as talking does, on its Files tab - so the
	 * two menu items differ in where they land rather than in what they open. Driven
	 * through the signal, since a context menu cannot be clicked from here.
	 */
	emit pane->filesRequested(user);

	Qdc::ChatPane* shown = hubs.currentPane();
	QVERIFY(shown);
	QVERIFY(shown != pane);

	QTabWidget* tabs = shown->findChild<QTabWidget*>();
	QVERIFY(tabs);

	QCOMPARE(tabs->tabText(tabs->currentIndex()), QStringLiteral("Files"));
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

void TestHubs::aPastedPictureIsOfferedLikeAChosenFile()
{
	/*
	 * Pasted through the clipboard, because that is the path a person uses and
	 * insertFromMimeData() is not public. Only where the clipboard belongs to this
	 * application: on a real desktop this would take over the one somebody is using.
	 */
	if (QGuiApplication::platformName() != QLatin1String("offscreen"))
		QSKIP("this uses the clipboard, so it runs only on the offscreen platform");

	Qdc::Composer composer;
	composer.setRichTextEnabled(true);
	composer.setSendable(true);

	QSignalSpy pasted(&composer, &Qdc::Composer::imagePasted);

	QTextEdit* edit = composer.findChild<QTextEdit*>();
	QVERIFY(edit);

	/* A screenshot, as the clipboard would hold one. */
	QImage shot(40, 30, QImage::Format_RGB32);
	shot.fill(Qt::darkGreen);

	QByteArray png;
	QBuffer sink(&png);
	QVERIFY(sink.open(QIODevice::WriteOnly));
	QVERIFY(shot.save(&sink, "PNG"));
	sink.close();

	const auto put = [](QMimeData* what) {
		/* The clipboard takes ownership. */
		QGuiApplication::clipboard()->setMimeData(what);
	};

	QMimeData* asPng = new QMimeData;
	asPng->setData(QStringLiteral("image/png"), png);
	put(asPng);

	edit->paste();

	QCOMPARE(pasted.count(), 1);

	/*
	 * The bytes as the clipboard held them, not re-encoded on the way through: a
	 * screenshot that went in as PNG comes out byte for byte.
	 */
	QCOMPARE(pasted.at(0).at(0).toByteArray(), png);
	QCOMPARE(pasted.at(0).at(1).toString(), QStringLiteral("png"));

	/* And nothing was typed into the box - a picture is not text. */
	QCOMPARE(edit->toPlainText(), QString());

	/* A bare image with no recognised form is written out as PNG instead. */
	QMimeData* bare = new QMimeData;
	bare->setImageData(shot);
	put(bare);

	edit->paste();

	QCOMPARE(pasted.count(), 2);
	QCOMPARE(pasted.at(1).at(1).toString(), QStringLiteral("png"));
	QVERIFY(!pasted.at(1).at(0).toByteArray().isEmpty());

	/* Text still pastes as text. */
	QMimeData* words = new QMimeData;
	words->setText(QStringLiteral("just words"));
	put(words);

	edit->paste();

	QCOMPARE(pasted.count(), 2);
	QCOMPARE(edit->toPlainText(), QStringLiteral("just words"));

	/*
	 * And where a message cannot say it is markdown there is nothing to paste a picture
	 * into, so it is dropped rather than half-handled.
	 */
	composer.setRichTextEnabled(false);
	QVERIFY(!composer.acceptsMedia());

	QMimeData* again = new QMimeData;
	again->setData(QStringLiteral("image/png"), png);
	put(again);

	edit->paste();
	QCOMPARE(pasted.count(), 2);
}

void TestHubs::aPastedPictureIsWrittenDownWhereItCanBeServed()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);

	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.1:412")));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);

	QImage shot(20, 10, QImage::Format_RGB32);
	shot.fill(Qt::magenta);

	QByteArray png;
	QBuffer sink(&png);
	QVERIFY(sink.open(QIODevice::WriteOnly));
	QVERIFY(shot.save(&sink, "PNG"));
	sink.close();

	/*
	 * Pasted pictures are kept beside the rest of QuickDC's own files, so the home
	 * directory is pointed at the temporary one for the length of this test rather than
	 * writing into the home of whoever is running it.
	 */
	const QByteArray realHome = qgetenv("HOME");
	qputenv("HOME", m_dir.path().toUtf8());

	const QString folder = m_dir.path() + QStringLiteral("/.quickdc/media");

	emit pane->imagePasted(png, QStringLiteral("png"));

	qputenv("HOME", realHome);

	/*
	 * A hash names a file and a peer asks for a file, so the clipboard's bytes have to
	 * land on disk before anything can be offered - and stay there for as long as the
	 * link in the message might be followed.
	 */
	const QStringList written = QDir(folder).entryList({QStringLiteral("pasted-*.png")},
	                                                   QDir::Files);

	QCOMPARE(written.size(), 1);

	/* Written whole, and not re-encoded on the way through. */
	QFile kept(folder + QLatin1Char('/') + written.first());
	QVERIFY(kept.open(QIODevice::ReadOnly));
	QCOMPARE(kept.readAll(), png);
	kept.close();
}

void TestHubs::aDroppedFileIsOfferedRatherThanNamed()
{
	if (QGuiApplication::platformName() != QLatin1String("offscreen"))
		QSKIP("this uses the clipboard, so it runs only on the offscreen platform");

	/* Two real files, because a drop carries what is there rather than a name. */
	const QString picture = m_dir.path() + QStringLiteral("/dropped.png");
	const QString document = m_dir.path() + QStringLiteral("/dropped.pdf");

	QImage made(12, 8, QImage::Format_RGB32);
	made.fill(Qt::darkYellow);
	QVERIFY(made.save(picture));

	QFile pdf(document);
	QVERIFY(pdf.open(QIODevice::WriteOnly));
	pdf.write("%PDF-1.4 not really");
	pdf.close();

	Qdc::Composer composer;
	composer.setRichTextEnabled(true);
	composer.setSendable(true);

	QSignalSpy dropped(&composer, &Qdc::Composer::filesDropped);

	QTextEdit* edit = composer.findChild<QTextEdit*>();
	QVERIFY(edit);

	QMimeData* files = new QMimeData;
	files->setUrls({QUrl::fromLocalFile(picture), QUrl::fromLocalFile(document)});
	QGuiApplication::clipboard()->setMimeData(files);

	QVERIFY2(edit->canPaste(), "a drag carrying only files would be refused");

	edit->paste();

	QCOMPARE(dropped.count(), 1);
	QCOMPARE(dropped.at(0).at(0).toStringList(), QStringList({picture, document}));

	/* And the names were not typed into the box, which is what used to happen. */
	QCOMPARE(edit->toPlainText(), QString());

	/* An address that is not a file is an address, and still goes in as text. */
	QMimeData* link = new QMimeData;
	link->setUrls({QUrl(QStringLiteral("https://example.org/page"))});
	link->setText(QStringLiteral("https://example.org/page"));
	QGuiApplication::clipboard()->setMimeData(link);

	edit->paste();

	QCOMPARE(dropped.count(), 1);
	QCOMPARE(edit->toPlainText(), QStringLiteral("https://example.org/page"));
}

void TestHubs::oldPastedPicturesAreSweptAway()
{
	/* The home directory is pointed at the temporary one, as the paste test does. */
	const QByteArray realHome = qgetenv("HOME");
	qputenv("HOME", m_dir.path().toUtf8());

	const QString folder = m_dir.path() + QStringLiteral("/.quickdc/media");
	QVERIFY(QDir().mkpath(folder));

	const auto write = [&folder](const QString& name, int daysOld) {
		const QString path = folder + QLatin1Char('/') + name;

		QFile made(path);
		if (!made.open(QIODevice::WriteOnly)) return QString();
		made.write("picture");
		made.close();

		/*
		 * Aged by hand in a second pass, and after the write: closing flushes, and the
		 * flush sets the modification time back to now - which is the only thing saying
		 * how old a picture is.
		 */
		QFile aged(path);
		if (!aged.open(QIODevice::ReadWrite)) return QString();

		aged.setFileTime(QDateTime::currentDateTime().addDays(-daysOld),
		                 QFileDevice::FileModificationTime);
		aged.close();

		return path;
	};

	const QString old = write(QStringLiteral("pasted-20240101-120000.png"), 200);
	const QString recent = write(QStringLiteral("pasted-20260101-120000.png"), 5);
	const QString other = write(QStringLiteral("something-else.png"), 400);

	QVERIFY(!old.isEmpty() && !recent.isEmpty() && !other.isEmpty());

	Qdc::ConfigBridge config(m_thread);
	config.setNumber(Qdc::ConfigStore::Application, QStringLiteral("Chat media"),
	                 QStringLiteral("Keep pasted for days"), 180);
	config.save(Qdc::ConfigStore::Application);


	{
		/* Swept when the view is built, which is when the application starts. */
		Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);
		Q_UNUSED(hubs);
	}

	QVERIFY2(!QFile::exists(old), "a picture older than the limit was kept");
	QVERIFY2(QFile::exists(recent), "a recent picture was thrown away");

	/*
	 * And only what this client wrote. The folder is QuickDC's, but deleting anything
	 * that happens to be in it would be somebody else's file gone.
	 */
	QVERIFY2(QFile::exists(other), "a file this client did not write was removed");

	/* Zero keeps everything, however old. */
	const QString again = write(QStringLiteral("pasted-20240101-130000.png"), 500);

	config.setNumber(Qdc::ConfigStore::Application, QStringLiteral("Chat media"),
	                 QStringLiteral("Keep pasted for days"), 0);
	config.save(Qdc::ConfigStore::Application);

	{
		Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);
		Q_UNUSED(hubs);
	}

	QVERIFY2(QFile::exists(again), "everything should be kept when there is no limit");

	qputenv("HOME", realHome);
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

namespace {

/** An ADC user: a session id that lasts one visit, and a client ID that does not. */
Qdc::UserInfo adcUser(const QString& sid, const QString& nick, const QString& cid)
{
	Qdc::UserInfo who;
	who.id = sid;
	who.nick = nick;
	who.sourceId = cid;

	return who;
}

/** Bring a freshly opened hub up to the point where a message could be sent. */
void logIn(Qdc::HubBridge* bridge)
{
	emit bridge->netStatusChanged(Qdc::NetStatus::Connected);
	emit bridge->hubStatusChanged(Qdc::HubStatus::LoggedIn);
}

/*
 * The one open conversation. Found among the children rather than through
 * currentPane(), because a message arriving opens a conversation without moving to
 * it - reading somebody's message is not the same as being taken to it.
 */
Qdc::DmPane* theConversation(Qdc::HubsView& hubs)
{
	const QList<Qdc::DmPane*> open = hubs.findChildren<Qdc::DmPane*>();
	return open.size() == 1 ? open.first() : nullptr;
}

}

void TestHubs::aConversationShutsWhenTheOtherPartyLeaves()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);
	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.20:412")));

	Qdc::ChatPane* hub = hubs.currentPane();
	QVERIFY(hub);

	Qdc::HubBridge* bridge = hub->hub();
	QVERIFY(bridge);

	const Qdc::UserInfo alice =
		adcUser(QStringLiteral("AAAB"), QStringLiteral("alice"),
		        QStringLiteral("CID-ALICE"));

	emit bridge->userUpdated(alice);

	Qdc::ChatMessage hello;
	hello.from = alice;
	hello.context = alice.id;
	hello.privateMessage = true;
	hello.text = QStringLiteral("are you there?");
	emit bridge->chatReceived(hello);

	Qdc::DmPane* dm = theConversation(hubs);
	QVERIFY(dm);

	/*
	 * Logged in only now: a pane reads the connection off the bridge when it is
	 * built, and the bridge only moves on events from the core - so a conversation
	 * opened after the fact would never hear the status this test is about.
	 */
	logIn(bridge);

	Qdc::Composer* composer = dm->findChild<Qdc::Composer*>();
	QVERIFY(composer);
	QVERIFY(composer->isSendable());

	/*
	 * A message goes to one person by way of the hub, so a hub that is up and has
	 * let us in is not enough: it cannot relay anything to somebody who has gone.
	 */
	emit bridge->userLeft(alice, QString(), true);

	QVERIFY(!composer->isSendable());

	/* And the band says which of the two reasons it is. */
	Qdc::ConnectionBar* band = dm->findChild<Qdc::ConnectionBar*>();
	QVERIFY(band);
	QVERIFY2(band->text().contains(QStringLiteral("alice")), qPrintable(band->text()));

	/* The hub going away as well does not un-say it. */
	emit bridge->netStatusChanged(Qdc::NetStatus::Disconnected);
	QVERIFY(!composer->isSendable());
}

void TestHubs::aConversationFollowsThemBackUnderANewNickname()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);
	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.21:412")));

	Qdc::ChatPane* hub = hubs.currentPane();
	QVERIFY(hub);

	Qdc::HubBridge* bridge = hub->hub();
	QVERIFY(bridge);

	const QString cid = QStringLiteral("CID-ALICE");
	const Qdc::UserInfo before = adcUser(QStringLiteral("AAAB"),
	                                     QStringLiteral("alice"), cid);

	emit bridge->userUpdated(before);

	Qdc::ChatMessage hello;
	hello.from = before;
	hello.context = before.id;
	hello.privateMessage = true;
	hello.text = QStringLiteral("back shortly");
	emit bridge->chatReceived(hello);

	const int panes = hubs.paneCount();
	Qdc::DmPane* dm = theConversation(hubs);
	QVERIFY(dm);

	/* As above: the conversation has to exist before the status reaches it. */
	logIn(bridge);

	Qdc::Composer* composer = dm->findChild<Qdc::Composer*>();
	QVERIFY(composer);
	QVERIFY(composer->isSendable());

	emit bridge->userLeft(before, QString(), true);
	QVERIFY(!composer->isSendable());

	/*
	 * The same person back: a new session id, a new nickname, the client ID they
	 * had before. That last one is the whole of what says it is them.
	 */
	const Qdc::UserInfo after = adcUser(QStringLiteral("AAAC"),
	                                    QStringLiteral("alice_away"), cid);

	emit bridge->userUpdated(after);

	/* The conversation carried on rather than a second one opening beside it. */
	QCOMPARE(hubs.paneCount(), panes);
	QCOMPARE(theConversation(hubs), dm);

	/* Under their new name, and writable again. */
	QCOMPARE(dm->paneTitle(), QStringLiteral("alice_away"));
	QVERIFY(composer->isSendable());

	/*
	 * And a message from the new session lands in it, which is what the re-keying is
	 * for: the conversation was filed under the session id they no longer have.
	 */
	Qdc::ChatMessage again;
	again.from = after;
	again.context = after.id;
	again.privateMessage = true;
	again.text = QStringLiteral("back now");
	emit bridge->chatReceived(again);

	QCOMPARE(hubs.paneCount(), panes);
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
	QVERIFY(m_thread->runOnCoreBlocking([] { }));
}

void TestHubs::favouritesNormaliseWhatTheyAreGiven()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::Favourites favourites(&config);
	for (const QString& url : favourites.urls()) favourites.remove(url);

	/*
	 * A public hub list names a hub however its publisher spelled it, and the header
	 * star names it the way the connection did. One favourite either way, or a hub
	 * kept from the list would read as unkept everywhere else.
	 */
	QVERIFY(favourites.add(QStringLiteral("example.org")));
	QCOMPARE(favourites.urls().at(0),
	         QStringLiteral("dchub://example.org:411"));

	QVERIFY(favourites.contains(QStringLiteral("example.org")));
	QVERIFY(favourites.contains(QStringLiteral("dchub://example.org:411")));

	/* The same hub, so not a second one. */
	QVERIFY(!favourites.add(QStringLiteral("dchub://example.org:411")));
	QCOMPARE(favourites.urls().size(), 1);

	/* And it comes off under either spelling. */
	QVERIFY(favourites.remove(QStringLiteral("example.org")));
	QVERIFY(favourites.urls().isEmpty());
}

void TestHubs::favouritesMigrateWhatWasWrittenBefore()
{
	using Qdc::ConfigStore;

	Qdc::ConfigBridge config(m_thread);

	/* A group as it would have been written before addresses were canonical. */
	config.replaceGroup(ConfigStore::Application, QStringLiteral("Favourites"),
		{QStringLiteral("Hub 0"), QStringLiteral("Hub 1"), QStringLiteral("Hub 2")},
		{{QStringLiteral("Hub 0"), QStringLiteral("example.org")},
		 {QStringLiteral("Hub 1"), QStringLiteral("dchub://example.org:411")},
		 {QStringLiteral("Total"), QStringLiteral("2")}});

	{
		Qdc::Favourites favourites(&config);

		/* Two spellings of one hub collapse into the one they both mean. */
		QCOMPARE(favourites.urls().size(), 1);
		QCOMPARE(favourites.urls().at(0),
		         QStringLiteral("dchub://example.org:411"));
	}

	/*
	 * Written back rather than only corrected in memory, or the same list would be
	 * migrated again on every start and the file would keep disagreeing with it.
	 */
	QCOMPARE(config.getString(ConfigStore::Application,
	                          QStringLiteral("Favourites"), QStringLiteral("Hub 0")),
	         QStringLiteral("dchub://example.org:411"));

	QCOMPARE(config.getNumber(ConfigStore::Application,
	                          QStringLiteral("Favourites"), QStringLiteral("Total")),
	         1);

	Qdc::Favourites cleanup(&config);
	for (const QString& url : cleanup.urls()) cleanup.remove(url);
}

void TestHubs::aPastedPageArrivesAsPlainTextWhereFormattingCannotBeSent()
{
	Qdc::Composer composer;

	QTextEdit* edit = composer.findChild<QTextEdit*>();
	QVERIFY(edit);

	QMimeData* page = new QMimeData;
	page->setHtml(QStringLiteral("<b>loud</b> and <i>slanted</i>"));
	page->setText(QStringLiteral("loud and slanted"));

	QGuiApplication::clipboard()->setMimeData(page);

	edit->paste();
	QVERIFY(Qdc::RichText::hasMarkup(*edit->document()));

	edit->clear();
	composer.setRichTextEnabled(false);
	edit->paste();

	/* The words, and nothing about how they were set. */
	QCOMPARE(edit->toPlainText(), QStringLiteral("loud and slanted"));
	QVERIFY(!Qdc::RichText::hasMarkup(*edit->document()));

	/* A fragment that offers only its markup is read for its words rather than
	   dropped. */
	QMimeData* markup = new QMimeData;
	markup->setHtml(QStringLiteral("<b>only</b> markup"));
	QGuiApplication::clipboard()->setMimeData(markup);

	edit->clear();
	edit->paste();

	QCOMPARE(edit->toPlainText(), QStringLiteral("only markup"));
	QVERIFY(!Qdc::RichText::hasMarkup(*edit->document()));

	QGuiApplication::clipboard()->clear();
}

void TestHubs::thePadlockSaysNothingUntilThereIsAConnection()
{
	Qdc::HubHeader header;

	/*
	 * Three states. Not connected is neither of the other two: an open padlock on a
	 * hub nobody has spoken to would read as "this hub is insecure" when all it means
	 * is that there is nothing to secure yet.
	 */
	header.setSecure(false);
	header.setNetStatus(Qdc::NetStatus::Invalid);

	QLabel* lock = header.findChild<QLabel*>(QStringLiteral("hubPadlock"));
	QVERIFY(lock);

	const auto lockTip = [lock] { return lock->toolTip(); };

	QVERIFY(lockTip().contains(QStringLiteral("Not connected")));

	header.setNetStatus(Qdc::NetStatus::Connected);
	QVERIFY(lockTip().contains(QStringLiteral("not using TLS")));

	header.setSecure(true);
	QVERIFY(lockTip().contains(QStringLiteral("encrypted")));
	QVERIFY(!lockTip().contains(QStringLiteral("not using TLS")));

	/* And it goes quiet again when the connection does. */
	header.setNetStatus(Qdc::NetStatus::Disconnected);
	QVERIFY(!lockTip().contains(QStringLiteral("encrypted")));
}

void TestHubs::theHeaderNamesTheHubAndKeepsTheAddress()
{
	Qdc::HubHeader header;

	const QString url = QStringLiteral("adcs://hub.example.org:412");

	/* Nothing is known yet, so the address is all there is to call it. */
	header.setHubIdentity(QString(), QString(), url);
	QCOMPARE(header.title(), url);
	QCOMPARE(header.subtitle(), QString());

	/*
	 * A name takes the title and the address drops to the line below. Not left
	 * blank: the strip would then be one line tall, two once a description arrived
	 * and one again on a hub that never sends one, and the conversation under it
	 * would move each time.
	 */
	header.setHubIdentity(QStringLiteral("The Example Hub"), QString(), url);
	QCOMPARE(header.title(), QStringLiteral("The Example Hub"));
	QCOMPARE(header.subtitle(), url);

	/* And a description displaces it, being the better thing to say. */
	header.setHubIdentity(QStringLiteral("The Example Hub"),
	                      QStringLiteral("films and arguments about them"), url);

	QCOMPARE(header.title(), QStringLiteral("The Example Hub"));
	QCOMPARE(header.subtitle(), QStringLiteral("films and arguments about them"));

	/*
	 * The address is never unreachable, whatever is on the two lines - and it is on the
	 * tooltip as markup, because that is the only way to hand Qt a string it will not
	 * read as markup itself; see RichText::literalHtml().
	 */
	QLabel* title = header.findChildren<QLabel*>().value(0);
	QVERIFY(title);
	QCOMPARE(title->toolTip(), Qdc::RichText::literalHtml(url));
}

void TestHubs::aHubsOwnDescriptionReachesThePane()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);

	const QString url = QStringLiteral("adc://192.0.2.21:412");
	QVERIFY(hubs.openHub(url));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);

	/* Until the hub has said anything, its address is all there is to call it. */
	QCOMPARE(pane->header()->title(), url);

	/*
	 * The pane reads the name and the description back off the bridge rather than
	 * off the signal, so a signal emitted by hand only pins that the two are wired
	 * to rebuild the header. What they rebuild it into is theHeaderNamesTheHub...().
	 */
	QSignalSpy described(pane->hub(), &Qdc::HubBridge::hubDescriptionChanged);
	QVERIFY(described.isValid());

	emit pane->hub()->hubDescriptionChanged(QStringLiteral("a friendly place"));
	QCOMPARE(described.count(), 1);
	QCOMPARE(pane->header()->title(), url);
}

void TestHubs::theUserListCanBeFilteredDownToAName()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);
	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.22:412")));

	Qdc::HubPane* pane = qobject_cast<Qdc::HubPane*>(hubs.currentPane());
	QVERIFY(pane);

	for (const QString& nick : {QStringLiteral("alice"), QStringLiteral("bob"),
	                            QStringLiteral("carol")})
	{
		pane->users()->upsert(user(nick));
	}

	QToolButton* reveal =
		pane->findChild<QToolButton*>(QStringLiteral("userFilterButton"));
	QVERIFY(reveal);

	QLineEdit* filter = pane->findChild<QLineEdit*>(QStringLiteral("userFilter"));
	QVERIFY(filter);
	QVERIFY(filter->isHidden());

	reveal->click();
	QVERIFY(!filter->isHidden());

	QListView* list = pane->findChild<QListView*>(QStringLiteral("userList"));
	QVERIFY(list);
	QCOMPARE(list->model()->rowCount(), 3);

	filter->setText(QStringLiteral("car"));
	QCOMPARE(list->model()->rowCount(), 1);

	/* The model itself is untouched: it is who is on the hub, not who is showing. */
	QCOMPARE(pane->users()->count(), 3);
	QCOMPARE(pane->users()->nicknames().size(), 3);
	QCOMPARE(pane->header()->findChildren<QToolButton*>().isEmpty(), false);
}

void TestHubs::hidingTheFilterPutsEverybodyBack()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);
	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.23:412")));

	Qdc::HubPane* pane = qobject_cast<Qdc::HubPane*>(hubs.currentPane());
	QVERIFY(pane);

	for (const QString& nick : {QStringLiteral("alice"), QStringLiteral("bob")})
		pane->users()->upsert(user(nick));

	QToolButton* reveal =
		pane->findChild<QToolButton*>(QStringLiteral("userFilterButton"));
	QLineEdit* filter = pane->findChild<QLineEdit*>(QStringLiteral("userFilter"));
	QListView* list = pane->findChild<QListView*>(QStringLiteral("userList"));
	QVERIFY(reveal && filter && list);

	reveal->click();
	filter->setText(QStringLiteral("ali"));
	QCOMPARE(list->model()->rowCount(), 1);

	/*
	 * A filter still narrowing a list nobody can see reads as everybody having left,
	 * so putting the field away lifts it rather than remembering it.
	 */
	reveal->click();
	QVERIFY(filter->isHidden());
	QVERIFY(filter->text().isEmpty());
	QCOMPARE(list->model()->rowCount(), 2);
}

void TestHubs::aNameThatHasLeftStillResolvesToWhatItWas()
{
	Qdc::UserModel model;

	Qdc::UserInfo alice;
	alice.id = QStringLiteral("SID1");
	alice.nick = QStringLiteral("alice");
	alice.sourceId = QStringLiteral("CID-ALICE");

	model.upsert(alice);
	QCOMPARE(model.resolve(alice).id, QStringLiteral("SID1"));

	model.remove(alice.id);
	QVERIFY(model.resolve(alice).id.isEmpty());

	/*
	 * Back under a new session id. A message keeps the one they had when they sent
	 * it, so the client id is what has to find them again.
	 */
	Qdc::UserInfo again = alice;
	again.id = QStringLiteral("SID9");
	model.upsert(again);

	QCOMPARE(model.resolve(alice).id, QStringLiteral("SID9"));
}

void TestHubs::theHubsOwnSettingsHideBehindTheGears()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);

	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.24:412")));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);

	QTabWidget* tabs = pane->findChild<QTabWidget*>();
	QVERIFY(tabs);

	const auto tabNamed = [tabs](const QString& name) {
		for (int n = 0; n < tabs->count(); ++n)
			if (tabs->tabText(n) == name) return n;

		return -1;
	};

	QCOMPARE(tabNamed(QStringLiteral("Settings")), -1);

	/* Off the strip, but built - which is what lets it keep what was typed. */
	QVERIFY(pane->findChild<QLineEdit*>(QStringLiteral("sessionNickname")));

	QToolButton* gears =
		pane->findChild<QToolButton*>(QStringLiteral("hubSettingsButton"));
	QVERIFY(gears);

	gears->click();

	const int settings = tabNamed(QStringLiteral("Settings"));
	QVERIFY(settings >= 0);
	QCOMPARE(tabs->currentIndex(), settings);

	gears->click();
	QCOMPARE(tabNamed(QStringLiteral("Settings")), -1);
	QCOMPARE(tabs->tabText(tabs->currentIndex()), QStringLiteral("Messages"));
}

void TestHubs::aConversationShowsThePersonWhereAHubShowsItsStar()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);
	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.26:412")));

	Qdc::HubPane* pane = qobject_cast<Qdc::HubPane*>(hubs.currentPane());
	QVERIFY(pane);

	Qdc::UserInfo alice = user(QStringLiteral("alice"), true);
	pane->users()->upsert(alice);

	emit pane->directMessageRequested(alice);

	Qdc::ChatPane* dm = hubs.currentPane();
	QVERIFY(dm);
	QVERIFY(dm != pane);

	QLabel* figure = dm->header()->findChild<QLabel*>(QStringLiteral("hubPresence"));
	QVERIFY(figure);
	QVERIFY(!figure->isHidden());
	QVERIFY(!figure->pixmap().isNull());

	/*
	 * The same figure the user list draws for the same person, so the two cannot
	 * come to say different things about them.
	 */
	QCOMPARE(figure->pixmap().toImage(),
	         Qdc::presence_icon(alice.away, alice.op, figure->pixmap().width()
	                            / int(figure->pixmap().devicePixelRatio()))
	             .pixmap(figure->pixmap().size()
	                     / figure->pixmap().devicePixelRatio(),
	                     figure->pixmap().devicePixelRatio())
	             .toImage());

	/*
	 * And the comparison above discriminates: the same figure for somebody away is
	 * a different image, so matching it was not matching anything at all.
	 */
	QVERIFY(Qdc::presence_icon(true, alice.op).pixmap(QSize(13, 13)).toImage()
	        != Qdc::presence_icon(false, alice.op).pixmap(QSize(13, 13)).toImage());

	/* And it stands exactly where the hub's star does, or the two headers indent
	   differently. */
	QToolButton* star =
		pane->header()->findChildren<QToolButton*>().value(0);

	QVERIFY(star);
	QCOMPARE(figure->size(), star->sizeHint());

	/* Nothing about a conversation is favourited, listed, or has settings. */
	QVERIFY(!dm->header()->findChild<QToolButton*>(
		QStringLiteral("hubSettingsButton"))->isVisibleTo(dm->header()));

	/* The hub's own settings button is in the header, beside the padlock. */
	QToolButton* gears = pane->header()->findChild<QToolButton*>(
		QStringLiteral("hubSettingsButton"));

	QVERIFY(gears);
	QVERIFY(gears->isVisibleTo(pane->header()));

	QLabel* lock = pane->header()->findChild<QLabel*>(QStringLiteral("hubPadlock"));
	QVERIFY(lock);

	QHBoxLayout* row = pane->header()->findChild<QHBoxLayout*>();
	QVERIFY(row);
	QVERIFY(row->indexOf(lock) < row->indexOf(gears));
}

void TestHubs::everyRowIsNamedByTheKindOfThingItStandsFor()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);
	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.27:412")));

	Qdc::HubPane* pane = qobject_cast<Qdc::HubPane*>(hubs.currentPane());
	QVERIFY(pane);

	Qdc::UserInfo alice = user(QStringLiteral("alice"));
	pane->users()->upsert(alice);

	emit pane->directMessageRequested(alice);

	QTreeWidget* tree = hubs.findChild<QTreeWidget*>(QStringLiteral("hubTree"));
	QVERIFY(tree);
	QCOMPARE(tree->topLevelItemCount(), 1);

	QTreeWidgetItem* hubRow = tree->topLevelItem(0);
	QCOMPARE(hubRow->childCount(), 1);

	QTreeWidgetItem* conversationRow = hubRow->child(0);

	/*
	 * The size the row was drawn at, asked of the icon rather than assumed, so this
	 * pins which glyph a row carries and not how large the tree draws one.
	 */
	const qreal ratio = hubs.devicePixelRatioF();
	const QList<QSize> stored = hubRow->icon(0).availableSizes();
	QVERIFY(!stored.isEmpty());

	const int size = qRound(stored.first().width() / ratio);
	QVERIFY(size > 0);

	const auto rowGlyph = [ratio, size](const QIcon& icon) {
		return icon.pixmap(QSize(size, size), ratio, QIcon::Normal).toImage();
	};

	/*
	 * Built the same way the tree builds one, in the ink a row is drawn in, because
	 * the row keeps a picture rather than the name of a glyph.
	 */
	const auto expected = [ratio, size](Qdc::Glyph glyph) {
		return Qdc::glyph_icon(glyph, Qdc::theme().sidebarText, ratio, size)
			.pixmap(QSize(size, size), ratio, QIcon::Normal).toImage();
	};

	QVERIFY(!rowGlyph(hubRow->icon(0)).isNull());

	QCOMPARE(rowGlyph(hubRow->icon(0)), expected(Qdc::Glyph::Hubs));
	QCOMPARE(rowGlyph(conversationRow->icon(0)), expected(Qdc::Glyph::People));

	/*
	 * And the two comparisons discriminate: a hub and a conversation are not the same
	 * picture, so matching one of them was not matching anything at all.
	 */
	QVERIFY(expected(Qdc::Glyph::Hubs) != expected(Qdc::Glyph::People));
	QVERIFY(expected(Qdc::Glyph::Board) != expected(Qdc::Glyph::Hubs));
	QVERIFY(expected(Qdc::Glyph::Board) != expected(Qdc::Glyph::People));
}

void TestHubs::theBoardGlyphIsAThirdFigureInTheSameSpace()
{
	/*
	 * Painted into a box with margin all round it, because nothing clips a glyph: one
	 * drawn outside the grid its neighbours share would overrun the row beside it, and
	 * ink on the margin here is what says it does.
	 */
	constexpr int BOX = 48;
	constexpr int MARGIN = 8;

	QPixmap sheet(BOX + MARGIN * 2, BOX + MARGIN * 2);
	sheet.fill(Qt::transparent);

	QPainter painter(&sheet);
	Qdc::paint_glyph(painter, Qdc::Glyph::Board,
	                 QRectF(MARGIN, MARGIN, BOX, BOX), Qt::black);
	painter.end();

	const QImage drawn = sheet.toImage();

	int ink = 0;

	for (int y = 0; y < drawn.height(); ++y)
	{
		for (int x = 0; x < drawn.width(); ++x)
		{
			if (qAlpha(drawn.pixel(x, y)) == 0) continue;

			++ink;

			QVERIFY(x >= MARGIN && x < MARGIN + BOX);
			QVERIFY(y >= MARGIN && y < MARGIN + BOX);
		}
	}

	/* Enough of it to be a figure at the weight of the bubbles beside it. */
	QVERIFY(ink > BOX * BOX / 5);

	/*
	 * The writing is cut out of the sheet rather than drawn over it, which is only
	 * true while the path is filled odd-even - so the middle of the figure has a hole
	 * in it, with ink on both sides.
	 */
	const int middle = MARGIN + BOX / 2;

	int first = -1;
	int last = -1;
	int hole = 0;

	for (int x = 0; x < drawn.width(); ++x)
	{
		if (qAlpha(drawn.pixel(x, middle)) == 0) continue;

		if (first < 0) first = x;
		last = x;
	}

	QVERIFY(first >= 0 && last > first);

	for (int x = first; x <= last; ++x)
		if (qAlpha(drawn.pixel(x, middle)) == 0) ++hole;

	QVERIFY(hole > 0);
}

void TestHubs::aDescribedBoardBecomesARowUnderItsHub()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);
	hubs.setBoardDirectory(boardScratch(QStringLiteral("described")));

	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.40:412")));

	Qdc::HubPane* hub = qobject_cast<Qdc::HubPane*>(hubs.currentPane());
	QVERIFY(hub && hub->hub());

	QTreeWidget* rows = hubTree(hubs);
	QVERIFY(rows);

	QTreeWidgetItem* hubRow = rows->topLevelItem(0);
	QVERIFY(hubRow);
	QCOMPARE(hubRow->childCount(), 0);

	emit hub->hub()->boardDescribed(board(QStringLiteral("general")));

	QCOMPARE(hubRow->childCount(), 1);

	QTreeWidgetItem* boardRow = hubRow->child(0);

	/* The hub's title for it, and not the name on the wire, where there is one. */
	QCOMPARE(boardRow->text(0), QStringLiteral("General discussion"));

	/*
	 * And the figure is the board's. Built the way the tree builds one and at the size
	 * the row was drawn at, because a row keeps a picture rather than the name of a
	 * glyph.
	 */
	const qreal ratio = hubs.devicePixelRatioF();
	const QList<QSize> stored = boardRow->icon(0).availableSizes();
	QVERIFY(!stored.isEmpty());

	const int size = qRound(stored.first().width() / ratio);
	QVERIFY(size > 0);

	const auto expected = [ratio, size](Qdc::Glyph glyph) {
		return Qdc::glyph_icon(glyph, Qdc::theme().sidebarText, ratio, size)
			.pixmap(QSize(size, size), ratio, QIcon::Normal).toImage();
	};

	const QImage drawn =
		boardRow->icon(0).pixmap(QSize(size, size), ratio, QIcon::Normal).toImage();

	QCOMPARE(drawn, expected(Qdc::Glyph::Board));

	/* A board is not a person, and the comparison above says which of the two it is. */
	QVERIFY(drawn != expected(Qdc::Glyph::People));
	QVERIFY(drawn != expected(Qdc::Glyph::Hubs));

	/*
	 * The pane exists and the conversation maps have not been widened to hold it: what
	 * counts panes counts the chat panes, which is the hub's own and nothing else here.
	 */
	QVERIFY(boardPane(hubs));
	QCOMPARE(hubs.paneCount(), 1);
}

void TestHubs::aBoardRowShowsItsOwnPane()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);
	hubs.setBoardDirectory(boardScratch(QStringLiteral("shown")));

	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.41:412")));

	Qdc::HubPane* hub = qobject_cast<Qdc::HubPane*>(hubs.currentPane());
	QVERIFY(hub && hub->hub());

	emit hub->hub()->boardDescribed(board(QStringLiteral("general")));

	Qdc::BoardPane* pane = boardPane(hubs);
	QVERIFY(pane);

	QStackedWidget* stack = stackOf(pane);
	QVERIFY(stack);
	QVERIFY(stack->currentWidget() != pane);

	QTreeWidget* rows = hubTree(hubs);
	rows->setCurrentItem(rows->topLevelItem(0)->child(0));

	/*
	 * Selecting a board row shows the board. A row that is not in the conversation map
	 * used to leave whatever was there before it on the screen, which reads as a row
	 * that cannot be opened.
	 */
	QCOMPARE(stack->currentWidget(), pane);

	/* And what the board says it is reached it. */
	QCOMPARE(pane->board(), QStringLiteral("general"));
	QCOMPARE(pane->boardInfo().maxSize, quint64(262144));
}

void TestHubs::aBoardThatIsGoneTakesItsRowWithIt()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);
	hubs.setBoardDirectory(boardScratch(QStringLiteral("removed")));

	const QString url = QStringLiteral("adc://192.0.2.42:412");
	QVERIFY(hubs.openHub(url));

	Qdc::HubPane* hub = qobject_cast<Qdc::HubPane*>(hubs.currentPane());
	QVERIFY(hub && hub->hub());

	emit hub->hub()->boardDescribed(board(QStringLiteral("general")));

	QTreeWidget* rows = hubTree(hubs);
	QCOMPARE(rows->topLevelItem(0)->childCount(), 1);

	rows->setCurrentItem(rows->topLevelItem(0)->child(0));
	QCOMPARE(hubs.openBoards(url), QStringList{QStringLiteral("general")});

	Qdc::BoardInfo gone;
	gone.name = QStringLiteral("general");
	gone.removed = true;

	emit hub->hub()->boardDescribed(gone);

	QCOMPARE(rows->topLevelItem(0)->childCount(), 0);
	QVERIFY(!boardPane(hubs));
	QVERIFY(hubs.openBoards(url).isEmpty());

	/* The hub's own pane is what is left showing, rather than an empty stack. */
	QCOMPARE(hubs.currentPane(), static_cast<Qdc::ChatPane*>(hub));
}

void TestHubs::aBoardRowOffersTheBoardsOwnMenu()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);
	hubs.setBoardDirectory(boardScratch(QStringLiteral("menu")));

	hubs.resize(900, 600);
	hubs.show();
	QVERIFY(QTest::qWaitForWindowExposed(&hubs));

	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.43:412")));

	Qdc::HubPane* hub = qobject_cast<Qdc::HubPane*>(hubs.currentPane());
	QVERIFY(hub && hub->hub());

	emit hub->hub()->boardDescribed(board(QStringLiteral("general")));

	QTreeWidget* rows = hubTree(hubs);
	QTreeWidgetItem* boardRow = rows->topLevelItem(0)->child(0);
	QVERIFY(boardRow);

	QStringList offered;

	/*
	 * The menu runs an event loop of its own, so the looking happens inside it - and
	 * repeatedly, because a single shot scheduled before the menu is asked for is
	 * answered before there is a menu to look at.
	 */
	QTimer watcher;
	watcher.setInterval(20);

	QObject::connect(&watcher, &QTimer::timeout, [&offered] {
		for (QWidget* top : QApplication::topLevelWidgets())
		{
			QMenu* menu = qobject_cast<QMenu*>(top);
			if (!menu || !menu->isVisible()) continue;

			for (const QAction* action : menu->actions())
				if (!action->isSeparator()) offered.append(action->text());

			menu->close();
			return;
		}
	});

	watcher.start();

	emit rows->customContextMenuRequested(rows->visualItemRect(boardRow).center());

	watcher.stop();

	QVERIFY2(!offered.isEmpty(), "the board row offered no menu at all");

	/* What can be decided about a board. */
	QVERIFY(offered.contains(QStringLiteral("Mark everything read")));
	QVERIFY(offered.contains(
		QStringLiteral("Read the board again from the beginning")));
	QVERIFY(offered.contains(QStringLiteral("Leave this board")));

	/*
	 * And not the hub's, which is the row above. Answering with that from here would
	 * offer the hub's choices from something that is not the hub.
	 */
	QVERIFY(!offered.contains(QStringLiteral("Fetch pictures on this hub")));
	QVERIFY(!offered.contains(QStringLiteral("Follow the general setting")));
}

void TestHubs::openingABoardIsWhatSubscribesToIt()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);
	hubs.setBoardDirectory(boardScratch(QStringLiteral("resume")));

	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.44:412")));

	Qdc::HubPane* hub = qobject_cast<Qdc::HubPane*>(hubs.currentPane());
	QVERIFY(hub && hub->hub());

	Qdc::BoardBridge* bulletins = hubs.bulletins();
	QVERIFY(bulletins);

	QSignalSpy stored(bulletins, &Qdc::BoardBridge::entriesStored);
	QSignalSpy states(bulletins, &Qdc::BoardBridge::boardStateReady);

	const QString first = QStringLiteral("KX3TQ7ZVN5PLQGKD3NDBK6ZTZG5PYQXSNMFYVJH");
	const QString second = QStringLiteral("7ZQGKD3NDBK6ZTZG5PYQXSNMFYVJH4TXAVN6PLQ");

	emit hub->hub()->boardEntriesReceived(
		{entry(first, 1000), entry(second, 2000)});

	QTRY_COMPARE(stored.count(), 1);

	/*
	 * Nothing has been subscribed to at this point and nothing has been asked of the
	 * hub: entries were written down, which is all that follows from them arriving.
	 */
	QCOMPARE(states.count(), 0);

	emit hub->hub()->boardDescribed(board(QStringLiteral("general")));

	/*
	 * The descriptor puts a row in the tree and reports where the board stands - a
	 * descriptor is the one thing that can turn a complete view into one with a hole
	 * in it - and subscribes to nothing.
	 */
	QTRY_COMPARE(states.count(), 1);

	Qdc::BoardPane* pane = boardPane(hubs);
	QVERIFY(pane);

	/* What was cached is read back into the pane without the hub being asked. */
	QTRY_COMPARE(pane->threads()->postCount(), 2);

	QTreeWidget* rows = hubTree(hubs);
	rows->setCurrentItem(rows->topLevelItem(0)->child(0));

	QTRY_COMPARE(states.count(), 2);

	/*
	 * From the highest timestamp held and never from one past it. Timestamps are
	 * seconds and are not unique, so a post the hub accepted in the same second as the
	 * last entry received would fall behind a cursor advanced past it and reach nobody.
	 */
	QCOMPARE(states.last().at(1).toString(), QStringLiteral("general"));
	QCOMPARE(states.last().at(2).toULongLong(), quint64(2000));

	/* And a second look at a board already subscribed to asks for nothing again. */
	rows->setCurrentItem(rows->topLevelItem(0));
	rows->setCurrentItem(rows->topLevelItem(0)->child(0));

	QCOMPARE(states.count(), 2);
}

void TestHubs::aHubWithoutBoardsForgetsWhatWasSubscribed()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);
	hubs.setBoardDirectory(boardScratch(QStringLiteral("withdrawn")));

	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.45:412")));

	Qdc::HubPane* hub = qobject_cast<Qdc::HubPane*>(hubs.currentPane());
	QVERIFY(hub && hub->hub());

	QSignalSpy states(hubs.bulletins(), &Qdc::BoardBridge::boardStateReady);

	emit hub->hub()->boardDescribed(board(QStringLiteral("general")));
	QTRY_COMPARE(states.count(), 1);

	QTreeWidget* rows = hubTree(hubs);
	rows->setCurrentItem(rows->topLevelItem(0)->child(0));

	QTRY_COMPARE(states.count(), 2);

	/*
	 * The hub saying it has no boards cancels every subscription on the connection and
	 * is the only thing that says so, so what this side believes it holds goes with it.
	 */
	emit hub->hub()->bulletinSupportChanged(false);
	QCOMPARE(states.count(), 2);

	/* Which is why looking at the board again asks for it again. */
	rows->setCurrentItem(rows->topLevelItem(0));
	rows->setCurrentItem(rows->topLevelItem(0)->child(0));

	QTRY_COMPARE(states.count(), 3);

	/*
	 * And a session that has just found the feature holds none either, so a board
	 * somebody had open is asked for rather than assumed to be still arriving.
	 */
	emit hub->hub()->bulletinSupportChanged(true);

	QTRY_COMPARE(states.count(), 4);
	QCOMPARE(states.last().at(1).toString(), QStringLiteral("general"));
}

void TestHubs::postsNobodyHasOpenedReachTheRailBadge()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);
	hubs.setBoardDirectory(boardScratch(QStringLiteral("unread")));

	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.46:412")));

	Qdc::HubPane* hub = qobject_cast<Qdc::HubPane*>(hubs.currentPane());
	QVERIFY(hub && hub->hub());

	emit hub->hub()->boardDescribed(board(QStringLiteral("general")));

	Qdc::BoardPane* pane = boardPane(hubs);
	QVERIFY(pane);

	QCOMPARE(hubs.badgeCount(), 0);

	const QString first = QStringLiteral("VN6PLQ7ZQGKD3NDBK6ZTZG5PYQXSNMFYVJH4TXA");
	const QString second = QStringLiteral("XSNMFYVJH4TXAVN6PLQ7ZQGKD3NDBK6ZTZG5PYQ");

	emit hub->hub()->boardEntriesReceived({entry(first, 3000), entry(second, 3001)});

	QTRY_COMPARE(hubs.badgeCount(), 2);

	QTreeWidget* rows = hubTree(hubs);
	QTreeWidgetItem* boardRow = rows->topLevelItem(0)->child(0);

	/* The count is on the row as well, where the hub's own name carries one. */
	QVERIFY(boardRow->text(0).endsWith(QStringLiteral("(2)")));

	/*
	 * Opening a post in the reading pane is what reads it. Not fetching it: a post
	 * whose body nobody could be found for was still read, and one fetched ahead of a
	 * reader was not.
	 */
	pane->read(first);
	QTRY_COMPARE(hubs.badgeCount(), 1);

	pane->read(second);
	QTRY_COMPARE(hubs.badgeCount(), 0);

	QCOMPARE(boardRow->text(0), QStringLiteral("General discussion"));

	/* And a post read twice is not read twice. */
	pane->read(first);
	QCOMPARE(hubs.badgeCount(), 0);
}

void TestHubs::theBoardsOpenOnAHubComeBackWithIt()
{
	using Qdc::ConfigStore;

	Qdc::ConfigBridge config(m_thread);

	const QString url = QStringLiteral("adc://192.0.2.47:412");
	const QString directory = boardScratch(QStringLiteral("session"));

	{
		Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);
		hubs.setBoardDirectory(directory);

		QVERIFY(hubs.openHub(url));

		Qdc::HubPane* hub = qobject_cast<Qdc::HubPane*>(hubs.currentPane());
		QVERIFY(hub && hub->hub());

		emit hub->hub()->boardDescribed(board(QStringLiteral("general")));
		emit hub->hub()->boardDescribed(board(QStringLiteral("dev.adc")));

		QCOMPARE(hubs.openBoards(url).size(), 2);
	}

	/* Written under the hub's address, beside the conversations, in the same shape. */
	QCOMPARE(config.getNumber(ConfigStore::Session, url, QStringLiteral("Boards")), 2);
	QCOMPARE(config.getString(ConfigStore::Session, url, QStringLiteral("Board 0")),
	         QStringLiteral("general"));

	/*
	 * The list of windows is narrowed to this one hub first. Every test that opens a
	 * hub with a configuration writes to that list, and restoring it whole here would
	 * open all of them - what is being tested is what comes back with a hub, not which
	 * hubs come back.
	 */
	config.setNumber(ConfigStore::Application, QStringLiteral("Windows"),
	                 QStringLiteral("Total"), 1);
	config.setString(ConfigStore::Application, QStringLiteral("Windows"),
	                 QStringLiteral("Hub 0"), url);
	config.save(ConfigStore::Application);

	Qdc::HubsView reopened(m_thread, nullptr, m_filelists, nullptr, &config);
	reopened.setBoardDirectory(directory);

	QVERIFY(reopened.restoreSession());

	QCOMPARE(reopened.openHubs(), QStringList{url});

	/*
	 * The rows are back, in the order they were listed, and neither of them has asked
	 * the hub for anything: what was kept is where somebody was reading, and reading is
	 * what asks a hub to go on sending.
	 */
	QCOMPARE(reopened.openBoards(url),
	         (QStringList{QStringLiteral("general"), QStringLiteral("dev.adc")}));

	QTreeWidget* rows = hubTree(reopened);
	QCOMPARE(rows->topLevelItem(0)->childCount(), 2);

	/* The name on the wire until the hub describes it, which it has not yet done. */
	QCOMPARE(rows->topLevelItem(0)->child(0)->text(0), QStringLiteral("general"));
}

void TestHubs::closingAHubLeavesNoBoardRowBehind()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);
	hubs.setBoardDirectory(boardScratch(QStringLiteral("closed")));

	const QString url = QStringLiteral("adc://192.0.2.48:412");
	QVERIFY(hubs.openHub(url));

	Qdc::HubPane* hub = qobject_cast<Qdc::HubPane*>(hubs.currentPane());
	QVERIFY(hub && hub->hub());

	emit hub->hub()->boardDescribed(board(QStringLiteral("general")));

	QTreeWidget* rows = hubTree(hubs);
	QCOMPARE(rows->topLevelItem(0)->childCount(), 1);

	rows->setCurrentItem(rows->topLevelItem(0)->child(0));

	const QString first = QStringLiteral("QGKD3NDBK6ZTZG5PYQXSNMFYVJH4TXAVN6PLQ7Z");
	emit hub->hub()->boardEntriesReceived({entry(first, 4000)});

	QTRY_COMPARE(hubs.badgeCount(), 1);

	hubs.closeHub(url);

	QCOMPARE(rows->topLevelItemCount(), 0);
	QVERIFY(!boardPane(hubs));
	QCOMPARE(hubs.badgeCount(), 0);
	QCOMPARE(hubs.paneCount(), 0);

	/*
	 * Closing a hub deletes its row, and deleting a row deletes the rows under it - so
	 * anything still keyed on one of those is keyed on a row that has gone. A colour
	 * change walks every row there is and draws it, which is where that would be found.
	 */
	QEvent palette(QEvent::PaletteChange);
	QApplication::sendEvent(&hubs, &palette);

	/* And the view still works: the same hub opens again with a board of its own. */
	QVERIFY(hubs.openHub(url));

	Qdc::HubPane* again = qobject_cast<Qdc::HubPane*>(hubs.currentPane());
	QVERIFY(again && again->hub());

	emit again->hub()->boardDescribed(board(QStringLiteral("general")));

	QCOMPARE(rows->topLevelItem(0)->childCount(), 1);
	QVERIFY(boardPane(hubs));

	hubs.closeHub(url);
}

void TestHubs::aComposedPostIsTheDocumentItsHashCovers()
{
	Qdc::ConfigBridge config(m_thread);

	const QString cid = QStringLiteral("IPJJWEPPPLCA3PF2ZCRRYO4F2ZX2EV2JMW2KC3I");

	config.setString(Qdc::ConfigStore::Application, QStringLiteral("User"),
	                 QStringLiteral("Client ID"), cid);

	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);
	hubs.setBoardDirectory(boardScratch(QStringLiteral("composed")));

	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.49:412")));

	Qdc::HubPane* hub = qobject_cast<Qdc::HubPane*>(hubs.currentPane());
	QVERIFY(hub && hub->hub());

	emit hub->hub()->boardDescribed(board(QStringLiteral("general")));

	Qdc::BoardPane* pane = boardPane(hubs);
	QVERIFY(pane);

	const QString body =
		QStringLiteral("The hub will be **offline** on Saturday.\n\nSee you then.");

	/*
	 * A post is kept beside the rest of QuickDC's own files, so the home directory is
	 * pointed at the temporary one for the length of this test rather than writing into
	 * the home of whoever is running it.
	 */
	const QByteArray realHome = qgetenv("HOME");
	qputenv("HOME", m_dir.path().toUtf8());

	emit pane->threadSubmitted(QStringLiteral("Hub upgrade on Saturday"), body, true);

	qputenv("HOME", realHome);

	/*
	 * Written down before the hub is told anything, because until the first reader has
	 * fetched it the author is the post's only source - and an author who submits a post
	 * and then cannot send it has published nothing.
	 */
	const QString folder = m_dir.path() + QStringLiteral("/.quickdc/outbox");

	const QStringList written = QDir(folder).entryList({QStringLiteral("post-*")},
	                                                   QDir::Files);
	QCOMPARE(written.size(), 1);

	QFile document(folder + QLatin1Char('/') + written.first());
	QVERIFY(document.open(QIODevice::ReadOnly));

	const QByteArray bytes = document.readAll();
	document.close();

	const int newline = bytes.indexOf('\n');
	QVERIFY(newline > 0);

	/*
	 * One ADC message, and the escaping the grammar asks for and no more: the hash is
	 * taken over every byte of this, so a document written any other way is a different
	 * post - and one a reader is required to refuse rather than repair.
	 */
	const QByteArray header = bytes.left(newline);

	QVERIFY2(header.startsWith(QByteArray("IBB0 ID") + cid.toUtf8()
	                           + " SJHub\\supgrade\\son\\sSaturday DA"),
	         qPrintable(QString::fromUtf8(header)));

	/* And the flag that says the body is the markdown subset, last as it is listed. */
	QVERIFY(header.endsWith(" RT1"));

	/*
	 * The body is everything after the first newline, raw: only the header is ADC, and
	 * the first newline is where ADC stops. So the blank line between the paragraphs is
	 * in the document as it was typed.
	 */
	QCOMPARE(bytes.mid(newline + 1), body.toUtf8());
}

void TestHubs::whatWasTypedSurvivesLeavingTheSettings()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, &config);

	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.25:412")));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);

	QToolButton* gears =
		pane->findChild<QToolButton*>(QStringLiteral("hubSettingsButton"));
	QLineEdit* nickname =
		pane->findChild<QLineEdit*>(QStringLiteral("sessionNickname"));
	QVERIFY(gears && nickname);

	gears->click();

	nickname->setText(QStringLiteral("halfway-through"));
	emit nickname->textEdited(QStringLiteral("halfway-through"));

	QPushButton* save = session_save(pane);
	QVERIFY(save);
	QVERIFY(save->isEnabled());

	/*
	 * Putting the page away is not discarding it: the tab goes, the page keeps its
	 * parent, and nothing loads the fields again.
	 */
	gears->click();
	gears->click();

	QCOMPARE(nickname->text(), QStringLiteral("halfway-through"));
	QVERIFY(save->isEnabled());
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
	config.save(Qdc::ConfigStore::Application);

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
	config.save(Qdc::ConfigStore::Application);

	const int before = queued();
	sendPicture(1024);
	QCOMPARE(queued(), before);

	/* Turned on generally, a small picture is fetched. */
	config.setBool(Qdc::ConfigStore::Application, QStringLiteral("Chat media"),
	               QStringLiteral("Fetch automatically"), true);
	config.setNumber(Qdc::ConfigStore::Application, QStringLiteral("Chat media"),
	                 QStringLiteral("Maximum size"), 2048);
	config.save(Qdc::ConfigStore::Application);

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
	config.save(Qdc::ConfigStore::Session);

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
	config.save(Qdc::ConfigStore::Session);

	Qdc::ChatMessage offered = refused;
	offered.text = QStringLiteral("[another.png](%1)")
		.arg(Qdc::RichText::writeMagnet(second));

	emit hubs.currentPane()->hub()->chatReceived(offered);
	QCOMPARE(queued(), before + 1);
}

void TestHubs::anArrivingMessageDoesNotWaitForTheCore()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::TransferBridge transfers(m_thread, this);

	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, &transfers, &config);
	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.1:412")));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);

	Qdc::RichText::Attachment picture;
	picture.tth = QStringLiteral("VN6TQGXQ7HYQ5ARWZ2LNFVQ7YQKGXQ7HYQ5ARWZ");
	picture.name = QStringLiteral("photo.png");
	picture.size = 1024;

	Qdc::ChatMessage message;
	message.hubUrl = pane->hub()->url();
	message.from = user(QStringLiteral("alice"));
	message.received = QDateTime::currentDateTime();
	message.richText = true;
	message.text = QStringLiteral("look: ![a photo](%1)")
		.arg(Qdc::RichText::writeMagnet(picture));

	/*
	 * The core thread is held, as a transfer or a finished hash holds it. Delivering a
	 * message must not wait for it: whether pictures are fetched here, how big they may
	 * be and where downloads go are all settings, and reading a setting is a blocking
	 * trip to that thread. Asked once per message, they stopped the interface for as
	 * long as the core's turn took - on a busy hub, continuously.
	 */
	m_thread->post([] { QThread::msleep(1500); });

	QElapsedTimer timer;
	timer.start();

	emit pane->hub()->chatReceived(message);

	const qint64 spent = timer.elapsed();

	QVERIFY2(spent < 100,
	         qPrintable(QStringLiteral("delivering a message took %1 ms while the core "
	                                   "was busy").arg(spent)));
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

void TestHubs::theConnectionBandOnlyShowsWhileAHubCannotBeUsed()
{
	/*
	 * Driven directly rather than through a hub: every state the band has to draw is
	 * one a real connection passes through in its own time, and half of them cannot
	 * be reached at all without a hub to refuse us.
	 */
	Qdc::ConnectionBar bar;

	QVERIFY(bar.isHidden());

	bar.setStatus(Qdc::NetStatus::Lookup, Qdc::HubStatus::None);
	QVERIFY(!bar.isHidden());
	QVERIFY(!bar.text().isEmpty());

	/* Reachable, but not yet admitted - which is not the same as being able to talk. */
	bar.setStatus(Qdc::NetStatus::TlsConnected, Qdc::HubStatus::Login);
	QVERIFY(!bar.isHidden());

	bar.setStatus(Qdc::NetStatus::TlsConnected, Qdc::HubStatus::Password);
	QVERIFY(!bar.isHidden());

	/*
	 * The one state with no band. It is what a hub looks like for all the time it is
	 * working, so anything left showing here is showing for ever.
	 */
	bar.setStatus(Qdc::NetStatus::TlsConnected, Qdc::HubStatus::LoggedIn);
	QVERIFY(bar.isHidden());
	QVERIFY(bar.text().isEmpty());

	bar.setStatus(Qdc::NetStatus::Disconnected, Qdc::HubStatus::None);
	QVERIFY(!bar.isHidden());
	QVERIFY(!bar.text().isEmpty());
}

void TestHubs::theConnectionBandCountsTheWaitDownAndForgetsIt()
{
	Qdc::ConnectionBar bar;

	bar.setStatus(Qdc::NetStatus::Disconnected, Qdc::HubStatus::None);
	const QString bare = bar.text();
	QVERIFY(!bare.isEmpty());

	/* Nothing pending says nothing about a wait, rather than a wait of no seconds. */
	bar.setReconnectIn(-1);
	QCOMPARE(bar.text(), bare);

	bar.setReconnectIn(2);
	const QString waiting = bar.text();
	QVERIFY(waiting.contains(QStringLiteral("2")));
	QVERIFY(waiting.length() > bare.length());

	/*
	 * The point of the counter: a second later it says something else. A number
	 * stated once and left alone reads as current when it is not, which is the one
	 * thing being asked of it.
	 */
	QTest::qWait(1200);
	QVERIFY(bar.text() != waiting);
	QVERIFY(bar.text().contains(QStringLiteral("1")));

	/*
	 * The attempt reports itself, and that retires the wait. Without this the band
	 * would go on offering a countdown beside a hub that is connecting.
	 */
	bar.setReconnectIn(30);
	QVERIFY(bar.text().contains(QStringLiteral("30")));

	bar.setStatus(Qdc::NetStatus::Connecting, Qdc::HubStatus::None);
	QVERIFY(!bar.text().contains(QStringLiteral("30")));

	/*
	 * A login state arriving on its own must not: it says nothing about the network,
	 * and a countdown it cancelled would be one the hub is still waiting out.
	 */
	bar.setStatus(Qdc::NetStatus::Disconnected, Qdc::HubStatus::None);
	bar.setReconnectIn(40);
	bar.setStatus(Qdc::NetStatus::Disconnected, Qdc::HubStatus::Login);
	QVERIFY(bar.text().contains(QStringLiteral("40")));
}

void TestHubs::aScheduledReconnectReachesTheBand()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);
	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.1:412")));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);

	Qdc::HubBridge* bridge = pane->hub();
	QVERIFY(bridge);

	Qdc::ConnectionBar* bar = pane->findChild<Qdc::ConnectionBar*>();
	QVERIFY(bar);

	/*
	 * Emitted in the order the core reports them - the status that ended the session,
	 * then what happens next - because the band retires a wait on a change of status
	 * and the other order would retire this one immediately.
	 */
	emit bridge->netStatusChanged(Qdc::NetStatus::ErrorConnectionRefused);
	emit bridge->reconnectScheduled(25);

	QVERIFY(!bar->isHidden());
	QVERIFY(bar->text().contains(QStringLiteral("25")));
}

/*
 * The wait can be nearly five minutes, and somebody watching often knows what
 * the backoff does not - that whatever broke the connection has been fixed. The
 * offer to stop waiting is only there while there is a wait to stop.
 */
void TestHubs::theBandOffersToStopWaitingOnlyWhileThereIsAWait()
{
	Qdc::ConnectionBar bar;

	QAbstractButton* now = bar.reconnectButton();
	QVERIFY(now);
	QVERIFY(now->isHidden());

	bar.setStatus(Qdc::NetStatus::Disconnected, Qdc::HubStatus::None);
	QVERIFY(now->isHidden());

	bar.setReconnectIn(40);
	QVERIFY(!now->isHidden());

	/* An attempt with no wait behind it is one already under way. */
	bar.setReconnectIn(-1);
	QVERIFY(now->isHidden());
}

void TestHubs::pressingReconnectNowAsksTheHub()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);
	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.1:412")));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);

	Qdc::ConnectionBar* bar = pane->findChild<Qdc::ConnectionBar*>();
	QVERIFY(bar);

	emit pane->hub()->netStatusChanged(Qdc::NetStatus::ErrorConnectionRefused);
	emit pane->hub()->reconnectScheduled(120);

	QAbstractButton* now = bar->reconnectButton();
	QVERIFY(now && !now->isHidden());

	QSignalSpy asked(bar, &Qdc::ConnectionBar::reconnectRequested);

	now->click();

	QCOMPARE(asked.count(), 1);

	/*
	 * And the offer is withdrawn as it is taken. The attempt reports a status of
	 * its own within the moment, and a button still offering to start one that
	 * is already running invites a second press.
	 */
	QVERIFY(now->isHidden());
	QVERIFY(!bar->text().contains(QStringLiteral("120")));
}

namespace {

/** The bidirectional override, and the marker a reduced string puts in its place. */
const QChar RLO(0x202e);
const QChar MARKER(QChar::ReplacementCharacter);

/** The one row of the hub tree, and the conversations under it. */
QTreeWidgetItem* hubRow(Qdc::HubsView& hubs)
{
	QTreeWidget* tree = hubs.findChild<QTreeWidget*>(QStringLiteral("hubTree"));
	if (!tree || tree->topLevelItemCount() != 1) return nullptr;

	return tree->topLevelItem(0);
}

}

/*
 * A hub's own name is drawn in the narrowest list in the window, and RTF0 requires
 * that such a name cannot be mistaken for our own text. An override in it would
 * otherwise reverse the row it is drawn in.
 */
void TestHubs::aDoctoredHubNameCannotReverseItsRow()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);

	/*
	 * In the address rather than in the name the hub sends, because a name only arrives
	 * from a hub there is somebody to talk to - and the row draws whichever of the two
	 * it has through the one call. An address like this comes from a clicked link.
	 *
	 * Long, as well as doctored, so that the reduction has to cut it: the count of what
	 * is unread is our own text and must survive being put beside a name that fills the
	 * bound, which it only does if the name is reduced before the count is added.
	 */
	const QString url = QStringLiteral("adc://192.0.2.60:412/") + RLO
		+ QString(200, QLatin1Char('i'));

	QVERIFY(hubs.openHub(url));

	QTreeWidgetItem* row = hubRow(hubs);
	QVERIFY(row);

	const QString shown = Qdc::RichText::displayText(url);
	QVERIFY2(shown != url, "the reduction left this address alone");

	QCOMPARE(row->text(0), shown);
	QCOMPARE(row->toolTip(0), Qdc::RichText::literalHtml(shown));

	/* Marked as doctored rather than quietly repaired, and not still reversing. */
	QVERIFY(shown.contains(MARKER));
	QVERIFY(!row->text(0).contains(RLO));

	/* Hovering it is not the way round the reduction either. */
	QVERIFY(!row->toolTip(0).contains(RLO));

	Qdc::ChatMessage said;
	said.from = user(QStringLiteral("alice"));
	said.received = QDateTime::currentDateTime();
	said.text = QStringLiteral("anybody there?");

	emit hubs.currentPane()->hub()->chatReceived(said);

	/* The count is still on the end of it, where a name cut to the bound would be. */
	QCOMPARE(row->text(0), shown + QStringLiteral(" (1)"));
}

void TestHubs::aNicknameIsReducedInItsRowAndItsTooltip()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);
	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.61:412")));

	Qdc::HubPane* pane = qobject_cast<Qdc::HubPane*>(hubs.currentPane());
	QVERIFY(pane);

	/*
	 * A control character and a tab: one goes, the other becomes the space it was, so
	 * a name written on two lines is one line here rather than two rows tall.
	 */
	const Qdc::UserInfo alice = user(QStringLiteral("al\x01ice\tb"));
	pane->users()->upsert(alice);

	emit pane->directMessageRequested(alice);

	QTreeWidgetItem* hub = hubRow(hubs);
	QVERIFY(hub);
	QCOMPARE(hub->childCount(), 1);

	QTreeWidgetItem* row = hub->child(0);

	QCOMPARE(row->text(0), QStringLiteral("alice b"));
	QCOMPARE(row->toolTip(0),
	         Qdc::RichText::literalHtml(QStringLiteral("alice b")));

	/* And the name itself was never touched: it is who the conversation is with. */
	Qdc::DmPane* dm = theConversation(hubs);
	QVERIFY(dm);
	QCOMPARE(dm->peer().nick, alice.nick);
}

/*
 * The half of the requirement that is not about a row: a name interpolated raw into
 * one of our own sentences gets to finish that sentence, because an override reverses
 * everything drawn after it.
 */
void TestHubs::aRemoteNameInOneOfOurSentencesIsReduced()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);
	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.62:412")));

	Qdc::ChatPane* hub = hubs.currentPane();
	QVERIFY(hub);

	Qdc::HubBridge* bridge = hub->hub();
	QVERIFY(bridge);

	const Qdc::UserInfo alice =
		adcUser(QStringLiteral("AAAB"), QStringLiteral("alice") + RLO,
		        QStringLiteral("CID-ALICE"));

	emit bridge->userUpdated(alice);

	Qdc::ChatMessage hello;
	hello.from = alice;
	hello.context = alice.id;
	hello.privateMessage = true;
	hello.text = QStringLiteral("are you there?");
	emit bridge->chatReceived(hello);

	Qdc::DmPane* dm = theConversation(hubs);
	QVERIFY(dm);

	/* As elsewhere: the conversation has to exist before the status reaches it. */
	logIn(bridge);

	emit bridge->userLeft(alice, QString(), true);

	/* The band across the top of the conversation. */
	Qdc::ConnectionBar* band = dm->findChild<Qdc::ConnectionBar*>();
	QVERIFY(band);
	QVERIFY2(band->text().contains(MARKER), qPrintable(band->text()));
	QVERIFY(!band->text().contains(RLO));

	/* Why nothing can be typed, which the box says in our words. */
	QTextEdit* edit = dm->findChild<QTextEdit*>();
	QVERIFY(edit);
	QVERIFY(edit->placeholderText().contains(MARKER));
	QVERIFY(!edit->placeholderText().contains(RLO));

	/* And the line written into the conversation as a record of it. */
	Qdc::ChatFeed* feed = dm->findChild<Qdc::ChatFeed*>();
	QVERIFY(feed && feed->model());

	QAbstractItemModel* lines = feed->model();
	QVERIFY(lines->rowCount() > 0);

	const Qdc::ChatMessage last =
		lines->data(lines->index(lines->rowCount() - 1, 0),
		            Qdc::ChatModel::MessageRole).value<Qdc::ChatMessage>();

	QVERIFY2(last.text.contains(QStringLiteral("has left the hub")),
	         qPrintable(last.text));

	QVERIFY(last.text.contains(MARKER));
	QVERIFY(!last.text.contains(RLO));
}

void TestHubs::aFileNameInAMessageBoxIsReduced()
{
	Qdc::HubsView hubs(m_thread, nullptr, m_filelists, nullptr, nullptr);
	QVERIFY(hubs.openHub(QStringLiteral("adc://192.0.2.63:412")));

	Qdc::ChatPane* pane = hubs.currentPane();
	QVERIFY(pane);

	/*
	 * The name a sender put in a magnet, which is the one thing in an attachment that
	 * they chose the whole of. Reversed, "photo<RLO>gpj.exe" reads as "photoexe.jpg".
	 */
	Qdc::RichText::Attachment file;
	file.tth = QStringLiteral("QKGXQ7HYQ5ARWZ2LNFVQ7YVN6TQGXQ7HYQ5ARWZ");
	file.name = QStringLiteral("photo") + RLO + QStringLiteral("gpj.exe");
	file.size = 1024;

	/*
	 * Read from a timer, because a message box runs its own event loop: nothing here
	 * gets another turn until it has been dismissed, so what it says has to be read
	 * from inside it.
	 */
	QString said;

	QTimer::singleShot(200, [&said] {
		for (QWidget* top : QApplication::topLevelWidgets())
		{
			QMessageBox* box = qobject_cast<QMessageBox*>(top);
			if (!box || !box->isVisible()) continue;

			said = box->text();
			box->accept();
		}
	});

	/* No transfer manager here, which is the message this one has to give. */
	emit pane->linkActivated(QUrl(Qdc::RichText::writeMagnet(file)));

	QTest::qWait(1000);

	QVERIFY2(!said.isEmpty(), "no message box was on screen");
	QVERIFY2(said.contains(MARKER), qPrintable(said));
	QVERIFY(!said.contains(RLO));
}

/*
 * The hanging indent is the width of the speaker's name, so the value it is measured
 * from and the value drawn have to be the one value - otherwise every line after the
 * first sits under nothing, by however much a stranger put in their name.
 */
void TestHubs::theFeedMeasuresTheSpeakerItDraws()
{
	Qdc::ChatModel model;

	const QString sprawling = QString(200, QLatin1Char('i'));
	const QString reduced = Qdc::RichText::displayText(sprawling);
	QVERIFY2(reduced != sprawling, "the reduction left this name alone");

	Qdc::ChatMessage said;
	said.from = user(sprawling);
	said.received = QDateTime::currentDateTime();
	said.text = QStringLiteral("a line with enough words in it to wrap around "
	                           "several times over, whatever room it is given");
	model.append(said);

	/* The same line, from somebody already called what the reduction calls them. */
	Qdc::ChatMessage same = said;
	same.from = user(reduced);
	model.append(same);

	/* And from somebody with a short name, as the yardstick for the indent itself. */
	Qdc::ChatMessage brief = said;
	brief.from = user(QStringLiteral("al"));
	model.append(brief);

	Qdc::ChatDelegate delegate;

	QStyleOptionViewItem option;
	option.rect = QRect(0, 0, 900, 0);
	option.font = QApplication::font();
	option.palette = QApplication::palette();

	/* What is drawn is the reduced name, prefix and all. */
	QCOMPARE(delegate.plainText(model.index(0), option),
	         delegate.plainText(model.index(1), option));

	const int tall = delegate.sizeHint(option, model.index(0)).height();
	const int shortRow = delegate.sizeHint(option, model.index(2)).height();

	/*
	 * The indent is what the text column is measured from, so a long name really does
	 * make a taller row - which is what says the equality below is worth anything.
	 */
	QVERIFY2(tall > shortRow, qPrintable(QStringLiteral("a cut name measured %1, a "
	                                                   "short one %2")
	                                     .arg(tall).arg(shortRow)));

	QCOMPARE(tall, delegate.sizeHint(option, model.index(1)).height());
}

#include "tst_hubs.moc"

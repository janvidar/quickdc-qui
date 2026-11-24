/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <gui/boardmodel.h>
#include <gui/boardpane.h>
#include <gui/composer.h>
#include <gui/richtext.h>

#include <QAbstractButton>
#include <QDateTime>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTest>
#include <QTimer>
#include <QTreeView>

/**
 * Bulletin boards: how an index is grouped into threads, and how honestly a post is
 * drawn before, during and after the document behind it has been read.
 *
 * Nothing here needs a hub, and that is the point of the two widgets rather than an
 * accident of the test. Entries, descriptors, bodies, refusals and tombstones all arrive
 * through slots, so every state a post can be in - including the three that only happen
 * when something goes wrong - is reachable from here in a line or two.
 *
 * The values fed in are Qdc::BoardEntry and Qdc::BoardPost, which is what the bridge
 * emits, so what is exercised is the same path a real entry takes from the point it
 * becomes a Qt value.
 */
class TestBoards : public QObject {
	Q_OBJECT

private slots:
	void siblingsAreOrderedByTheHubsTimestamp();
	void theAuthorsClaimedDateChangesNoOrder();
	void aTieOnTimestampIsBrokenTheSameWayEveryTime();
	void aTieSurvivesTheTreeBeingRebuilt();

	void aReplyIsGroupedUnderTheParentItNames();
	void groupingIsRebuiltFromTheParentWhereTheThreadDisagrees();
	void anOrphanIsKeptAndAttachedToItsThread();
	void anOrphanWithNoThreadHeldStandsOnItsOwn();
	void aParentThatIsNotHeldCanBeAskedFor();
	void displayDepthIsBoundedOnADeepThread();
	void entriesNamingEachOtherAsParentAreStillDrawn();

	void aWithdrawnEntryLeavesAndItsRepliesStay();
	void aReplayedEntryFromTheCursorsSecondIsIgnored();
	void anEntryForAnotherBoardIsRefused();

	void anImplausibleFutureTimestampIsAnUnknownTime();
	void aSubjectOfInvisibleCharactersFallsBackToOurWording();
	void aSubjectCarryingABidirectionalOverrideIsDrawnReduced();

	void aReadPostIsItsSubjectItsAuthorAndItsWords();
	void nothingIsSaidBetweenASubjectAndItsWords();
	void anAnnouncedPostIsNotDrawnAsThoughItHadBeenRead();
	void aPostWhoseDocumentIsHereIsNotDrawnAsUnfetched();
	void aDisputedPostIsAttributedToTheIndexAndNotToTheDocument();
	void aDisputedPostIsListedAndReadLikeAnyOther();
	void aDocumentThatIsNotThePostItClaimsToBeIsNotListed();
	void aPostNobodyCouldBeFoundForIsListedAndDimmed();
	void aPlainBodyIsNeverRenderedAsMarkdown();
	void theWordDeleteAppearsNowhereInThePane();

	void theThreadListDrawsNoParentOrDocumentColumn();
	void permissionsDecideWhatIsOffered();
	void withdrawingIsAskedAboutInPlainWords();
	void aRefusalIsReportedInThePane();
	void thePostBeingWrittenTakesTheRoomItIsGiven();
	void aRefusalCanBeDismissedAndGoesByItself();
	void aGapThatCannotBeClosedIsSaidSo();
	void writingAPostTakesTheReadingAreaAndGivesItBack();
	void aReplyOpensQuotingThePostItAnswers();
	void aSubmittedThreadIsNotPresentedAsPublished();
	void quotingAFormattedPostKeepsItsFormatting();
	void whatWasWrittenSurvivesAPostBeingRefused();
	void returnMakesAParagraphInAPostAndSendsAMessage();
	void aReplyKeepsTheParentItWasBegunOn();
	void aReplyToAPostWithdrawnWhileWritingIsNotSent();
	void aDescriptorForAnotherBoardStartsAgain();
	void theBoardIsNotSomethingAReaderRefreshes();
	void thePaneSaysWhichPostItIsShowing();
	void aPictureInAPostIsDrawnOnceItsFileIsHere();

private:
	/** One index entry, with only what a given test cares about spelled out. */
	static Qdc::BoardEntry entry(const QString& tth, quint64 timestamp,
	                             const QString& parent = QString(),
	                             const QString& thread = QString(),
	                             const QString& subject = QStringLiteral("A subject"),
	                             const QString& cid = QStringLiteral("CIDONE"),
	                             const QString& nick = QStringLiteral("alice"));

	/** A tombstone: a root, a board, a later timestamp and nothing else. */
	static Qdc::BoardEntry tombstone(const QString& tth, quint64 timestamp);

	/** A document that hashed to what was asked for. */
	static Qdc::BoardPost post(const QString& tth, const QString& body,
	                           bool richText = false,
	                           const QString& cid = QStringLiteral("CIDONE"),
	                           const QString& subject = QStringLiteral("A subject"));

	/** The roots of every row of a subtree, depth first, in the order they are drawn. */
	static QStringList drawn(const Qdc::BoardModel& model,
	                         const QModelIndex& parent = QModelIndex());

	static QString cell(const Qdc::BoardModel& model, const QString& tth,
	                    Qdc::BoardModel::Column column);

	static QVariant role(const Qdc::BoardModel& model, const QString& tth, int role);

	static QLabel* label(Qdc::BoardPane& pane, const char* name);
	static QPushButton* button(Qdc::BoardPane& pane, const char* name);

	/**
	 * The two bands across the top, in the order they are built: the gap, then the
	 * refusal. Both carry the name the theme colours a band's text by, which is what a
	 * band is; nothing else in the pane uses it.
	 */
	static QList<QLabel*> bands(Qdc::BoardPane& pane);

	/** A timestamp this machine's clock finds ordinary. */
	static quint64 base();
};

Qdc::BoardEntry TestBoards::entry(const QString& tth, quint64 timestamp,
                                  const QString& parent, const QString& thread,
                                  const QString& subject, const QString& cid,
                                  const QString& nick)
{
	Qdc::BoardEntry held;
	held.tth = tth;
	held.size = 412;
	held.board = QStringLiteral("general");
	held.cid = cid;
	held.nick = nick;
	held.parent = parent;
	held.thread = thread.isEmpty() ? (parent.isEmpty() ? tth : parent) : thread;
	held.subject = subject;
	held.timestamp = timestamp;

	return held;
}

Qdc::BoardEntry TestBoards::tombstone(const QString& tth, quint64 timestamp)
{
	Qdc::BoardEntry stone;
	stone.tth = tth;
	stone.board = QStringLiteral("general");
	stone.timestamp = timestamp;
	stone.removed = true;

	return stone;
}

Qdc::BoardPost TestBoards::post(const QString& tth, const QString& body, bool richText,
                                const QString& cid, const QString& subject)
{
	Qdc::BoardPost document;
	document.tth = tth;
	document.cid = cid;
	document.subject = subject;
	document.body = body;
	document.richText = richText;
	document.state = Qdc::PostState::Verified;

	return document;
}

QStringList TestBoards::drawn(const Qdc::BoardModel& model, const QModelIndex& parent)
{
	QStringList rows;

	for (int row = 0; row < model.rowCount(parent); ++row)
	{
		const QModelIndex index = model.index(row, 0, parent);

		rows.append(model.tthAt(index));
		rows.append(drawn(model, index));
	}

	return rows;
}

QString TestBoards::cell(const Qdc::BoardModel& model, const QString& tth,
                         Qdc::BoardModel::Column column)
{
	const QModelIndex index = model.indexOf(tth);

	return model.data(index.sibling(index.row(), column)).toString();
}

QVariant TestBoards::role(const Qdc::BoardModel& model, const QString& tth, int which)
{
	return model.data(model.indexOf(tth), which);
}

QLabel* TestBoards::label(Qdc::BoardPane& pane, const char* name)
{
	return pane.findChild<QLabel*>(QString::fromLatin1(name));
}

QPushButton* TestBoards::button(Qdc::BoardPane& pane, const char* name)
{
	return pane.findChild<QPushButton*>(QString::fromLatin1(name));
}

QList<QLabel*> TestBoards::bands(Qdc::BoardPane& pane)
{
	return pane.findChildren<QLabel*>(QStringLiteral("connectionBarText"));
}

quint64 TestBoards::base()
{
	/* An hour ago, so that everything derived from it is a time and not a doubt. */
	return static_cast<quint64>(QDateTime::currentSecsSinceEpoch()) - 3600;
}

void TestBoards::siblingsAreOrderedByTheHubsTimestamp()
{
	Qdc::BoardModel model;

	const QString root = QStringLiteral("ROOT");

	model.add(entry(root, base()));
	model.add(entry(QStringLiteral("LATE"), base() + 20, root));
	model.add(entry(QStringLiteral("EARLY"), base() + 10, root));

	const QModelIndex thread = model.indexOf(root);
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(model.rowCount(thread), 2);

	/* Arrival order is not the order: the hub's timestamp is. */
	QCOMPARE(drawn(model), QStringList({root, QStringLiteral("EARLY"),
	                                    QStringLiteral("LATE")}));
}

void TestBoards::theAuthorsClaimedDateChangesNoOrder()
{
	Qdc::BoardPane pane;

	const QString root = QStringLiteral("ROOT");

	pane.addEntry(entry(root, base()));
	pane.addEntry(entry(QStringLiteral("FIRST"), base() + 10, root));
	pane.addEntry(entry(QStringLiteral("SECOND"), base() + 20, root));

	/*
	 * The claimed composition dates are the reverse of the timestamps, and nothing about
	 * the board moves. The date is the author's own word, nothing verifies it, and it is
	 * the one thing the specification forbids ordering by - so it is not even carried on
	 * an index entry, and reaches this pane only inside a document.
	 */
	for (const auto& pair : {std::pair<const char*, quint64>{"FIRST", base() + 5000},
	                         std::pair<const char*, quint64>{"SECOND", base() + 1}})
	{
		const QString tth = QString::fromLatin1(pair.first);

		pane.read(tth);

		Qdc::BoardPost document = post(tth, QStringLiteral("Body"));
		document.date = pair.second;
		document.dated = true;

		pane.setPost(document);
	}

	QCOMPARE(drawn(*pane.threads()),
	         QStringList({root, QStringLiteral("FIRST"), QStringLiteral("SECOND")}));
}

void TestBoards::aTieOnTimestampIsBrokenTheSameWayEveryTime()
{
	const QString root = QStringLiteral("ROOT");
	const QString one = QStringLiteral("AAAA");
	const QString two = QStringLiteral("BBBB");

	/*
	 * Two boards holding the same three entries, fed in opposite orders. A timestamp
	 * collision is ordinary on a busy board, and the tie is broken on the root - which
	 * is the same in both sessions, where the order the entries happened to arrive in is
	 * not. This is the thread not rearranging itself between one session and the next.
	 */
	Qdc::BoardModel forwards;
	forwards.add(entry(root, base()));
	forwards.add(entry(one, base() + 10, root));
	forwards.add(entry(two, base() + 10, root));

	Qdc::BoardModel backwards;
	backwards.add(entry(root, base()));
	backwards.add(entry(two, base() + 10, root));
	backwards.add(entry(one, base() + 10, root));

	QCOMPARE(drawn(forwards), QStringList({root, one, two}));
	QCOMPARE(drawn(backwards), drawn(forwards));
}

void TestBoards::aTieSurvivesTheTreeBeingRebuilt()
{
	Qdc::BoardModel model;

	const QString root = QStringLiteral("ROOT");
	const QString one = QStringLiteral("AAAA");
	const QString two = QStringLiteral("BBBB");

	model.add(entry(root, base()));
	model.add(entry(two, base() + 10, root));
	model.add(entry(one, base() + 10, root));

	const QStringList before = drawn(model);

	/* Any arriving entry rebuilds the tree, and the tie must come out the same way. */
	model.add(entry(QStringLiteral("ZZZZ"), base() + 30, root));

	QCOMPARE(drawn(model), QStringList({root, one, two, QStringLiteral("ZZZZ")}));
	QCOMPARE(drawn(model).mid(0, before.size()), before);
}

void TestBoards::aReplyIsGroupedUnderTheParentItNames()
{
	Qdc::BoardModel model;

	const QString root = QStringLiteral("ROOT");
	const QString reply = QStringLiteral("REPLY");
	const QString deeper = QStringLiteral("DEEPER");

	model.add(entry(root, base()));
	model.add(entry(reply, base() + 10, root));
	model.add(entry(deeper, base() + 20, reply));

	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(model.rowCount(model.indexOf(reply)), 1);
	QCOMPARE(model.threadOf(deeper), root);
	QCOMPARE(role(model, deeper, Qdc::BoardModel::DepthRole).toInt(), 2);
	QVERIFY(!role(model, deeper, Qdc::BoardModel::MissingParentRole).toBool());
}

void TestBoards::groupingIsRebuiltFromTheParentWhereTheThreadDisagrees()
{
	Qdc::BoardModel model;

	const QString root = QStringLiteral("ROOT");
	const QString other = QStringLiteral("OTHER");
	const QString reply = QStringLiteral("REPLY");

	model.add(entry(root, base()));
	model.add(entry(other, base() + 5));

	/*
	 * The hub says this reply belongs to the other thread and its parent says otherwise.
	 * The thread root is the hub's convenience and is never verified; the parent is
	 * inside the document's hash and is the load-bearing link, so the parent wins.
	 */
	model.add(entry(reply, base() + 10, root, other));

	QCOMPARE(model.parent(model.indexOf(reply)), model.indexOf(root));
	QCOMPARE(model.threadOf(reply), root);
	QVERIFY(!role(model, reply, Qdc::BoardModel::MissingParentRole).toBool());
}

void TestBoards::anOrphanIsKeptAndAttachedToItsThread()
{
	Qdc::BoardModel model;

	const QString root = QStringLiteral("ROOT");
	const QString absent = QStringLiteral("ABSENT");
	const QString orphan = QStringLiteral("ORPHAN");

	model.add(entry(root, base()));

	/*
	 * A reply whose parent is not held: withdrawn, on a board this session cannot read,
	 * or from another hub. It is never discarded - it goes at the top level of the thread
	 * the hub named, and says that its parent is not here.
	 */
	model.add(entry(orphan, base() + 10, absent, root));

	QCOMPARE(model.postCount(), 2);
	QCOMPARE(model.parent(model.indexOf(orphan)), model.indexOf(root));
	QVERIFY(role(model, orphan, Qdc::BoardModel::MissingParentRole).toBool());

	/*
	 * And it is marked on the row rather than in a column of words beside the subject: a
	 * reply drawn somewhere other than under the post it names is slanted, and what is
	 * odd about it is in the tooltip for whoever wants to know.
	 */
	QVERIFY(role(model, orphan, Qt::FontRole).value<QFont>().italic());
	QVERIFY(!role(model, root, Qt::FontRole).value<QFont>().italic());

	QCOMPARE(model.missingParents(), QStringList({absent}));

	/* And it is adopted the moment its parent turns up. */
	model.add(entry(absent, base() + 2, root));

	QCOMPARE(model.parent(model.indexOf(orphan)), model.indexOf(absent));
	QVERIFY(!role(model, orphan, Qdc::BoardModel::MissingParentRole).toBool());
	QVERIFY(model.missingParents().isEmpty());
}

void TestBoards::anOrphanWithNoThreadHeldStandsOnItsOwn()
{
	Qdc::BoardModel model;

	const QString orphan = QStringLiteral("ORPHAN");

	model.add(entry(orphan, base(), QStringLiteral("ABSENT"),
	                QStringLiteral("ALSOABSENT")));

	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(model.threadOf(orphan), orphan);
	QVERIFY(role(model, orphan, Qdc::BoardModel::MissingParentRole).toBool());
}

void TestBoards::aParentThatIsNotHeldCanBeAskedFor()
{
	Qdc::BoardPane pane;

	const QString orphan = QStringLiteral("ORPHAN");
	const QString absent = QStringLiteral("ABSENT");

	pane.addEntry(entry(orphan, base(), absent));
	pane.read(orphan);

	QPushButton* ask = button(pane, "boardParent");
	QVERIFY(ask);
	QVERIFY(!ask->isHidden());

	QSignalSpy wanted(&pane, &Qdc::BoardPane::parentRequested);
	ask->click();

	QCOMPARE(wanted.count(), 1);
	QCOMPARE(wanted.first().first().toString(), absent);
}

void TestBoards::displayDepthIsBoundedOnADeepThread()
{
	Qdc::BoardModel model;

	QVector<Qdc::BoardEntry> chain;
	const int length = 40;

	for (int step = 0; step < length; ++step)
	{
		chain.append(entry(QStringLiteral("POST%1").arg(step, 3, 10, QLatin1Char('0')),
		                   base() + static_cast<quint64>(step),
		                   step ? QStringLiteral("POST%1")
		                   	.arg(step - 1, 3, 10, QLatin1Char('0'))
		                        : QString()));
	}

	model.add(chain);

	/* Nothing is dropped for being deep. */
	QCOMPARE(model.postCount(), length);
	QCOMPARE(drawn(model).size(), length);

	int deepest = 0;
	int flattened = 0;

	for (const Qdc::BoardEntry& one : chain)
	{
		deepest = qMax(deepest, role(model, one.tth, Qdc::BoardModel::DepthRole).toInt());

		if (role(model, one.tth, Qdc::BoardModel::FlattenedRole).toBool()) ++flattened;
	}

	QCOMPARE(deepest, Qdc::BoardModel::MaximumDepth);
	QCOMPARE(flattened, length - Qdc::BoardModel::MaximumDepth - 1);

	/* The tail is a flat run under the deepest row that still nests, in order. */
	const QString host = QStringLiteral("POST%1")
		.arg(Qdc::BoardModel::MaximumDepth - 1, 3, 10, QLatin1Char('0'));

	QCOMPARE(model.rowCount(model.indexOf(host)), length - Qdc::BoardModel::MaximumDepth);
}

void TestBoards::entriesNamingEachOtherAsParentAreStillDrawn()
{
	Qdc::BoardModel model;

	/*
	 * A parent hash names a document that existed first, so a real reply graph has no
	 * cycle - but the parent on an index entry is a field the hub copied and could have
	 * invented, and a pair of entries naming each other must not take either of them off
	 * the board or send the walk round for ever.
	 */
	model.add(entry(QStringLiteral("AAAA"), base(), QStringLiteral("BBBB")));
	model.add(entry(QStringLiteral("BBBB"), base() + 1, QStringLiteral("AAAA")));

	QCOMPARE(model.postCount(), 2);
	QCOMPARE(drawn(model).size(), 2);
}

void TestBoards::aWithdrawnEntryLeavesAndItsRepliesStay()
{
	Qdc::BoardModel model;

	const QString root = QStringLiteral("ROOT");
	const QString reply = QStringLiteral("REPLY");

	model.add(entry(root, base()));
	model.add(entry(reply, base() + 10, root));

	model.add(tombstone(root, base() + 20));

	/* The post leaves the view; the reply is a separate post and does not. */
	QVERIFY(!model.indexOf(root).isValid());
	QCOMPARE(model.postCount(), 1);
	QCOMPARE(drawn(model), QStringList({reply}));

	QVERIFY(model.holds(root));
	QVERIFY(model.isWithdrawn(root));
	QCOMPARE(model.stateOf(root), Qdc::PostState::Withdrawn);

	QVERIFY(role(model, reply, Qdc::BoardModel::WithdrawnParentRole).toBool());
	QVERIFY(!role(model, reply, Qdc::BoardModel::MissingParentRole).toBool());

	/* Marked the same way as any other reply whose parent is not on the board. */
	QVERIFY(role(model, reply, Qt::FontRole).value<QFont>().italic());

	/* A parent that was withdrawn is not one to go asking the hub for. */
	QVERIFY(model.missingParents().isEmpty());
}

void TestBoards::aReplayedEntryFromTheCursorsSecondIsIgnored()
{
	Qdc::BoardModel model;

	const QString root = QStringLiteral("ROOT");

	model.add(entry(root, base(), QString(), QString(), QStringLiteral("As accepted")));

	/*
	 * Resuming replays the whole second the cursor sits in, so the entries of that
	 * second arrive twice. An equal timestamp means the copy is dropped; a greater one -
	 * which is what a tombstone carries - means it takes effect.
	 */
	model.add(entry(root, base(), QString(), QString(), QStringLiteral("Rewritten")));

	QCOMPARE(model.postCount(), 1);
	QCOMPARE(model.entryOf(root).subject, QStringLiteral("As accepted"));

	model.add(tombstone(root, base() + 1));
	QVERIFY(model.isWithdrawn(root));
}

void TestBoards::anEntryForAnotherBoardIsRefused()
{
	Qdc::BoardModel model;
	model.setBoard(QStringLiteral("general"));

	model.add(entry(QStringLiteral("HERE"), base()));

	Qdc::BoardEntry elsewhere = entry(QStringLiteral("THERE"), base() + 1);
	elsewhere.board = QStringLiteral("announcements");

	model.add(elsewhere);

	/* One hub's assertions about one board; holding two of them makes a third board. */
	QCOMPARE(model.postCount(), 1);
	QVERIFY(!model.holds(QStringLiteral("THERE")));
}

void TestBoards::anImplausibleFutureTimestampIsAnUnknownTime()
{
	Qdc::BoardModel model;

	const quint64 far = static_cast<quint64>(QDateTime::currentSecsSinceEpoch())
		+ 400ull * 24 * 60 * 60;

	model.add(entry(QStringLiteral("NOW"), base()));
	model.add(entry(QStringLiteral("SOON"), far));

	QCOMPARE(cell(model, QStringLiteral("SOON"), Qdc::BoardModel::WhenColumn),
	         QStringLiteral("unknown time"));

	QVERIFY(cell(model, QStringLiteral("NOW"), Qdc::BoardModel::WhenColumn)
	        != QStringLiteral("unknown time"));

	/* Not corrected, either: the entry still carries what the hub said. */
	QCOMPARE(model.entryOf(QStringLiteral("SOON")).timestamp, far);
}

void TestBoards::aSubjectOfInvisibleCharactersFallsBackToOurWording()
{
	Qdc::BoardModel model;

	/* A zero-width space and a word joiner: nothing to read, and a hole in a list. */
	model.add(entry(QStringLiteral("ROOT"), base(), QString(), QString(),
	                QStringLiteral("​⁠")));

	QCOMPARE(cell(model, QStringLiteral("ROOT"), Qdc::BoardModel::SubjectColumn),
	         QStringLiteral("(no subject)"));
}

void TestBoards::aSubjectCarryingABidirectionalOverrideIsDrawnReduced()
{
	Qdc::BoardModel model;

	const QString doctored = QStringLiteral("Notes\u202Egpj.exe");

	model.add(entry(QStringLiteral("ROOT"), base(), QString(), QString(), doctored));

	const QString shown = cell(model, QStringLiteral("ROOT"),
	                           Qdc::BoardModel::SubjectColumn);

	/* Marked rather than removed: a name doctored that way should look doctored. */
	QVERIFY(!shown.contains(QChar(0x202e)));
	QVERIFY(shown.contains(QChar(QChar::ReplacementCharacter)));

	/* And what is held is still what the hub sent, because that is what identifies it. */
	QCOMPARE(model.entryOf(QStringLiteral("ROOT")).subject, doctored);
}

void TestBoards::aReadPostIsItsSubjectItsAuthorAndItsWords()
{
	Qdc::BoardPane pane;

	const QString root = QStringLiteral("ROOT");

	pane.addEntry(entry(root, base()));
	pane.read(root);
	pane.setPost(post(root, QStringLiteral("The body")));

	QCOMPARE(pane.threads()->stateOf(root), Qdc::PostState::Verified);

	/* The subject, whose author is named beside it, and the words. Nothing between. */
	QCOMPARE(label(pane, "boardSubject")->text(), QStringLiteral("A subject"));

	QTextBrowser* body = pane.findChild<QTextBrowser*>(QStringLiteral("boardBody"));
	QVERIFY(body);
	QVERIFY(!body->isHidden());
	QVERIFY(body->toPlainText().contains(QStringLiteral("The body")));

	/*
	 * One line, and it is the hub's nickname for whoever it accepted the post from. The
	 * author's own claimed date is drawn nowhere at all: nothing verifies it, nothing is
	 * ordered by it, and a date beside a warning that it may be untrue is worse than no
	 * date.
	 */
	const QString byline = label(pane, "boardAuthor")->text();
	QVERIFY2(!byline.contains(QLatin1Char('\n')), qPrintable(byline));
	QVERIFY2(byline.startsWith(QStringLiteral("alice")), qPrintable(byline));
}

void TestBoards::nothingIsSaidBetweenASubjectAndItsWords()
{
	/*
	 * The register that is not to come back. Each of these was true and none of it was
	 * anything a reader could act on: whether a post has been checked decides whether it
	 * is shown, and a post that is on the screen does not argue for itself.
	 */
	static const char* const banned[] = {
		"the hub says", "hashed to the root", "came out of the document",
		"its author claim", "has been verified", "index line",
		"is not proof", "attributes nothing", "looks exactly like a forgery",
		"the entry is kept", "nothing here can check",

		/*
		 * And the register of the controls that stood in the same space. "Withdraw" is
		 * not among them: it is the name of a button and the extension's own word for
		 * what that button does, which is a different thing from prose about how far a
		 * post has been checked.
		 */
		"disputed", "re-read"
	};

	Qdc::BoardPane pane;

	const QString root = QStringLiteral("ROOT");
	pane.addEntry(entry(root, base()));
	pane.read(root);

	/* Every state a post on the screen can be in, including the two transient ones. */
	const auto drawn = [&pane]() {
		QString all;

		for (QLabel* one : pane.findChildren<QLabel*>()) all += one->text() + QLatin1Char('\n');

		for (QTextBrowser* one : pane.findChildren<QTextBrowser*>())
			all += one->toPlainText() + QLatin1Char('\n');

		for (QPushButton* one : pane.findChildren<QPushButton*>())
			all += one->text() + QLatin1Char('\n');

		return all.toLower();
	};

	const auto clean = [&drawn](const char* where) {
		const QString all = drawn();

		for (const char* one : banned)
		{
			QVERIFY2(!all.contains(QLatin1String(one)),
			         qPrintable(QStringLiteral("%1: still says '%2'")
				         .arg(QLatin1String(where), QLatin1String(one))));
		}
	};

	clean("announced");

	pane.setPost(post(root, QStringLiteral("The body")));
	clean("verified");

	pane.setPostUnavailable(root, QStringLiteral("no sources answered"));
	clean("unavailable");

	/*
	 * And the one line that does stand in for a body says what happened once, without
	 * advice and without repeating the reason a bridge wrote for a log.
	 */
	QTextBrowser* body = pane.findChild<QTextBrowser*>(QStringLiteral("boardBody"));
	QVERIFY(body);
	QCOMPARE(body->toPlainText(), QStringLiteral("This post could not be retrieved."));
}

void TestBoards::anAnnouncedPostIsNotDrawnAsThoughItHadBeenRead()
{
	Qdc::BoardPane pane;

	const QString root = QStringLiteral("ROOT");

	pane.addEntry(entry(root, base(), QString(), QString(),
	                    QStringLiteral("Hub upgrade on Saturday")));
	pane.read(root);

	/* The subject is on the screen, and the screen says whose subject it is. */
	QCOMPARE(label(pane, "boardSubject")->text(),
	         QStringLiteral("Hub upgrade on Saturday"));

	/*
	 * A post nobody has fetched is told apart from one that has been read by there being
	 * no words on the screen and something there to press, and not by a sentence saying
	 * so. The subject above is still the hub's - that is what an index entry is - and
	 * saying that beside it every time told a reader nothing they could act on.
	 */
	QVERIFY(button(pane, "boardFetch"));
	QVERIFY(!button(pane, "boardFetch")->isHidden());

	/*
	 * And no reading area at all. An empty one is what a post with no words in it looks
	 * like, and the two must not be confusable.
	 */
	QTextBrowser* body = pane.findChild<QTextBrowser*>(QStringLiteral("boardBody"));
	QVERIFY(body->isHidden());
	QVERIFY(body->toPlainText().isEmpty());

	/* Nothing was fetched by the entry arriving, either. */
	QCOMPARE(pane.threads()->stateOf(root), Qdc::PostState::Announced);

	QSignalSpy asked(&pane, &Qdc::BoardPane::bodyRequested);
	pane.addEntry(entry(QStringLiteral("SECOND"), base() + 10));
	QCOMPARE(asked.count(), 0);

	/* It takes a reader's action, and then it is asked for by hash. */
	pane.read(root);
	button(pane, "boardFetch")->click();

	QCOMPARE(asked.count(), 1);
	QCOMPARE(asked.first().first().toString(), root);
}

/**
 * A post whose document is on this machine is not drawn as one nothing is known about.
 *
 * A document already here is read the moment somebody opens the post, off the disk and
 * without asking anybody for anything, and the pane is told that by the state moving to
 * Fetching while it is already drawing the entry. What must not be left on the screen for
 * that moment is the unfetched wording with a button in it, over a document that is on
 * the disk in front of the reader - so the reading area follows the state of the post it
 * is showing rather than only what it was told at the time it was opened.
 */
void TestBoards::aPostWhoseDocumentIsHereIsNotDrawnAsUnfetched()
{
	Qdc::BoardPane pane;

	const QString here = QStringLiteral("HELD");
	const QString elsewhere = QStringLiteral("NOTHELD");

	pane.addEntry(entry(here, base()));
	pane.addEntry(entry(elsewhere, base() + 10));

	pane.read(here);

	/* As it stands, nothing has been read of it: no words, and something to press. */
	QVERIFY(pane.findChild<QTextBrowser*>(QStringLiteral("boardBody"))->isHidden());
	QVERIFY(button(pane, "boardFetch")->isEnabled());

	/*
	 * And then the document turns out to be here. Nothing else happens: no post is
	 * handed over, and the pane is told only that the post is being read.
	 */
	pane.threads()->setState(here, Qdc::PostState::Fetching);

	/* Nor is a document on this disk something a reader is asked to fetch. */
	QVERIFY(!button(pane, "boardFetch")->isEnabled());

	/* And the body simply appears, without anything having been pressed. */
	QSignalSpy asked(&pane, &Qdc::BoardPane::bodyRequested);

	pane.setPost(post(here, QStringLiteral("The hub will be offline on Saturday.")));

	QTextBrowser* body = pane.findChild<QTextBrowser*>(QStringLiteral("boardBody"));
	QVERIFY(!body->isHidden());
	QVERIFY(body->toPlainText().contains(QStringLiteral("offline on Saturday")));

	QCOMPARE(asked.count(), 0);

	/*
	 * A post whose document is not here is drawn exactly as before: nothing about it has
	 * been verified, and a reader who wants it has to ask - which is the whole of the
	 * privacy rule this pane is built around.
	 */
	pane.read(elsewhere);

	QCOMPARE(pane.threads()->stateOf(elsewhere), Qdc::PostState::Announced);

	QVERIFY(pane.findChild<QTextBrowser*>(QStringLiteral("boardBody"))->isHidden());
	QVERIFY(button(pane, "boardFetch")->isEnabled());
	QCOMPARE(asked.count(), 0);
}

void TestBoards::aDisputedPostIsAttributedToTheIndexAndNotToTheDocument()
{
	Qdc::BoardPane pane;

	const QString root = QStringLiteral("ROOT");

	/*
	 * No nickname, so the hub's CID is what the byline has to fall back to - which is
	 * what makes this a test of which claim the post is attributed to rather than of
	 * which of two names reads better.
	 */
	pane.addEntry(entry(root, base(), QString(), QString(), QStringLiteral("A subject"),
	                    QStringLiteral("HUBSAIDTHIS"), QString()));
	pane.read(root);

	/*
	 * Arrives called verified, and is drawn as disputed all the same: the pane holds both
	 * halves and can see the disagreement itself.
	 */
	pane.setPost(post(root, QStringLiteral("Body"), false,
	                  QStringLiteral("DOCUMENTSAIDTHIS")));

	QCOMPARE(pane.threads()->stateOf(root), Qdc::PostState::Disputed);

	QStringList notes;
	for (QLabel* one : pane.findChildren<QLabel*>()) notes.append(one->text());

	const QString all = notes.join(QLatin1Char('\n'));

	/*
	 * The hub's claim is what the post is attributed to, and the document's is drawn
	 * nowhere at all. Showing both and explaining the difference was the old answer; it
	 * asked a reader to adjudicate something nothing on this machine can settle.
	 */
	QVERIFY2(all.contains(QStringLiteral("HUBSAIDTHIS")), qPrintable(all));
	QVERIFY2(!all.contains(QStringLiteral("DOCUMENTSAIDTHIS")), qPrintable(all));

	QTextBrowser* body = pane.findChild<QTextBrowser*>(QStringLiteral("boardBody"));
	QVERIFY2(!body->toPlainText().contains(QStringLiteral("DOCUMENTSAIDTHIS")),
	         qPrintable(body->toPlainText()));
}

/**
 * A post whose two authorship claims disagree is on the board like any other.
 *
 * Its bytes hashed to the name they were asked for and its header parsed, so everything
 * it says is the post's own; all that disagrees is who wrote it, and the honest case of
 * that is a post relayed to a second hub by somebody who did not write it. Since a relay
 * is indistinguishable from a forgery, refusing to list one would empty every relayed
 * board to catch the one forger - so it is listed, read, and attributed to the hub.
 */
void TestBoards::aDisputedPostIsListedAndReadLikeAnyOther()
{
	Qdc::BoardPane pane;

	const QString root = QStringLiteral("ROOT");

	pane.addEntry(entry(root, base()));
	pane.read(root);

	pane.setPost(post(root, QStringLiteral("Relayed from the other hub"), false,
	                  QStringLiteral("DOCUMENTSAIDTHIS")));

	QCOMPARE(pane.threads()->stateOf(root), Qdc::PostState::Disputed);

	/* Listed, and not marked in the list either: nothing about it is doubtful to draw. */
	QVERIFY(pane.threads()->listable(root));
	QVERIFY(pane.threads()->indexOf(root).isValid());
	QCOMPARE(pane.threads()->postCount(), 1);
	QVERIFY(!role(*pane.threads(), root, Qt::ForegroundRole).isValid());

	QTextBrowser* body = pane.findChild<QTextBrowser*>(QStringLiteral("boardBody"));
	QVERIFY(!body->isHidden());
	QVERIFY(body->toPlainText().contains(QStringLiteral("Relayed from the other hub")));
}

/**
 * A document that is here and is not the post its entry named is not listed at all.
 *
 * The bytes did not hash to the name, or they did and the header did not parse. Either
 * way this client has read the document and there is nothing true left to show - not even
 * the hub's subject line, which described a document that turns out to be something else -
 * and no later copy can differ, because every copy of it is these same bytes.
 */
void TestBoards::aDocumentThatIsNotThePostItClaimsToBeIsNotListed()
{
	Qdc::BoardPane pane;

	const QString root = QStringLiteral("ROOT");
	const QString reply = QStringLiteral("REPLY");
	const QString other = QStringLiteral("OTHER");

	pane.addEntries({entry(root, base()), entry(reply, base() + 10, root),
	                 entry(other, base() + 20)});

	pane.read(root);

	QSignalSpy showing(&pane, &Qdc::BoardPane::readingChanged);

	pane.setPostUnverifiable(root);

	QVERIFY(!pane.threads()->listable(root));
	QVERIFY(!pane.threads()->indexOf(root).isValid());
	QVERIFY2(!drawn(*pane.threads()).contains(root),
	         qPrintable(drawn(*pane.threads()).join(QLatin1Char(' '))));

	/* The board is still the board: everything else on it stands. */
	QCOMPARE(pane.threads()->postCount(), 2);
	QVERIFY(pane.threads()->indexOf(other).isValid());

	/*
	 * And the reply to it is a post of its own, which is now a reply to something that
	 * is not on the board - the same arrangement as a parent that never arrived.
	 */
	QVERIFY(pane.threads()->indexOf(reply).isValid());
	QVERIFY(role(*pane.threads(), reply, Qdc::BoardModel::MissingParentRole).toBool());

	/* Nothing is left on the screen either, and whoever was told it is told again. */
	QVERIFY(pane.reading().isEmpty());
	QCOMPARE(showing.count(), 1);
	QVERIFY(showing.first().first().toString().isEmpty());

	QVERIFY(pane.findChild<QTextBrowser*>(QStringLiteral("boardBody"))->isHidden());
}

/**
 * A post nobody could be found for stays in the list and is drawn quieter than the rest.
 *
 * An index entry can outlive every copy of the document it names, and the copy that is
 * missing today may be on a client that is switched off. So the row keeps its subject,
 * its author and its date and says what has happened by being dimmed - a mark on the row
 * rather than a word in a column, which is what the columns of this list are not for.
 */
void TestBoards::aPostNobodyCouldBeFoundForIsListedAndDimmed()
{
	Qdc::BoardPane pane;

	const QString missing = QStringLiteral("GONE");
	const QString held = QStringLiteral("HERE");

	pane.addEntries({entry(missing, base()), entry(held, base() + 10)});

	pane.read(missing);
	pane.setPostUnavailable(missing, QStringLiteral("nobody answered"));

	QVERIFY(pane.threads()->listable(missing));
	QVERIFY(pane.threads()->indexOf(missing).isValid());
	QCOMPARE(pane.threads()->postCount(), 2);

	const QVariant dimmed = role(*pane.threads(), missing, Qt::ForegroundRole);

	QVERIFY(dimmed.isValid());
	QVERIFY(!role(*pane.threads(), held, Qt::ForegroundRole).isValid());

	/* Its subject is still drawn, and drawn as itself. */
	QCOMPARE(cell(*pane.threads(), missing, Qdc::BoardModel::SubjectColumn),
	         QStringLiteral("A subject"));
}

void TestBoards::aPlainBodyIsNeverRenderedAsMarkdown()
{
	Qdc::BoardPane pane;

	const QString root = QStringLiteral("ROOT");
	const QString body = QStringLiteral("**not bold** and `not code`");

	pane.addEntry(entry(root, base()));
	pane.read(root);

	/* No RT flag on the document, so the markers are words the author typed. */
	pane.setPost(post(root, body, false));

	QTextBrowser* shown = pane.findChild<QTextBrowser*>(QStringLiteral("boardBody"));
	QVERIFY(!shown->isHidden());
	QVERIFY2(shown->toPlainText().contains(body), qPrintable(shown->toPlainText()));

	/* The same bytes with the flag set are read as the markdown subset, and only then. */
	pane.setPost(post(root, body, true));

	QVERIFY(!shown->toPlainText().contains(QStringLiteral("**not bold**")));
	QVERIFY(shown->toPlainText().contains(QStringLiteral("not bold")));
}

void TestBoards::theWordDeleteAppearsNowhereInThePane()
{
	Qdc::BoardPane pane;

	Qdc::BoardInfo board;
	board.name = QStringLiteral("general");
	board.title = QStringLiteral("General discussion");
	board.permissions = 1 + 2 + 4 + 8;
	board.oldest = base() - 86400;
	board.posts = 2;

	pane.setBoard(board);

	const QString root = QStringLiteral("ROOT");

	pane.addEntry(entry(root, base()));
	pane.addEntry(entry(QStringLiteral("REPLY"), base() + 10, root));
	pane.read(root);

	/* Every state, both bands and both composers, so that every string has been drawn. */
	pane.setPost(post(root, QStringLiteral("Body")));
	pane.setPostUnavailable(root, QStringLiteral("nobody answered"));
	pane.reportRefusal(72, QStringLiteral("Post exceeds 65536 bytes"), root);
	pane.setGap(true);
	button(pane, "boardDelete")->click();
	button(pane, "boardNewPost")->click();
	button(pane, "boardReply")->click();

	/*
	 * And the note a reply gets when what it answers goes while it is being written,
	 * which is the one place left that had its own word for this and was reached by
	 * nothing: it takes a tombstone arriving for the post the composer was opened on.
	 */
	pane.addEntry(tombstone(root, base() + 500));

	QStringList drawnText;

	for (QLabel* one : pane.findChildren<QLabel*>()) drawnText.append(one->text());

	for (QAbstractButton* one : pane.findChildren<QAbstractButton*>())
		drawnText << one->text() << one->toolTip();

	for (QLineEdit* one : pane.findChildren<QLineEdit*>())
		drawnText << one->text() << one->placeholderText();

	for (QTextBrowser* one : pane.findChildren<QTextBrowser*>())
		drawnText.append(one->toPlainText());

	/* The model's words too: it is what the list draws. */
	const Qdc::BoardModel& model = *pane.threads();

	for (int column = 0; column < Qdc::BoardModel::ColumnCount; ++column)
	{
		drawnText.append(model.headerData(column, Qt::Horizontal).toString());
		drawnText.append(cell(model, QStringLiteral("REPLY"),
		                      static_cast<Qdc::BoardModel::Column>(column)));
	}

	drawnText.append(model.data(model.indexOf(QStringLiteral("REPLY")),
	                            Qt::ToolTipRole).toString());

	/*
	 * Withdraw, and never delete. Taking a post out of a hub's index is the whole of
	 * what this can do: the document is a file on the disk of everybody who has read it
	 * and goes on being served from there, so a control offering to delete a post offers
	 * something no client can carry out. The specification uses withdraw throughout for
	 * this reason and so does the pane.
	 */
	const QString all = drawnText.join(QLatin1Char('\n'));

	QVERIFY2(!all.contains(QStringLiteral("delete"), Qt::CaseInsensitive),
	         qPrintable(all));

	QVERIFY(all.contains(QStringLiteral("Withdraw")));
}

/**
 * The list is a subject, an author and a time, and no working notes.
 *
 * What the parent column and the document column carried was this client's own bookkeeping
 * - which entry names a parent nobody holds, how far a document has been checked - and a
 * reader of a board has nothing to do with either. What they do need is on the row itself:
 * a post that could not be had is dimmed and a reply drawn away from its parent is slanted.
 */
void TestBoards::theThreadListDrawsNoParentOrDocumentColumn()
{
	Qdc::BoardModel model;

	model.add(entry(QStringLiteral("ROOT"), base()));

	QCOMPARE(model.columnCount(), 3);
	QCOMPARE(model.columnCount(), int(Qdc::BoardModel::ColumnCount));

	QStringList headings;

	for (int column = 0; column < Qdc::BoardModel::ColumnCount; ++column)
		headings.append(model.headerData(column, Qt::Horizontal).toString());

	QCOMPARE(headings, QStringList({QStringLiteral("Subject"), QStringLiteral("Author"),
	                                QStringLiteral("Accepted")}));

	for (const char* const gone : {"Parent", "Document"})
		QVERIFY2(!headings.contains(QLatin1String(gone)), gone);
}

void TestBoards::permissionsDecideWhatIsOffered()
{
	Qdc::BoardPane pane;

	/*
	 * Named as the entry's own board, or the model would refuse it and the pane would be
	 * reading a post it holds no entry for - which is not the case being tested here and
	 * would answer nothing about whose post it is.
	 */
	Qdc::BoardInfo readOnly;
	readOnly.name = QStringLiteral("general");
	readOnly.permissions = static_cast<quint32>(Qdc::BoardPermission::Subscribe);

	pane.setBoard(readOnly);
	pane.addEntry(entry(QStringLiteral("ROOT"), base()));
	pane.read(QStringLiteral("ROOT"));

	/* Not there at all rather than there and refusing: see refreshActions(). */
	QVERIFY(button(pane, "boardNewPost")->isHidden());
	QVERIFY(button(pane, "boardReply")->isHidden());
	QVERIFY(button(pane, "boardDelete")->isHidden());

	/* Reply without start-a-thread is what an announcements board is. */
	Qdc::BoardInfo replies = readOnly;
	replies.permissions |= static_cast<quint32>(Qdc::BoardPermission::Reply);

	pane.setBoard(replies);

	QVERIFY(button(pane, "boardNewPost")->isHidden());
	QVERIFY(!button(pane, "boardReply")->isHidden());
	QVERIFY(button(pane, "boardDelete")->isHidden());

	/* Starting a thread is what "New post" is for, and it says that and not "thread". */
	Qdc::BoardInfo posting = replies;
	posting.permissions |= static_cast<quint32>(Qdc::BoardPermission::StartThread);

	pane.setBoard(posting);

	QVERIFY(!button(pane, "boardNewPost")->isHidden());
	QCOMPARE(button(pane, "boardNewPost")->text(), QStringLiteral("New post"));

	/*
	 * Withdrawing anybody's offers it whoever the post belongs to, and this user is
	 * nobody in particular here.
	 */
	Qdc::BoardInfo anybody = posting;
	anybody.permissions |= static_cast<quint32>(Qdc::BoardPermission::WithdrawAny);

	pane.setBoard(anybody);
	pane.read(QStringLiteral("ROOT"));

	QVERIFY(!button(pane, "boardDelete")->isHidden());
	QCOMPARE(button(pane, "boardDelete")->text(), QStringLiteral("Withdraw"));

	/*
	 * Withdrawing one's own offers it on this user's own posts and on no others. The
	 * entry above was accepted from CIDONE, so a client posting as anybody else is
	 * looking at somebody else's post and has nothing to withdraw - and the button is
	 * gone rather than there and refused, for the reason every other control here is.
	 */
	Qdc::BoardInfo own = posting;
	own.permissions |= static_cast<quint32>(Qdc::BoardPermission::WithdrawOwn);

	pane.setBoard(own);
	pane.setOwnCid(QStringLiteral("CIDTWO"));
	pane.read(QStringLiteral("ROOT"));

	QVERIFY2(button(pane, "boardDelete")->isHidden(),
	         "somebody else's post was offered for withdrawal");

	/* And an installation that has no client id yet owns nothing. */
	pane.setOwnCid(QString());
	QVERIFY(button(pane, "boardDelete")->isHidden());

	pane.setOwnCid(QStringLiteral("CIDONE"));

	QVERIFY(!button(pane, "boardDelete")->isHidden());
	QCOMPARE(button(pane, "boardDelete")->text(), QStringLiteral("Withdraw"));

	/* Both permissions at once is what an operator on their own post has. */
	Qdc::BoardInfo both = own;
	both.permissions |= static_cast<quint32>(Qdc::BoardPermission::WithdrawAny);

	pane.setBoard(both);
	pane.read(QStringLiteral("ROOT"));

	QVERIFY(!button(pane, "boardDelete")->isHidden());

	QSignalSpy taken(&pane, &Qdc::BoardPane::withdrawRequested);

	/* And it is asked about before it happens; the question is what sends it. */
	button(pane, "boardDelete")->click();
	QCOMPARE(taken.count(), 0);

	button(pane, "boardDeleteConfirm")->click();

	QCOMPARE(taken.count(), 1);
	QCOMPARE(taken.first().first().toString(), QStringLiteral("ROOT"));
}

/**
 * Deleting says what it does and promises nothing about the document.
 *
 * The word is the reader's: somebody pressing it means delete. What happens is the
 * extension's withdrawal - the entry leaves this hub's index and the withdrawal travels
 * to whoever is watching the board - and a copy is a file on somebody else's disk. So the
 * question says both of those things and stops, rather than promising the post is gone.
 */
void TestBoards::withdrawingIsAskedAboutInPlainWords()
{
	Qdc::BoardPane pane;

	Qdc::BoardInfo board;
	board.name = QStringLiteral("general");
	board.permissions = 1 + 2 + 4 + 8;

	pane.setBoard(board);
	pane.addEntry(entry(QStringLiteral("ROOT"), base()));
	pane.read(QStringLiteral("ROOT"));

	QLabel* question = label(pane, "boardDeleteQuestion");
	QVERIFY(question);

	/* The question and the buttons are one row and the other, in the same place. */
	const auto asking = [question] { return !question->parentWidget()->isHidden(); };

	const auto offering = [&pane] {
		return !button(pane, "boardDelete")->parentWidget()->isHidden();
	};

	/* Nothing is asked until somebody asks for it. */
	QVERIFY(!asking());
	QVERIFY(offering());

	button(pane, "boardDelete")->click();

	QVERIFY(asking());
	QVERIFY(!offering());

	const QString asked = question->text();

	QVERIFY2(asked.contains(QStringLiteral("hub's index")), qPrintable(asked));
	QVERIFY2(asked.contains(QStringLiteral("drop their copies")), qPrintable(asked));

	/* And no promise about the bytes, which are on disks this client does not own. */
	for (const char* const promise : {"is gone", "permanently", "for ever", "everywhere",
	                                  "cannot be recovered", "erased"})
	{
		QVERIFY2(!asked.contains(QLatin1String(promise), Qt::CaseInsensitive),
		         qPrintable(QStringLiteral("%1: promises '%2'")
			         .arg(asked, QLatin1String(promise))));
	}

	/* Cancelling puts the buttons back and asks the hub for nothing. */
	QSignalSpy taken(&pane, &Qdc::BoardPane::withdrawRequested);

	button(pane, "boardDeleteCancel")->click();

	QVERIFY(!asking());
	QVERIFY(offering());
	QCOMPARE(taken.count(), 0);

	/*
	 * And a question asked about one post does not follow the reader to the next: what
	 * would be deleted is whatever is on the screen when it is answered.
	 */
	pane.addEntry(entry(QStringLiteral("NEXT"), base() + 10));

	button(pane, "boardDelete")->click();
	pane.read(QStringLiteral("NEXT"));

	QVERIFY(!asking());
	QCOMPARE(taken.count(), 0);
}

void TestBoards::aRefusalIsReportedInThePane()
{
	Qdc::BoardPane pane;

	const QString root = QStringLiteral("ROOT");

	pane.addEntry(entry(root, base(), QString(), QString(),
	                    QStringLiteral("Hub upgrade on Saturday")));

	QCOMPARE(bands(pane).size(), 2);
	QVERIFY(bands(pane).at(1)->text().isEmpty());

	/* There is no token, so the refusal is matched to the post by the hash it names. */
	pane.reportRefusal(75, QStringLiteral("One post per minute"), root);

	const QString said = bands(pane).at(1)->text();

	QVERIFY2(said.contains(QStringLiteral("refused")), qPrintable(said));
	QVERIFY2(said.contains(QStringLiteral("75")), qPrintable(said));
	QVERIFY2(said.contains(QStringLiteral("One post per minute")), qPrintable(said));
	QVERIFY2(said.contains(QStringLiteral("Hub upgrade on Saturday")), qPrintable(said));

	/*
	 * And what the hub said is ended before this pane's own sentences run on from it.
	 * A hub's words are a stranger's and nothing obliges them to be punctuated, so
	 * without this the band read "...per minute It named the post...".
	 */
	QVERIFY2(!said.contains(QStringLiteral("minute It named")), qPrintable(said));
	QVERIFY2(said.contains(QStringLiteral("minute. It named")), qPrintable(said));

	/* One that names a board this pane knows nothing about is still reported. */
	pane.reportRefusal(71, QStringLiteral("No such board"), QString(),
	                   QStringLiteral("genrel"));

	QVERIFY2(bands(pane).at(1)->text().contains(QStringLiteral("genrel")),
	         qPrintable(bands(pane).at(1)->text()));
}

void TestBoards::thePostBeingWrittenTakesTheRoomItIsGiven()
{
	Qdc::BoardPane pane;

	Qdc::ComposerEdit* box = pane.findChild<Qdc::ComposerEdit*>();
	QVERIFY(box);

	/*
	 * A post is the only thing on the screen while it is being written, so its box takes
	 * the height it is given. Left as a conversation's is, it stops at the few lines that
	 * are right at the foot of a chat, and somebody writing a post types into a slot with
	 * an empty pane underneath it.
	 */
	QCOMPARE(box->sizePolicy().verticalPolicy(), QSizePolicy::Expanding);

	/*
	 * And it can still shrink, which is what the floor is for: a minimum that tracked the
	 * content would ratchet upward with every line typed, and nothing - not deleting the
	 * text, not a smaller window - would bring it back down.
	 */
	const int floor = box->minimumSizeHint().height();

	box->setPlainText(QStringLiteral("one\ntwo\nthree\nfour\nfive\nsix\nseven\neight"));

	QCOMPARE(box->minimumSizeHint().height(), floor);
	QVERIFY(box->sizeHint().height() >= floor);

	/*
	 * A conversation's box is unchanged, which is the regression that would matter: the
	 * two share a class and only one of them was meant to move.
	 */
	Qdc::Composer chat;

	Qdc::ComposerEdit* chatBox = chat.findChild<Qdc::ComposerEdit*>();
	QVERIFY(chatBox);

	QCOMPARE(chatBox->sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
}

void TestBoards::aRefusalCanBeDismissedAndGoesByItself()
{
	Qdc::BoardPane pane;

	QWidget* band = bands(pane).at(1)->parentWidget();
	QVERIFY(band);
	QVERIFY(band->isHidden());

	pane.reportRefusal(75, QStringLiteral("One post per minute"));
	QVERIFY(!band->isHidden());

	/* A way out of it, for somebody who has read it. */
	QPushButton* close = pane.findChild<QPushButton*>(QStringLiteral("boardRefusalClose"));
	QVERIFY(close);

	close->click();

	QVERIFY(band->isHidden());
	QVERIFY(bands(pane).at(1)->text().isEmpty());

	/*
	 * And it goes by itself, because a refusal is something that happened rather than
	 * what the board is. Driven by moving the timer rather than by waiting out its
	 * twenty seconds, which is the interval a person needs and not one a test does.
	 */
	pane.reportRefusal(75, QStringLiteral("One post per minute"));
	QVERIFY(!band->isHidden());

	QTimer* timer = nullptr;

	for (QTimer* one : pane.findChildren<QTimer*>())
		if (one->isSingleShot() && one->isActive()) timer = one;

	QVERIFY2(timer, "the refusal was shown with nothing set to take it away");
	QVERIFY(timer->interval() > 1000);

	timer->stop();
	timer->setInterval(1);
	timer->start();

	QTRY_VERIFY(band->isHidden());

	/*
	 * The gap is not like that and must not have gone with it: it is what this view of
	 * the board is until the hub will replay far enough back, and one that quietly
	 * stopped saying so would look complete.
	 */
	Qdc::BoardInfo board;
	board.name = QStringLiteral("general");
	board.oldest = base();

	pane.setBoard(board);
	pane.setGap(true);

	QWidget* gapBand = bands(pane).at(0)->parentWidget();
	QVERIFY(gapBand && !gapBand->isHidden());

	QVERIFY(!pane.findChild<QPushButton*>(QStringLiteral("boardGapClose")));
}

void TestBoards::aGapThatCannotBeClosedIsSaidSo()
{
	Qdc::BoardPane pane;

	Qdc::BoardInfo board;
	board.name = QStringLiteral("general");
	board.permissions = 1;
	board.oldest = base() - 30 * 86400;

	pane.setBoard(board);

	QVERIFY(bands(pane).at(0)->text().isEmpty());

	pane.setGap(true);

	const QString said = bands(pane).at(0)->text();
	QVERIFY2(said.contains(QStringLiteral("gap")), qPrintable(said));
	QVERIFY2(said.contains(QStringLiteral("part of the board")), qPrintable(said));

	pane.setGap(false);
	QVERIFY(bands(pane).at(0)->parentWidget()->isHidden());
}

/**
 * Writing something takes the area the post was in, and gives it back.
 *
 * A composer standing open under every post takes a third of the pane from the thing the
 * pane is for, and a reader who has asked to write something is writing it. So the
 * subject line and the editor arrive in the reading area's place - and cancelling puts
 * back the post that was being read, rather than an empty pane the reader has to find
 * their way back into.
 */
void TestBoards::writingAPostTakesTheReadingAreaAndGivesItBack()
{
	Qdc::BoardPane pane;

	Qdc::BoardInfo board;
	board.name = QStringLiteral("general");
	board.permissions = 1 + 2 + 4;

	pane.setBoard(board);

	const QString root = QStringLiteral("ROOT");

	pane.addEntry(entry(root, base()));
	pane.read(root);
	pane.setPost(post(root, QStringLiteral("The hub will be offline")));

	QStackedWidget* area = pane.findChild<QStackedWidget*>();
	QVERIFY(area);

	QLineEdit* subject = pane.findChild<QLineEdit*>(QStringLiteral("boardComposeSubject"));
	QVERIFY(subject);

	/* Two things in one space, which is what makes writing replace reading. */
	QWidget* reading = label(pane, "boardSubject")->parentWidget();
	QWidget* writing = subject->parentWidget();

	QCOMPARE(area->count(), 2);
	QVERIFY(reading != writing);
	QCOMPARE(area->currentWidget(), reading);

	button(pane, "boardNewPost")->click();
	QCOMPARE(area->currentWidget(), writing);

	button(pane, "boardComposeCancel")->click();

	QCOMPARE(area->currentWidget(), reading);

	/* And the post is where it was, still read, without being opened again. */
	QCOMPARE(pane.reading(), root);
	QCOMPARE(label(pane, "boardSubject")->text(), QStringLiteral("A subject"));

	QTextBrowser* body = pane.findChild<QTextBrowser*>(QStringLiteral("boardBody"));
	QVERIFY(!body->isHidden());
	QVERIFY(body->toPlainText().contains(QStringLiteral("The hub will be offline")));

	/* A reply is the same area and the same way out of it. */
	button(pane, "boardReply")->click();
	QCOMPARE(area->currentWidget(), writing);

	button(pane, "boardComposeCancel")->click();
	QCOMPARE(area->currentWidget(), reading);
}

/**
 * A reply opens with the subject it is answering and the words it is answering, quoted.
 *
 * Neither is put through RichText::displayText() on the way in. That reduction exists for
 * drawing a stranger's text in a row - it bounds the length, folds the newlines and marks
 * what it disarms - and this text is on its way into a document whose hash is the post's
 * name. A replacement character or an ellipsis of ours in there would be in the bytes
 * somebody sends, under a name every reader checks against.
 */
void TestBoards::aReplyOpensQuotingThePostItAnswers()
{
	Qdc::BoardPane pane;

	Qdc::BoardInfo board;
	board.name = QStringLiteral("general");
	board.permissions = 1 + 2 + 4;

	pane.setBoard(board);

	const QString root = QStringLiteral("ROOT");

	/*
	 * Longer than displayText() allows, on more than one line, and carrying an override
	 * it would replace: three things that would each be visible had the quote been
	 * reduced on the way into the box.
	 */
	const QString body = QStringLiteral("The hub will be offline on Saturday, from "
	                                    "eight in the morning until the afternoon, "
	                                    "while the disks are replaced.\n"
	                                    "Bring the file list back\u202Eafterwards.");

	pane.addEntry(entry(root, base()));
	pane.read(root);
	pane.setPost(post(root, body));

	button(pane, "boardReply")->click();

	QLineEdit* subject = pane.findChild<QLineEdit*>(QStringLiteral("boardComposeSubject"));
	QVERIFY(subject);
	QCOMPARE(subject->text(), QStringLiteral("Re: A subject"));

	Qdc::ComposerEdit* box = pane.findChild<Qdc::ComposerEdit*>();
	QVERIFY(box);

	const QString opened = box->toPlainText();

	/*
	 * The quote is a quote and not two characters in front of a line. Written into the
	 * text, the "> " is what a reader sees when the reply goes out as plain text, and
	 * what gets escaped to "\>" the moment anything else in the reply is formatted. So
	 * it lives on the block, and the markers appear where they belong - on the way out.
	 */
	const QString outgoing = Qdc::RichText::toMarkdown(*box->document());

	QVERIFY2(outgoing.contains(QStringLiteral("> The hub will be offline")),
	         qPrintable(outgoing));

	QVERIFY2(outgoing.contains(QStringLiteral("> Bring the file list back")),
	         qPrintable(outgoing));

	QVERIFY2(!opened.contains(QStringLiteral("> ")), qPrintable(opened));

	/* The author's own bytes: the override is there and nothing of ours is. */
	QVERIFY(opened.contains(QChar(0x202e)));
	QVERIFY(!opened.contains(QChar(QChar::ReplacementCharacter)));
	QVERIFY(!opened.contains(QChar(0x2026)));

	/* A paragraph of their own after the quote, with the cursor in it. */
	QVERIFY2(opened.endsWith(QLatin1Char('\n')), qPrintable(opened));
	QCOMPARE(box->textCursor().position(), opened.size());

	/* And the reply carries markup, since a quote is markup. */
	QVERIFY(Qdc::RichText::hasMarkup(*box->document()));

	QSignalSpy replied(&pane, &Qdc::BoardPane::replySubmitted);

	Qdc::Composer* composer = pane.findChild<Qdc::Composer*>();
	emit composer->sendRequested(outgoing, true);

	QCOMPARE(replied.count(), 1);
	QCOMPARE(replied.first().at(1).toString(), QStringLiteral("Re: A subject"));
	QVERIFY(replied.first().at(2).toString().contains(QStringLiteral("> The hub will")));

	/*
	 * And "Re: " once. A reply to a reply is about the same thing as the post it
	 * answers, and a subject that grows a prefix every time says nothing more.
	 */
	const QString again = QStringLiteral("AGAIN");

	pane.addEntry(entry(again, base() + 10, root, QString(),
	                    QStringLiteral("Re: A subject")));

	pane.read(again);
	button(pane, "boardReply")->click();

	QCOMPARE(subject->text(), QStringLiteral("Re: A subject"));
}

void TestBoards::aSubmittedThreadIsNotPresentedAsPublished()
{
	Qdc::BoardPane pane;

	Qdc::BoardInfo board;
	board.name = QStringLiteral("general");
	board.permissions = 1 + 2 + 4;

	pane.setBoard(board);

	button(pane, "boardNewPost")->click();

	Qdc::Composer* composer = pane.findChild<Qdc::Composer*>();
	QVERIFY(composer);

	QSignalSpy submitted(&pane, &Qdc::BoardPane::threadSubmitted);

	/* A thread needs a subject, and is not sent without one. */
	emit composer->sendRequested(QStringLiteral("Body"), false);
	QCOMPARE(submitted.count(), 0);

	QLineEdit* subject = pane.findChild<QLineEdit*>(QStringLiteral("boardComposeSubject"));
	QVERIFY(subject);
	subject->setText(QStringLiteral("Hub upgrade on Saturday"));

	emit composer->sendRequested(QStringLiteral("The hub will be offline"), false);

	QCOMPARE(submitted.count(), 1);
	QCOMPARE(submitted.first().at(0).toString(),
	         QStringLiteral("Hub upgrade on Saturday"));
	QCOMPARE(submitted.first().at(1).toString(), QStringLiteral("The hub will be offline"));

	/*
	 * There is no success message in this protocol: the entry arriving in the list is
	 * the acknowledgement and the only one. So nothing anywhere says the post went up -
	 * what will say so is its appearing among the others, and it is not there yet.
	 */
	QStringList notes;
	for (QLabel* one : pane.findChildren<QLabel*>()) notes.append(one->text());

	const QString all = notes.join(QLatin1Char('\n'));

	for (const char* const claim : {"posted", "published", "sent"})
	{
		QVERIFY2(!all.contains(QLatin1String(claim), Qt::CaseInsensitive),
		         qPrintable(all));
	}

	QCOMPARE(pane.threads()->postCount(), 0);

	/* And the post that was being read is back, rather than what was already sent. */
	QVERIFY(pane.findChild<QLineEdit*>(QStringLiteral("boardComposeSubject"))
	            ->parentWidget()->isHidden());
}

void TestBoards::quotingAFormattedPostKeepsItsFormatting()
{
	Qdc::BoardPane pane;

	Qdc::BoardInfo board;
	board.name = QStringLiteral("general");
	board.permissions = 1 + 2 + 4;

	pane.setBoard(board);

	const QString root = QStringLiteral("ROOT");

	pane.addEntry(entry(root, base()));
	pane.read(root);

	/* A post that said it was the markdown subset, and is. */
	pane.setPost(post(root, QStringLiteral("The hub is **offline** on Saturday."), true));

	button(pane, "boardReply")->click();

	Qdc::ComposerEdit* box = pane.findChild<Qdc::ComposerEdit*>();
	QVERIFY(box);

	/*
	 * Rendered on the way in, so the emphasis is emphasis in the box rather than a pair
	 * of asterisks somebody has to look at while they answer.
	 */
	const QString shown = box->toPlainText();

	QVERIFY2(shown.contains(QStringLiteral("The hub is offline on Saturday.")),
	         qPrintable(shown));

	QVERIFY2(!shown.contains(QStringLiteral("**")), qPrintable(shown));

	/*
	 * And markers on the way back out rather than escapes. Put in as text, the asterisks
	 * would leave as "\*\*offline\*\*" the moment anything in the reply was formatted -
	 * markdown for "these are not markers", which is the opposite of what was quoted.
	 */
	const QString outgoing = Qdc::RichText::toMarkdown(*box->document());

	QVERIFY2(outgoing.contains(QStringLiteral("**offline**")), qPrintable(outgoing));
	QVERIFY2(!outgoing.contains(QStringLiteral("\\*")), qPrintable(outgoing));
	QVERIFY2(outgoing.contains(QStringLiteral("> The hub is")), qPrintable(outgoing));
}

void TestBoards::whatWasWrittenSurvivesAPostBeingRefused()
{
	Qdc::BoardPane pane;

	Qdc::BoardInfo board;
	board.name = QStringLiteral("general");
	board.permissions = 1 + 2 + 4;

	pane.setBoard(board);
	button(pane, "boardNewPost")->click();

	Qdc::Composer* composer = pane.findChild<Qdc::Composer*>();
	Qdc::ComposerEdit* box = pane.findChild<Qdc::ComposerEdit*>();
	QVERIFY(composer && box);

	box->setPlainText(QStringLiteral("Something worth keeping"));

	/*
	 * Refused here rather than by the hub, for want of a subject. The words have to
	 * still be there: emptying the box takes them away at the very moment the reason
	 * for keeping them is being written underneath it.
	 */
	emit composer->sendRequested(QStringLiteral("Something worth keeping"), false);

	QCOMPARE(box->toPlainText(), QStringLiteral("Something worth keeping"));
	QVERIFY(!pane.findChild<QLineEdit*>(QStringLiteral("boardComposeSubject"))
	             ->parentWidget()->isHidden());
}

void TestBoards::returnMakesAParagraphInAPostAndSendsAMessage()
{
	/*
	 * A message is one thing said in a conversation and Return is how it is said. A post
	 * is a document written at length, and one where every line break needs a modifier -
	 * over a key that fires it off half written - is the wrong way round.
	 */
	Qdc::BoardPane pane;

	Qdc::ComposerEdit* post = pane.findChild<Qdc::ComposerEdit*>();
	QVERIFY(post);

	post->setPlainText(QStringLiteral("First line"));

	QSignalSpy posted(post, &Qdc::ComposerEdit::sendRequested);

	QTest::keyClick(post, Qt::Key_Return);

	QCOMPARE(posted.count(), 0);
	QVERIFY2(post->toPlainText().contains(QLatin1Char('\n')),
	         qPrintable(post->toPlainText()));

	/* And the modifier still sends, for anybody who would rather not reach for a button. */
	QTest::keyClick(post, Qt::Key_Return, Qt::ControlModifier);
	QCOMPARE(posted.count(), 1);

	/* A conversation's box is unchanged. */
	Qdc::Composer chat;

	Qdc::ComposerEdit* message = chat.findChild<Qdc::ComposerEdit*>();
	QVERIFY(message);

	message->setPlainText(QStringLiteral("Hello"));

	QSignalSpy said(message, &Qdc::ComposerEdit::sendRequested);

	QTest::keyClick(message, Qt::Key_Return);
	QCOMPARE(said.count(), 1);

	QTest::keyClick(message, Qt::Key_Return, Qt::ShiftModifier);
	QCOMPARE(said.count(), 1);
}

void TestBoards::aReplyKeepsTheParentItWasBegunOn()
{
	Qdc::BoardPane pane;

	Qdc::BoardInfo board;
	board.name = QStringLiteral("general");
	board.permissions = 1 + 2 + 4;

	pane.setBoard(board);

	const QString first = QStringLiteral("AAAA");
	const QString second = QStringLiteral("BBBB");

	pane.addEntry(entry(first, base()));
	pane.addEntry(entry(second, base() + 10));

	pane.read(first);
	button(pane, "boardReply")->click();

	/* The list goes on arriving and a reader may click something else while typing. */
	pane.read(second);

	QSignalSpy replied(&pane, &Qdc::BoardPane::replySubmitted);

	Qdc::Composer* composer = pane.findChild<Qdc::Composer*>();
	emit composer->sendRequested(QStringLiteral("Will the file list survive?"), false);

	QCOMPARE(replied.count(), 1);
	QCOMPARE(replied.first().at(0).toString(), first);
}

void TestBoards::aReplyToAPostWithdrawnWhileWritingIsNotSent()
{
	Qdc::BoardPane pane;

	Qdc::BoardInfo board;
	board.name = QStringLiteral("general");
	board.permissions = 1 + 2 + 4;

	pane.setBoard(board);

	const QString root = QStringLiteral("ROOT");

	pane.addEntry(entry(root, base()));
	pane.read(root);
	button(pane, "boardReply")->click();

	/* The tombstone arrives while the reply is being typed. */
	pane.addEntry(tombstone(root, base() + 10));

	QSignalSpy replied(&pane, &Qdc::BoardPane::replySubmitted);

	Qdc::Composer* composer = pane.findChild<Qdc::Composer*>();
	emit composer->sendRequested(QStringLiteral("Too late"), false);

	QCOMPARE(replied.count(), 0);

	QStringList notes;
	for (QLabel* one : pane.findChildren<QLabel*>()) notes.append(one->text());

	const QString all = notes.join(QLatin1Char('\n'));

	QVERIFY2(all.contains(QStringLiteral("has been withdrawn")), qPrintable(all));
	QVERIFY2(all.contains(QStringLiteral("kept here")), qPrintable(all));
}

void TestBoards::aDescriptorForAnotherBoardStartsAgain()
{
	Qdc::BoardPane pane;

	Qdc::BoardInfo first;
	first.name = QStringLiteral("general");
	first.permissions = 1;

	pane.setBoard(first);
	pane.addEntry(entry(QStringLiteral("ROOT"), base()));
	pane.read(QStringLiteral("ROOT"));

	QCOMPARE(pane.threads()->postCount(), 1);

	/*
	 * Another board is another board, even on the same hub. Keeping the first board's
	 * entries beside the second's would make a third that exists nowhere.
	 */
	Qdc::BoardInfo second;
	second.name = QStringLiteral("announcements");
	second.permissions = 1;

	pane.setBoard(second);

	QCOMPARE(pane.threads()->postCount(), 0);
	QVERIFY(pane.reading().isEmpty());
	QCOMPARE(pane.board(), QStringLiteral("announcements"));
}

/**
 * Nobody is asked to refresh a board.
 *
 * Reading the whole board again is how a hub that reordered or dropped entries becomes
 * visible at all, and it is still done - by connecting to the hub, which is when a
 * subscription is taken out again. What is gone is the button: a board arrives as it
 * happens, and a control asking a reader to check whether their client is up to date
 * hands them a job that is not theirs.
 */
void TestBoards::theBoardIsNotSomethingAReaderRefreshes()
{
	Qdc::BoardPane pane;

	pane.addEntry(entry(QStringLiteral("ROOT"), base()));

	for (QPushButton* one : pane.findChildren<QPushButton*>())
	{
		QVERIFY2(!one->text().contains(QStringLiteral("read the board"),
		                               Qt::CaseInsensitive),
		         qPrintable(one->text()));

		QVERIFY2(!one->text().contains(QStringLiteral("refresh"), Qt::CaseInsensitive),
		         qPrintable(one->text()));
	}

	QVERIFY(!pane.findChild<QPushButton*>(QStringLiteral("boardReread")));

	/* The way of saying it is kept, and nothing in the pane says it. */
	QSignalSpy again(&pane, &Qdc::BoardPane::rereadRequested);
	QVERIFY(again.isValid());
	QCOMPARE(again.count(), 0);
}

/**
 * The pane says which post the reading pane is showing, and says it when that changes.
 *
 * What a client outside the pane needs in order to know a post has been read, and the
 * reason it is a signal of its own: read means opened by somebody, which is neither the
 * row that happens to be selected in a list nor the body having been fetched.
 */
void TestBoards::thePaneSaysWhichPostItIsShowing()
{
	Qdc::BoardPane pane;

	pane.addEntries({entry(QStringLiteral("ROOT"), base()),
	                 entry(QStringLiteral("NEXT"), base() + 60)});

	QSignalSpy showing(&pane, &Qdc::BoardPane::readingChanged);
	QVERIFY(showing.isValid());

	pane.read(QStringLiteral("ROOT"));

	QCOMPARE(showing.count(), 1);
	QCOMPARE(showing.at(0).at(0).toString(), QStringLiteral("ROOT"));
	QCOMPARE(pane.reading(), QStringLiteral("ROOT"));

	/* The same post again is not another post: nothing new has been opened. */
	pane.read(QStringLiteral("ROOT"));
	QCOMPARE(showing.count(), 1);

	pane.read(QStringLiteral("NEXT"));

	QCOMPARE(showing.count(), 2);
	QCOMPARE(showing.at(1).at(0).toString(), QStringLiteral("NEXT"));

	/*
	 * And a post chosen in the list rather than asked for by hash arrives the same way,
	 * which is the point of the pane saying it rather than a caller watching the list.
	 */
	QTreeView* list = pane.findChild<QTreeView*>(QStringLiteral("boardThreads"));
	QVERIFY(list);

	list->setCurrentIndex(pane.threads()->indexOf(QStringLiteral("ROOT")));

	QCOMPARE(showing.count(), 3);
	QCOMPARE(showing.at(2).at(0).toString(), QStringLiteral("ROOT"));

	/*
	 * Clearing the board is a change too: nothing is being shown afterwards, and a
	 * caller left believing the last post is still open would be believing it for as
	 * long as the pane lives.
	 */
	pane.clear();

	QCOMPARE(showing.count(), 4);
	QVERIFY(showing.at(3).at(0).toString().isEmpty());

	/* Clearing what is already empty says nothing. */
	pane.clear();
	QCOMPARE(showing.count(), 4);
}

/**
 * A picture a post names is a chip until this machine holds the file, and is drawn
 * without the post being opened again once it does.
 *
 * An attachment to a chat message is fetched within minutes by the people present; one
 * attached to a post may be asked for years later by whoever has just read it - and that
 * reader is looking at the post while it arrives. A post whose attachment can no longer
 * be had at all is still a post: the body renders and the reference is shown either way.
 */
void TestBoards::aPictureInAPostIsDrawnOnceItsFileIsHere()
{
	const QString tth = QStringLiteral("BWXFLX5NO4T54FMVTGZ6UXER2QKN6J2IGGL3W7B");

	Qdc::RichText::Attachment picture;
	picture.tth = tth;
	picture.name = QStringLiteral("plan.png");
	picture.size = 524288;

	const QString root = QStringLiteral("ROOT");

	Qdc::BoardPane pane;

	pane.addEntry(entry(root, base()));
	pane.read(root);

	pane.setPost(post(root, QStringLiteral("the plan is ![the plan](%1)")
		.arg(Qdc::RichText::writeMagnet(picture)), true));

	QTextBrowser* shown = pane.findChild<QTextBrowser*>(QStringLiteral("boardBody"));
	QVERIFY(shown);

	/* Nobody has been asked anything yet, so the reference is something to click. */
	QVERIFY(!shown->isHidden());
	QVERIFY2(!shown->toHtml().contains(QStringLiteral("<img")),
	         qPrintable(shown->toHtml()));

	QVERIFY(shown->toPlainText().contains(QStringLiteral("plan.png")));
	QVERIFY(shown->toPlainText().contains(QStringLiteral("the plan is")));

	bool arrived = false;

	pane.setMediaResolver([&arrived, tth](const QString& asked) {
		Qdc::RichText::LocalMedia here;
		if (!arrived || asked != tth) return here;

		here.path = QStringLiteral("/tmp/plan.png");
		here.size = QSize(800, 600);

		return here;
	});

	/* Asked, and the answer is that the file is not here. Still a chip. */
	QVERIFY2(!shown->toHtml().contains(QStringLiteral("<img")),
	         qPrintable(shown->toHtml()));

	arrived = true;
	pane.mediaChanged();

	/* And now it is drawn, without the post having been opened again. */
	QVERIFY2(shown->toHtml().contains(QStringLiteral("/tmp/plan.png")),
	         qPrintable(shown->toHtml()));

	/* The words around it survive, which is the post still being a post. */
	QVERIFY(shown->toPlainText().contains(QStringLiteral("the plan is")));
}

QTEST_MAIN(TestBoards)

#include "tst_boards.moc"

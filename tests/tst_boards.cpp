/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <gui/boardmodel.h>
#include <gui/boardpane.h>
#include <gui/composer.h>
#include <gui/richtext.h>

#include <QAbstractButton>
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QTextBrowser>
#include <QTest>
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

	void eachStateOfAPostIsDrawnAsADifferentThing();
	void anAnnouncedPostIsNotDrawnAsThoughItHadBeenRead();
	void aDisputedPostIsAttributedToTheIndexAndNotToTheDocument();
	void aPlainBodyIsNeverRenderedAsMarkdown();
	void theWordDeleteAppearsNowhereInThePane();

	void permissionsDecideWhatIsOffered();
	void aRefusalIsReportedInThePane();
	void aGapThatCannotBeClosedIsSaidSo();
	void aSubmittedThreadIsNotPresentedAsPublished();
	void aReplyKeepsTheParentItWasBegunOn();
	void aReplyToAPostWithdrawnWhileWritingIsNotSent();
	void aDescriptorForAnotherBoardStartsAgain();
	void rereadingTheBoardIsOffered();
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

	QVERIFY2(cell(model, orphan, Qdc::BoardModel::ParentColumn)
	         	.contains(QStringLiteral("missing")),
	         qPrintable(cell(model, orphan, Qdc::BoardModel::ParentColumn)));

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

	QVERIFY2(cell(model, reply, Qdc::BoardModel::ParentColumn)
	         	.contains(QStringLiteral("withdrawn")),
	         qPrintable(cell(model, reply, Qdc::BoardModel::ParentColumn)));

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

	const QString doctored = QStringLiteral("Notes‮gpj.exe");

	model.add(entry(QStringLiteral("ROOT"), base(), QString(), QString(), doctored));

	const QString shown = cell(model, QStringLiteral("ROOT"),
	                           Qdc::BoardModel::SubjectColumn);

	/* Marked rather than removed: a name doctored that way should look doctored. */
	QVERIFY(!shown.contains(QChar(0x202e)));
	QVERIFY(shown.contains(QChar(QChar::ReplacementCharacter)));

	/* And what is held is still what the hub sent, because that is what identifies it. */
	QCOMPARE(model.entryOf(QStringLiteral("ROOT")).subject, doctored);
}

void TestBoards::eachStateOfAPostIsDrawnAsADifferentThing()
{
	Qdc::BoardPane pane;

	const QString root = QStringLiteral("ROOT");

	pane.addEntry(entry(root, base()));
	pane.read(root);

	QLabel* state = label(pane, "boardState");
	QVERIFY(state);

	QSet<QString> said;

	/* Announced: the entry is held and nothing has been fetched. */
	QCOMPARE(pane.threads()->stateOf(root), Qdc::PostState::Announced);
	said.insert(state->text());

	/* Fetching: asked for, on the reader's own action. */
	button(pane, "boardFetch")->click();
	QCOMPARE(pane.threads()->stateOf(root), Qdc::PostState::Fetching);
	said.insert(state->text());

	/* Verified: the bytes hashed to the root the entry named. */
	pane.setPost(post(root, QStringLiteral("The body")));
	QCOMPARE(pane.threads()->stateOf(root), Qdc::PostState::Verified);
	said.insert(state->text());

	/* Disputed: verified bytes, and two different claims about who wrote them. */
	pane.setPost(post(root, QStringLiteral("The body"), false,
	                  QStringLiteral("CIDTWO")));
	QCOMPARE(pane.threads()->stateOf(root), Qdc::PostState::Disputed);
	said.insert(state->text());

	/* Unavailable: nobody holding it could be reached, and the entry is kept. */
	pane.setPostUnavailable(root, QStringLiteral("no sources answered"));
	QCOMPARE(pane.threads()->stateOf(root), Qdc::PostState::Unavailable);
	QVERIFY(pane.threads()->holds(root));
	said.insert(state->text());

	QVERIFY2(state->text().contains(QStringLiteral("could not be retrieved")),
	         qPrintable(state->text()));
	QVERIFY2(state->text().contains(QStringLiteral("no sources answered")),
	         qPrintable(state->text()));
	QVERIFY(button(pane, "boardFetch")->text() == QStringLiteral("Try again"));

	/* Withdrawn: the row leaves the list and the pane says what happened. */
	pane.addEntry(tombstone(root, base() + 100));
	QCOMPARE(pane.threads()->stateOf(root), Qdc::PostState::Withdrawn);
	said.insert(state->text());

	QCOMPARE(label(pane, "boardSubject")->text(), QStringLiteral("Withdrawn post"));
	QVERIFY(pane.findChild<QTextBrowser*>(QStringLiteral("boardBody"))->isHidden());

	/* Six states, six things said. Collapsing any two would be presenting a claim. */
	QCOMPARE(said.size(), 6);

	/* And the model has its own word for each of them, in a column of its own. */
	QSet<QString> words;

	for (const Qdc::PostState one : {Qdc::PostState::Announced, Qdc::PostState::Fetching,
	                                 Qdc::PostState::Verified, Qdc::PostState::Disputed,
	                                 Qdc::PostState::Unavailable})
	{
		Qdc::BoardModel model;
		model.add(entry(root, base()));
		model.setState(root, one);

		words.insert(cell(model, root, Qdc::BoardModel::DocumentColumn));
	}

	QCOMPARE(words.size(), 5);
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

	QStringList notes;
	for (QLabel* one : pane.findChildren<QLabel*>()) notes.append(one->text());

	const QString all = notes.join(QLatin1Char('\n'));

	QVERIFY2(all.contains(QStringLiteral("hub's index line")), qPrintable(all));
	QVERIFY2(all.contains(QStringLiteral("has been verified")), qPrintable(all));

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

void TestBoards::aDisputedPostIsAttributedToTheIndexAndNotToTheDocument()
{
	Qdc::BoardPane pane;

	const QString root = QStringLiteral("ROOT");

	pane.addEntry(entry(root, base(), QString(), QString(), QStringLiteral("A subject"),
	                    QStringLiteral("HUBSAIDTHIS")));
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

	QVERIFY2(all.contains(QStringLiteral("HUBSAIDTHIS")), qPrintable(all));
	QVERIFY2(all.contains(QStringLiteral("DOCUMENTSAIDTHIS")), qPrintable(all));

	/* The hub's claim is the attribution and the document's is the disagreement. */
	const int attributed = all.indexOf(QStringLiteral("accepted this from the session"));
	const int claimed = all.indexOf(QStringLiteral("claims a different author"));

	QVERIFY(attributed >= 0);
	QVERIFY(claimed > attributed);

	/* Nothing anywhere calls either of them a verified identity. */
	QVERIFY2(all.contains(QStringLiteral("not a verified identity")), qPrintable(all));
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
	button(pane, "boardNewThread")->click();
	button(pane, "boardReply")->click();

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
	 * Withdrawal takes an entry out of an index. It cannot take the document back from
	 * everybody who has read it, so an interface offering to delete a post promises what
	 * this extension has no way of doing.
	 */
	const QString all = drawnText.join(QLatin1Char('\n'));

	QVERIFY2(!all.contains(QStringLiteral("delete"), Qt::CaseInsensitive),
	         qPrintable(all));

	QVERIFY(all.contains(QStringLiteral("Withdraw")));
}

void TestBoards::permissionsDecideWhatIsOffered()
{
	Qdc::BoardPane pane;

	Qdc::BoardInfo readOnly;
	readOnly.name = QStringLiteral("announcements");
	readOnly.permissions = static_cast<quint32>(Qdc::BoardPermission::Subscribe);

	pane.setBoard(readOnly);
	pane.addEntry(entry(QStringLiteral("ROOT"), base()));
	pane.read(QStringLiteral("ROOT"));

	QVERIFY(!button(pane, "boardNewThread")->isEnabled());
	QVERIFY(button(pane, "boardReply")->isHidden());
	QVERIFY(button(pane, "boardWithdraw")->isHidden());

	/* Reply without start-a-thread is what an announcements board is. */
	Qdc::BoardInfo replies = readOnly;
	replies.permissions |= static_cast<quint32>(Qdc::BoardPermission::Reply);
	replies.permissions |= static_cast<quint32>(Qdc::BoardPermission::WithdrawOwn);

	pane.setBoard(replies);

	QVERIFY(!button(pane, "boardNewThread")->isEnabled());
	QVERIFY(!button(pane, "boardReply")->isHidden());
	QVERIFY(!button(pane, "boardWithdraw")->isHidden());

	QSignalSpy withdrawn(&pane, &Qdc::BoardPane::withdrawRequested);
	button(pane, "boardWithdraw")->click();

	QCOMPARE(withdrawn.count(), 1);
	QCOMPARE(withdrawn.first().first().toString(), QStringLiteral("ROOT"));
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

	/* One that names a board this pane knows nothing about is still reported. */
	pane.reportRefusal(71, QStringLiteral("No such board"), QString(),
	                   QStringLiteral("genrel"));

	QVERIFY2(bands(pane).at(1)->text().contains(QStringLiteral("genrel")),
	         qPrintable(bands(pane).at(1)->text()));
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

void TestBoards::aSubmittedThreadIsNotPresentedAsPublished()
{
	Qdc::BoardPane pane;

	Qdc::BoardInfo board;
	board.name = QStringLiteral("general");
	board.permissions = 1 + 2 + 4;

	pane.setBoard(board);

	button(pane, "boardNewThread")->click();

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
	 * There is no success message in this protocol: the index entry is the
	 * acknowledgement and the only one. So nothing here says the post went up, and what
	 * was written is still in the box in case the hub declines it.
	 */
	QStringList notes;
	for (QLabel* one : pane.findChildren<QLabel*>()) notes.append(one->text());

	const QString all = notes.join(QLatin1Char('\n'));

	QVERIFY2(all.contains(QStringLiteral("not published until")), qPrintable(all));
	QVERIFY2(!all.contains(QStringLiteral("Posted"), Qt::CaseInsensitive)
	         || all.contains(QStringLiteral("not published")), qPrintable(all));

	QCOMPARE(subject->text(), QStringLiteral("Hub upgrade on Saturday"));
	QVERIFY(pane.threads()->postCount() == 0);
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

void TestBoards::rereadingTheBoardIsOffered()
{
	Qdc::BoardPane pane;

	pane.addEntry(entry(QStringLiteral("ROOT"), base()));

	QSignalSpy again(&pane, &Qdc::BoardPane::rereadRequested);

	button(pane, "boardReread")->click();
	QCOMPARE(again.count(), 1);

	/*
	 * And nothing is thrown away by asking: what arrives is reconciled against what is
	 * held, which is the whole point of reading the board again.
	 */
	QCOMPARE(pane.threads()->postCount(), 1);
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

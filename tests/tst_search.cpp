/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/corethread.h>
#include <core/searchbridge.h>
#include <core/transferbridge.h>
#include <gui/searchmodel.h>
#include <gui/searchview.h>

#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QTest>
#include <QTreeView>

/**
 * Searching: how replies are grouped, and how each one finds the search it answers.
 *
 * None of this needs a hub. Sending is one call into the core and there is nothing
 * to assert about it without somebody on the other end, so what is tested here is
 * everything that happens to a reply after it arrives - which is where the decisions
 * are.
 *
 * Replies are fed in as SearchHit values, exactly as HubBridge emits them, so the
 * tests exercise the same path a real reply takes from the point it becomes a Qt
 * value type.
 */
class TestSearch : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();

	void searchModelGroupsTheSameFileFromEveryPeer();
	void searchModelKeepsAnUnhashedResultOnItsOwn();
	void searchModelPrefersTheSourceWithSlotsFree();
	void searchModelCountsSlotsAcrossTheGroup();
	void searchModelTakesThePlainestNameOffered();
	void searchModelCountsAPeerOncePerHub();
	void searchModelNamesAFolderDespiteItsTrailingSeparator();

	void aCategoryBecomesTheExtensionsItStandsFor();
	void aNameIsFiledUnderTheCategoryOfItsExtension();

	void aResultGoesToTheSearchWhoseTokenItCarries();
	void aResultCarryingAnotherSearchesTokenIsDropped();
	void aResultWithoutATokenGoesToTheSearchItsNameMatches();
	void aResultForAClosedSearchIsDroppedWithoutComplaint();

	void resultsCostNoMoreThanTheirNumber();

	void theViewSaysWhenItAskedNoHubs();
	void aSearchWithoutACoreIsNotSent();

	void queueingAResultBringsThePeerThatAnsweredWithIt();
	void queueingTheSameResultTwiceAddsOneSource();

private:
	/** One reply, with only the fields a given test cares about spelled out. */
	static Qdc::SearchHit hit(const QString& name, const QString& tth,
	                          const QString& nick, quint32 freeSlots = 1,
	                          quint64 size = 1024,
	                          const QString& token = QStringLiteral("1"),
	                          const QString& hubUrl
	                              = QStringLiteral("adc://hub.example:7817"));

	/** Type a query into the bar and press the button, as a user would. */
	static void runSearch(Qdc::SearchView& view, const QString& terms);

	static QLabel* summaryOf(Qdc::SearchView& view);

	/**
	 * Take a file out of the queue and wait until it has gone.
	 *
	 * Removing is posted to the core thread and answered by nothing, so a case that
	 * called it and returned left its file queued for however long that took - and the
	 * next case, counting what the queue holds, counted the previous one's file as well.
	 * Which case failed then depended on how busy the machine was.
	 */
	static void dequeue(Qdc::TransferBridge& transfers, const QString& target);

	QTemporaryDir m_dir;
	Qdc::CoreThread* m_thread = nullptr;
};

void TestSearch::dequeue(Qdc::TransferBridge& transfers, const QString& target)
{
	transfers.removeFromQueue(target);

	const auto gone = [&transfers, &target]() {
		transfers.refresh();

		for (const Qdc::QueueEntry& one : transfers.queueSnapshot())
			if (one.filename == target) return false;

		return true;
	};

	QTRY_VERIFY_WITH_TIMEOUT(gone(), 5000);
}

Qdc::SearchHit TestSearch::hit(const QString& name, const QString& tth,
                               const QString& nick, quint32 freeSlots, quint64 size,
                               const QString& token, const QString& hubUrl)
{
	Qdc::SearchHit reply;
	reply.hubUrl = hubUrl;
	reply.token = token;
	reply.name = name;
	reply.tth = tth;
	reply.size = size;
	reply.freeSlots = freeSlots;
	reply.user.nick = nick;
	reply.user.sourceId = nick;
	reply.user.slotCount = 3;

	return reply;
}

void TestSearch::runSearch(Qdc::SearchView& view, const QString& terms)
{
	QLineEdit* field = view.findChild<QLineEdit*>();
	QVERIFY(field);

	field->setText(terms);

	for (QPushButton* button : view.findChildren<QPushButton*>())
	{
		if (button->text() == QStringLiteral("Search"))
		{
			button->click();
			return;
		}
	}

	QFAIL("the search button was not found");
}

QLabel* TestSearch::summaryOf(Qdc::SearchView& view)
{
	/*
	 * The last muted label in the tree: the empty page carries one too, and the tab
	 * that was just opened is added after it.
	 */
	QLabel* found = nullptr;

	for (QLabel* label : view.findChildren<QLabel*>())
	{
		if (label->objectName() == QStringLiteral("mutedText")) found = label;
	}

	return found;
}

void TestSearch::initTestCase()
{
	QVERIFY2(m_dir.isValid(), "could not create a temporary directory");

	m_thread = new Qdc::CoreThread(this);
	m_thread->setPreferenceFiles(
		QDir(m_dir.path()).filePath(QStringLiteral("quickdc.conf")),
		QDir(m_dir.path()).filePath(QStringLiteral("session.conf")),
		QDir(m_dir.path()).filePath(QStringLiteral("queue.xml")));

	QVERIFY2(m_thread->startAndWait(),
	         qPrintable(QStringLiteral("the core did not start: ")
	                    + m_thread->failureReason()));
}

void TestSearch::cleanupTestCase()
{
	if (m_thread) m_thread->shutdown();
}

void TestSearch::searchModelGroupsTheSameFileFromEveryPeer()
{
	Qdc::SearchModel model;

	const QString root = QStringLiteral("MWXFLX5NO4T54FMVTGZ6UXER2QKN6J2IGGL3W7A");

	model.add(hit(QStringLiteral("/music/album.zip"), root, QStringLiteral("alice")));
	model.add(hit(QStringLiteral("/shared/album.zip"), root, QStringLiteral("bob")));
	model.add(hit(QStringLiteral("album.zip"), root, QStringLiteral("carol")));

	/* One file, three ways to get it. */
	QCOMPARE(model.fileCount(), 1);
	QCOMPARE(model.hitCount(), 3);
	QCOMPARE(model.rowCount(), 1);

	const QModelIndex file = model.index(0, Qdc::SearchModel::NameColumn);
	QCOMPARE(model.rowCount(file), 3);

	QCOMPARE(model.data(model.index(0, Qdc::SearchModel::UsersColumn)).toInt(), 3);

	/* And the peers are under it rather than beside it. */
	const QModelIndex peer = model.index(1, Qdc::SearchModel::NameColumn, file);
	QVERIFY(peer.isValid());
	QCOMPARE(model.parent(peer), file);
	QCOMPARE(model.data(peer).toString(), QStringLiteral("bob"));
}

void TestSearch::searchModelKeepsAnUnhashedResultOnItsOwn()
{
	Qdc::SearchModel model;

	/*
	 * Without a root there is nothing that says two files are the same file, so the
	 * same name and size from two peers stays two rows rather than being guessed at.
	 */
	model.add(hit(QStringLiteral("notes.txt"), QString(), QStringLiteral("alice")));
	model.add(hit(QStringLiteral("notes.txt"), QString(), QStringLiteral("bob")));

	QCOMPARE(model.fileCount(), 2);
	QCOMPARE(model.rowCount(), 2);
	QCOMPARE(model.rowCount(model.index(0, 0)), 1);
}

void TestSearch::searchModelPrefersTheSourceWithSlotsFree()
{
	Qdc::SearchModel model;

	const QString root = QStringLiteral("MWXFLX5NO4T54FMVTGZ6UXER2QKN6J2IGGL3W7A");

	model.add(hit(QStringLiteral("album.zip"), root, QStringLiteral("busy"), 0));
	model.add(hit(QStringLiteral("album.zip"), root, QStringLiteral("free"), 4));
	model.add(hit(QStringLiteral("album.zip"), root, QStringLiteral("someslots"), 2));

	/* Acting on the collapsed file row asks whoever is most likely to answer. */
	const Qdc::SearchHit best = model.hitAt(model.index(0, 0));
	QCOMPARE(best.user.nick, QStringLiteral("free"));

	/* A peer row still stands for itself. */
	const QModelIndex file = model.index(0, 0);
	QCOMPARE(model.hitAt(model.index(0, 0, file)).user.nick, QStringLiteral("busy"));
}

void TestSearch::searchModelCountsSlotsAcrossTheGroup()
{
	Qdc::SearchModel model;

	const QString root = QStringLiteral("MWXFLX5NO4T54FMVTGZ6UXER2QKN6J2IGGL3W7A");

	model.add(hit(QStringLiteral("album.zip"), root, QStringLiteral("alice"), 1));
	model.add(hit(QStringLiteral("album.zip"), root, QStringLiteral("bob"), 2));

	/* Free of advertised, added up: two free out of the six the two of them claim. */
	QCOMPARE(model.data(model.index(0, Qdc::SearchModel::SlotsColumn)).toString(),
	         QStringLiteral("3/6"));
}

void TestSearch::searchModelTakesThePlainestNameOffered()
{
	Qdc::SearchModel model;

	const QString root = QStringLiteral("MWXFLX5NO4T54FMVTGZ6UXER2QKN6J2IGGL3W7A");

	/*
	 * A name is a path in somebody's share. The file row shows the name out of it,
	 * and where two peers disagree it shows the shorter, which carries less of
	 * whatever either of them called their folders.
	 */
	model.add(hit(QStringLiteral("/incoming/2026/Some Album (2026)/album.zip"), root,
	              QStringLiteral("alice")));
	model.add(hit(QStringLiteral("/music/album.zip"), root, QStringLiteral("bob")));

	QCOMPARE(model.data(model.index(0, Qdc::SearchModel::NameColumn)).toString(),
	         QStringLiteral("album.zip"));
}

void TestSearch::searchModelCountsAPeerOncePerHub()
{
	Qdc::SearchModel model;

	const QString one = QStringLiteral("MWXFLX5NO4T54FMVTGZ6UXER2QKN6J2IGGL3W7A");
	const QString two = QStringLiteral("AWXFLX5NO4T54FMVTGZ6UXER2QKN6J2IGGL3W7B");

	/* The same nick answering twice is one peer; on another hub it is another. */
	model.add(hit(QStringLiteral("a.zip"), one, QStringLiteral("alice")));
	model.add(hit(QStringLiteral("b.zip"), two, QStringLiteral("alice")));

	QCOMPARE(model.fileCount(), 2);
	QCOMPARE(model.peerCount(), 1);

	model.add(hit(QStringLiteral("c.zip"), QString(), QStringLiteral("alice"), 1, 1024,
	              QStringLiteral("1"), QStringLiteral("adc://elsewhere:7817")));

	QCOMPARE(model.peerCount(), 2);
}

void TestSearch::searchModelNamesAFolderDespiteItsTrailingSeparator()
{
	Qdc::SearchModel model;

	/*
	 * A hub answers a search with folders as well as files, and marks a folder by
	 * the separator on the end of its name. Taken as a path that is a name of
	 * nothing at all, so a folder shows as a blank row unless the separator comes
	 * off first - which is what real hubs turned up.
	 */
	Qdc::SearchHit folder = hit(QStringLiteral("/Music/Live Recordings/"), QString(),
	                            QStringLiteral("alice"));
	folder.directory = true;

	model.add(folder);

	QCOMPARE(model.data(model.index(0, Qdc::SearchModel::NameColumn)).toString(),
	         QStringLiteral("Live Recordings"));

	/* And it is called a folder rather than being classified by an extension. */
	QCOMPARE(model.data(model.index(0, Qdc::SearchModel::TypeColumn)).toString(),
	         QStringLiteral("Folder"));

	/* The hit keeps the name as the peer gave it, since that is what identifies it. */
	QCOMPARE(model.hitAt(model.index(0, 0)).name,
	         QStringLiteral("/Music/Live Recordings/"));
}

void TestSearch::aCategoryBecomesTheExtensionsItStandsFor()
{
	const QStringList audio =
		Qdc::SearchBridge::extensionsFor(Qdc::SearchCategory::Audio);

	QVERIFY(audio.contains(QStringLiteral("flac")));
	QVERIFY(audio.contains(QStringLiteral("mp3")));
	QVERIFY(!audio.contains(QStringLiteral("mkv")));

	/* Anything asks for no extension at all, which is what searches for anything. */
	QVERIFY(Qdc::SearchBridge::extensionsFor(Qdc::SearchCategory::Any).isEmpty());
}

void TestSearch::aNameIsFiledUnderTheCategoryOfItsExtension()
{
	using Qdc::SearchBridge;
	using Qdc::SearchCategory;

	QCOMPARE(SearchBridge::categoryOf(QStringLiteral("/a/b/song.FLAC")),
	         SearchCategory::Audio);
	QCOMPARE(SearchBridge::categoryOf(QStringLiteral("holiday.mkv")),
	         SearchCategory::Video);
	QCOMPARE(SearchBridge::categoryOf(QStringLiteral("book.epub")),
	         SearchCategory::Document);

	/* No extension, and an extension in no group, are both simply unclassified. */
	QCOMPARE(SearchBridge::categoryOf(QStringLiteral("README")), SearchCategory::Any);
	QCOMPARE(SearchBridge::categoryOf(QStringLiteral("thing.wibble")),
	         SearchCategory::Any);
}

void TestSearch::aResultGoesToTheSearchWhoseTokenItCarries()
{
	Qdc::SearchBridge bridge(m_thread);
	Qdc::SearchView view(&bridge, nullptr, nullptr, nullptr);

	runSearch(view, QStringLiteral("album"));
	runSearch(view, QStringLiteral("holiday"));

	QList<QTreeView*> trees = view.findChildren<QTreeView*>();
	QCOMPARE(trees.size(), 2);

	/*
	 * The tokens are the bridge's, and the first search gets the first one. A reply
	 * carrying it belongs to that tab however little its name has to do with the
	 * query - the token is the answer, not the name.
	 */
	view.addHit(hit(QStringLiteral("album.zip"), QString(), QStringLiteral("alice"), 1,
	                1024, QStringLiteral("1")));

	view.addHit(hit(QStringLiteral("nothing-alike.bin"), QString(),
	                QStringLiteral("bob"), 1, 1024, QStringLiteral("2")));

	QCOMPARE(trees.at(0)->model()->rowCount(), 1);
	QCOMPARE(trees.at(1)->model()->rowCount(), 1);

	QCOMPARE(trees.at(1)->model()->index(0, 0).data().toString(),
	         QStringLiteral("nothing-alike.bin"));
}

void TestSearch::aResultCarryingAnotherSearchesTokenIsDropped()
{
	Qdc::SearchBridge bridge(m_thread);
	Qdc::SearchView view(&bridge, nullptr, nullptr, nullptr);

	runSearch(view, QStringLiteral("album"));

	QTreeView* tree = view.findChild<QTreeView*>();
	QVERIFY(tree);

	/* A token from no search of ours: somebody else's reply, or a very late one. */
	view.addHit(hit(QStringLiteral("album.zip"), QString(), QStringLiteral("alice"), 1,
	                1024, QStringLiteral("999")));

	QCOMPARE(tree->model()->rowCount(), 0);
}

void TestSearch::aResultWithoutATokenGoesToTheSearchItsNameMatches()
{
	Qdc::SearchBridge bridge(m_thread);
	Qdc::SearchView view(&bridge, nullptr, nullptr, nullptr);

	runSearch(view, QStringLiteral("album"));
	runSearch(view, QStringLiteral("holiday photos"));

	QList<QTreeView*> trees = view.findChildren<QTreeView*>();
	QCOMPARE(trees.size(), 2);

	/*
	 * NMDC carries no token, so the name is all there is to go on. Every word of the
	 * query has to appear, which is what keeps a reply out of a search it does not
	 * answer.
	 */
	view.addHit(hit(QStringLiteral("my-album.zip"), QString(), QStringLiteral("alice"),
	                1, 1024, QString()));

	view.addHit(hit(QStringLiteral("holiday-photos-2026.zip"), QString(),
	                QStringLiteral("bob"), 1, 1024, QString()));

	/* And one that matches neither is dropped rather than being guessed at. */
	view.addHit(hit(QStringLiteral("kernel.tar.xz"), QString(), QStringLiteral("carol"),
	                1, 1024, QString()));

	QCOMPARE(trees.at(0)->model()->rowCount(), 1);
	QCOMPARE(trees.at(1)->model()->rowCount(), 1);
}

void TestSearch::aResultForAClosedSearchIsDroppedWithoutComplaint()
{
	Qdc::SearchBridge bridge(m_thread);
	Qdc::SearchView view(&bridge, nullptr, nullptr, nullptr);

	runSearch(view, QStringLiteral("album"));

	QTabWidget* tabs = view.findChild<QTabWidget*>();
	QVERIFY(tabs);
	QCOMPARE(tabs->count(), 1);

	emit tabs->tabCloseRequested(0);
	QCOMPARE(tabs->count(), 0);

	/*
	 * Peers go on answering a search for a while after nobody is listening, and
	 * closing the tab is how somebody says they have stopped caring.
	 */
	view.addHit(hit(QStringLiteral("album.zip"), QString(), QStringLiteral("alice"), 1,
	                1024, QStringLiteral("1")));

	QVERIFY(view.findChildren<QTreeView*>().isEmpty());
}

/**
 * A wide search answers in the thousands, and the cost of taking them has to grow
 * with their number rather than with its square.
 *
 * It used to be the square. Every arriving reply recounted the peers by walking
 * every hit of every group and building a fresh set of keys, and the summary under
 * the results was refreshed once per reply - so a search that found five thousand
 * files did twenty-five million string comparisons on the thread that draws, and
 * the window stopped answering until it was finished.
 */
void TestSearch::resultsCostNoMoreThanTheirNumber()
{
	const auto feed = [this](int count) {
		Qdc::SearchView view(nullptr, nullptr, nullptr, nullptr);
		runSearch(view, QStringLiteral("album"));

		QVector<Qdc::SearchHit> hits;
		hits.reserve(count);

		/* One file each and one peer each, which is the worst case for both. */
		for (int n = 0; n < count; ++n)
		{
			hits.append(hit(QStringLiteral("album-%1.zip").arg(n),
			                QStringLiteral("ROOT%1").arg(n),
			                QStringLiteral("peer-%1").arg(n)));
		}

		QElapsedTimer timer;
		timer.start();

		/* One at a time, as they arrive from a hub, rather than as one batch. */
		for (const Qdc::SearchHit& one : hits) view.addHit(one);

		return timer.nsecsElapsed();
	};

	/* A first run pays for whatever the process has not warmed up yet. */
	feed(200);

	const qint64 small = feed(1000);
	const qint64 large = feed(5000);

	/*
	 * Five times the replies for no more than fifteen times the work. Generous
	 * against a loaded machine and still nowhere near the twenty-five a square
	 * would cost.
	 */
	const double growth = small > 0 ? double(large) / double(small) : 0.0;

	qWarning() << "1000 replies took" << (small / 1000000) << "ms,"
	           << "5000 took" << (large / 1000000) << "ms, growth" << growth;

	QVERIFY2(growth < 15.0,
	         qPrintable(QStringLiteral("five times the replies cost %1 times the work, "
	                                   "which is the count being walked again for each "
	                                   "one of them").arg(growth)));
}

void TestSearch::theViewSaysWhenItAskedNoHubs()
{
	Qdc::SearchBridge bridge(m_thread);
	Qdc::SearchView view(&bridge, nullptr, nullptr, nullptr);

	QSignalSpy sent(&bridge, &Qdc::SearchBridge::searchSent);

	runSearch(view, QStringLiteral("album"));

	/* The count comes back from the core thread, so wait for it. */
	QVERIFY(sent.wait(4000));
	QCOMPARE(sent.count(), 1);
	QCOMPARE(sent.first().at(1).toInt(), 0);

	QLabel* summary = summaryOf(view);
	QVERIFY(summary);

	/*
	 * Nothing is connected in a test, and that is the interesting case: a search
	 * that went nowhere has to say so rather than sit there looking as though it is
	 * waiting for an answer.
	 */
	QVERIFY2(summary->text().contains(QStringLiteral("nothing is connected")),
	         qPrintable(summary->text()));
}

void TestSearch::aSearchWithoutACoreIsNotSent()
{
	/* No core thread at all: start() has nothing to ask and says so with no token. */
	Qdc::SearchBridge bridge(nullptr);

	Qdc::SearchQuery query;
	query.terms = QStringLiteral("album");

	QVERIFY(bridge.start(query).isEmpty());
}

void TestSearch::queueingAResultBringsThePeerThatAnsweredWithIt()
{
	Qdc::TransferBridge transfers(m_thread);

	const QString target = QDir(m_dir.path()).filePath(QStringLiteral("album.flac.zip"));

	Qdc::SearchHit reply = hit(QStringLiteral("/music/album.flac.zip"),
	                           QStringLiteral("MWXFLX5NO4T54FMVTGZ6UXER2QKN6J2IGGL3W7A"),
	                           QStringLiteral("alice"), 3, 4096);

	transfers.enqueue(target, reply);

	/*
	 * The point of the overload: a reply says who has the file, so the entry arrives
	 * with somewhere to get it from rather than waiting for the queue to go looking.
	 */
	QSignalSpy fresh(&transfers, &Qdc::TransferBridge::snapshotChanged);
	transfers.refresh();
	QVERIFY(fresh.wait(5000));

	const QVector<Qdc::QueueEntry> queued = transfers.queueSnapshot();
	QCOMPARE(queued.size(), 1);
	QCOMPARE(queued.first().filename, target);
	QCOMPARE(queued.first().sources, 1u);

	dequeue(transfers, target);
}

void TestSearch::queueingTheSameResultTwiceAddsOneSource()
{
	Qdc::TransferBridge transfers(m_thread);

	const QString target = QDir(m_dir.path()).filePath(QStringLiteral("twice.zip"));

	const Qdc::SearchHit reply =
		hit(QStringLiteral("/music/twice.zip"),
		    QStringLiteral("AWXFLX5NO4T54FMVTGZ6UXER2QKN6J2IGGL3W7B"),
		    QStringLiteral("alice"), 1, 4096);

	/* Somebody clicking Download twice must not make the peer count two. */
	transfers.enqueue(target, reply);
	transfers.enqueue(target, reply);

	QSignalSpy fresh(&transfers, &Qdc::TransferBridge::snapshotChanged);
	transfers.refresh();
	QVERIFY(fresh.wait(5000));

	const QVector<Qdc::QueueEntry> queued = transfers.queueSnapshot();
	QCOMPARE(queued.size(), 1);
	QCOMPARE(queued.first().sources, 1u);

	dequeue(transfers, target);
}

QTEST_MAIN(TestSearch)

#include "tst_search.moc"

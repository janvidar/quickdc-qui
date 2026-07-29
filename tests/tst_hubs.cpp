/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/configbridge.h>
#include <core/corethread.h>
#include <core/favourites.h>
#include <core/filelistbridge.h>
#include <core/hubbridge.h>
#include <gui/chatmodel.h>
#include <gui/chatpane.h>
#include <gui/filelistview.h>
#include <gui/hubsview.h>
#include <gui/usermodel.h>

#include <QDir>
#include <QSignalSpy>
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

	void favouritesRoundTripThroughTheStore();
	void favouritesShrinkWithoutLeavingATail();

	void urlNormalisationFillsInSchemeAndPort();

	void filelistModelExposesTheTree();
	void filelistModelFilterKeepsAncestors();

	void openingTheSameHubTwiceReusesThePane();
	void privateMessagesOpenAConversation();
	void unreadCountsReachTheRailBadge();
	void hubPasswordIsAskedForOnce();

private:
	Qdc::UserInfo user(const QString& nick, bool op = false) const;

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

#include "tst_hubs.moc"

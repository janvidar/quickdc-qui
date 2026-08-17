/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/configbridge.h>
#include <core/corethread.h>
#include <core/shares.h>
#include <gui/settingsview.h>

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <QTest>

/**
 * Settings: the shared folder list, and the form's binding to the configuration.
 *
 * The form is driven through its own load()/save() rather than by clicking, and
 * the values are checked where they end up - in the core's own preference store -
 * because that is the contract: what is typed here is what the core reads next
 * time it looks.
 */
class TestSettings : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();

	void sharesRoundTripThroughTheStore();
	void sharesRejectDuplicatesAndDescendants();
	void sharesNameDefaultsToTheFolderName();
	void sharesStripTheSeparatorFromNames();
	void sharesShrinkWithoutLeavingATail();

	void formLoadsTheStoredValues();
	void formDoesNotWaitForABusyCore();
	void formWritesWhatTheCoreReads();
	void formTracksBeingEdited();
	void formChunkSizeIsShownInKibibytes();
	void formAddsAFolderWithQtsOwnChooser();

	void activeModeIsNotForcedUntilItIsAskedFor();
	void forcedActiveModeSurvivesTheForm();
	void theConnectionPageSaysWhatForcingActiveModeCosts();

	void boardsFetchNothingAheadUntilTheyAreTold();
	void theBoardsPageSaysThatReadingAPostMayBeSeen();

	void theLocalHubIsOffUntilItIsAskedFor();
	void theLocalHubKeepsItsFiguresInTheUnitsTheCoreReads();
	void theLocalHubPageSaysWhatTurningItOnDoes();

private:
	QString scratch(const QString& name) const;

	/** A field by object name, so the form's internals stay private. */
	template<typename T>
	T* field(Qdc::SettingsView& view, const QString& label) const;

	QTemporaryDir m_dir;
	Qdc::CoreThread* m_thread = nullptr;
	Qdc::ConfigBridge* m_config = nullptr;
};

QString TestSettings::scratch(const QString& name) const
{
	return QDir(m_dir.path()).filePath(name);
}

void TestSettings::initTestCase()
{
	QVERIFY2(m_dir.isValid(), "could not create a temporary directory");

	m_thread = new Qdc::CoreThread(this);
	m_thread->setPreferenceFiles(scratch(QStringLiteral("quickdc.conf")),
	                             scratch(QStringLiteral("session.conf")),
	                             scratch(QStringLiteral("queue.xml")));

	QVERIFY2(m_thread->startAndWait(),
	         qPrintable(QStringLiteral("the core did not start: ")
	                    + m_thread->failureReason()));

	m_config = new Qdc::ConfigBridge(m_thread, this);
}

void TestSettings::cleanupTestCase()
{
	if (m_thread) m_thread->shutdown();
}

void TestSettings::sharesRoundTripThroughTheStore()
{
	const QString music = scratch(QStringLiteral("Music"));
	const QString video = scratch(QStringLiteral("Video"));
	QVERIFY(QDir().mkpath(music));
	QVERIFY(QDir().mkpath(video));

	{
		Qdc::Shares shares(m_config);
		QVERIFY(shares.folders().isEmpty());

		QSignalSpy changes(&shares, &Qdc::Shares::changed);

		QVERIFY(shares.add(music));
		QVERIFY(shares.add(video, QStringLiteral("Films")));
		QCOMPARE(changes.count(), 2);
		QCOMPARE(shares.count(), 2);
	}

	/* A second store reads back what the first wrote, in order. */
	Qdc::Shares reloaded(m_config);
	QCOMPARE(reloaded.count(), 2);
	QCOMPARE(reloaded.folders().at(0).path, music);
	QCOMPARE(reloaded.folders().at(0).name, QStringLiteral("Music"));
	QCOMPARE(reloaded.folders().at(1).path, video);
	QCOMPARE(reloaded.folders().at(1).name, QStringLiteral("Films"));

	/*
	 * Stored exactly as the core parses it: the virtual name, a colon, the real
	 * path. Checked against the raw value, because the core's own reader splits on
	 * that colon and would silently read a different pair if the format drifted.
	 */
	const QString raw = m_config->getString(Qdc::ConfigStore::Application,
		QStringLiteral("Shares"), QStringLiteral("Share 1"));
	QCOMPARE(raw, QStringLiteral("Films:") + video);

	QCOMPARE(m_config->getNumber(Qdc::ConfigStore::Application,
		QStringLiteral("Shares"), QStringLiteral("Total shares")), 2);

	QVERIFY(reloaded.renameAt(1, QStringLiteral("Movies")));
	QCOMPARE(reloaded.folders().at(1).name, QStringLiteral("Movies"));

	/* Renaming to the same thing, or out of range, is not a change. */
	QVERIFY(!reloaded.renameAt(1, QStringLiteral("Movies")));
	QVERIFY(!reloaded.renameAt(9, QStringLiteral("Nope")));

	QVERIFY(reloaded.removeAt(0));
	QVERIFY(!reloaded.removeAt(9));
	QCOMPARE(reloaded.count(), 1);

	reloaded.setFolders({});
	QCOMPARE(reloaded.count(), 0);
}

void TestSettings::sharesRejectDuplicatesAndDescendants()
{
	const QString parent = scratch(QStringLiteral("Library"));
	const QString child = scratch(QStringLiteral("Library/Records"));
	QVERIFY(QDir().mkpath(child));

	Qdc::Shares shares(m_config);
	shares.setFolders({});

	QVERIFY(shares.add(parent));

	/* The same folder twice. */
	QVERIFY(!shares.add(parent));

	/* A trailing separator names the same folder. */
	QVERIFY(!shares.add(parent + QLatin1Char('/')));

	/*
	 * And a folder inside one already offered is already offered - adding it would
	 * put every file in it into the file list twice.
	 */
	QVERIFY(!shares.add(child));
	QCOMPARE(shares.count(), 1);

	/* The other way round is allowed: the parent was not shared before. */
	shares.setFolders({});
	QVERIFY(shares.add(child));
	QVERIFY(shares.add(parent));
	QCOMPARE(shares.count(), 2);

	QVERIFY(!shares.add(QString()));
	QCOMPARE(shares.count(), 2);

	shares.setFolders({});
}

void TestSettings::sharesNameDefaultsToTheFolderName()
{
	const QString folder = scratch(QStringLiteral("Photographs"));
	QVERIFY(QDir().mkpath(folder));

	Qdc::Shares shares(m_config);
	shares.setFolders({});

	QVERIFY(shares.add(folder));
	QCOMPARE(shares.folders().first().name, QStringLiteral("Photographs"));

	shares.setFolders({});
}

void TestSettings::sharesStripTheSeparatorFromNames()
{
	const QString folder = scratch(QStringLiteral("Odd"));
	QVERIFY(QDir().mkpath(folder));

	Qdc::Shares shares(m_config);
	shares.setFolders({});

	/*
	 * A colon in the virtual name would be read back as the end of the name, so it
	 * is removed rather than stored and mis-parsed.
	 */
	QVERIFY(shares.add(folder, QStringLiteral("a:b:c")));
	QCOMPARE(shares.folders().first().name, QStringLiteral("abc"));

	Qdc::Shares reloaded(m_config);
	QCOMPARE(reloaded.folders().first().name, QStringLiteral("abc"));
	QCOMPARE(reloaded.folders().first().path, folder);

	reloaded.setFolders({});
}

void TestSettings::sharesShrinkWithoutLeavingATail()
{
	Qdc::Shares shares(m_config);
	shares.setFolders({});

	QList<Qdc::SharedFolder> many;
	for (int n = 0; n < 5; ++n)
	{
		Qdc::SharedFolder folder;
		folder.name = QStringLiteral("share%1").arg(n);
		folder.path = scratch(QStringLiteral("bulk%1").arg(n));
		many.append(folder);
	}

	shares.setFolders(many);
	QCOMPARE(shares.count(), 5);

	shares.setFolders(many.mid(0, 2));

	/*
	 * The group is numbered, so a shorter list has to clear the keys the longer one
	 * used - otherwise reloading finds the old tail still there.
	 */
	Qdc::Shares reloaded(m_config);
	QCOMPARE(reloaded.count(), 2);

	reloaded.setFolders({});
}

void TestSettings::formLoadsTheStoredValues()
{
	using Qdc::ConfigStore;

	m_config->setString(ConfigStore::Application, QStringLiteral("User"),
	                    QStringLiteral("Nickname"), QStringLiteral("tester"));
	m_config->setString(ConfigStore::Application, QStringLiteral("User"),
	                    QStringLiteral("Description"), QStringLiteral("somewhere"));
	m_config->setNumber(ConfigStore::Application, QStringLiteral("Network"),
	                    QStringLiteral("Slots"), 9);
	m_config->setBool(ConfigStore::Application, QStringLiteral("Network"),
	                  QStringLiteral("Active mode"), false);
	m_config->save(ConfigStore::Application);

	Qdc::Shares shares(m_config);
	Qdc::SettingsView view(m_config, &shares);

	/* Loaded on construction, and not counted as the user having edited it. */
	QVERIFY(!view.isModified());

	/* Round-tripped through save() rather than read out of private fields. */
	QVERIFY(view.save());

	QCOMPARE(m_config->getString(ConfigStore::Application, QStringLiteral("User"),
	                             QStringLiteral("Nickname")),
	         QStringLiteral("tester"));
	QCOMPARE(m_config->getNumber(ConfigStore::Application, QStringLiteral("Network"),
	                             QStringLiteral("Slots")), 9);
	QCOMPARE(m_config->getBool(ConfigStore::Application, QStringLiteral("Network"),
	                           QStringLiteral("Active mode"), true), false);
}

void TestSettings::formDoesNotWaitForABusyCore()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::Shares shares(&config);

	/*
	 * The page used to be read a key at a time, each one a blocking job on the core
	 * thread, so opening it while the core was busy waited out a whole core turn
	 * fourteen times over. It reads its own copy now, and so does the store behind
	 * the share list, and saving posts the write rather than waiting for it.
	 *
	 * The copy is filled when the bridge is built, which is why that happens above
	 * rather than below - it is the one call left in this layer that waits, and it
	 * happens once, at start-up, before there is a window to freeze.
	 */
	m_thread->post([] { QThread::msleep(1500); });

	QElapsedTimer timer;
	timer.start();

	Qdc::SettingsView view(&config, &shares);
	view.load();
	view.save();

	const qint64 spent = timer.elapsed();

	QVERIFY2(spent < 100,
	         qPrintable(QStringLiteral("loading and saving the page took %1 ms while "
	                                   "the core was busy").arg(spent)));
}

void TestSettings::formWritesWhatTheCoreReads()
{
	using Qdc::ConfigStore;

	Qdc::Shares shares(m_config);
	Qdc::SettingsView view(m_config, &shares);

	/*
	 * The fields are private, so the values are set through the store and read
	 * back through the form - which is the binding being tested either way.
	 */
	m_config->setString(ConfigStore::Application, QStringLiteral("Downloads"),
	                    QStringLiteral("Directory"), scratch(QStringLiteral("dl")));
	m_config->setNumber(ConfigStore::Application, QStringLiteral("Network"),
	                    QStringLiteral("Max downloads"), 7);
	m_config->setNumber(ConfigStore::Application, QStringLiteral("Network"),
	                    QStringLiteral("Port"), 4111);

	view.load();
	QVERIFY(!view.isModified());

	QSignalSpy saved(&view, &Qdc::SettingsView::saved);
	QVERIFY(view.save());
	QCOMPARE(saved.count(), 1);

	/* Every key survives the round trip through the form. */
	QCOMPARE(m_config->getString(ConfigStore::Application, QStringLiteral("Downloads"),
	                             QStringLiteral("Directory")),
	         scratch(QStringLiteral("dl")));
	QCOMPARE(m_config->getNumber(ConfigStore::Application, QStringLiteral("Network"),
	                             QStringLiteral("Max downloads")), 7);
	QCOMPARE(m_config->getNumber(ConfigStore::Application, QStringLiteral("Network"),
	                             QStringLiteral("Port")), 4111);
}

void TestSettings::formTracksBeingEdited()
{
	Qdc::Shares shares(m_config);
	Qdc::SettingsView view(m_config, &shares);

	QVERIFY(!view.isModified());

	/*
	 * Typing is what marks the form dirty, and load() populating the fields must
	 * not - which is what the loading guard is for. Simulated by editing a field
	 * found through the object tree, since the members are private.
	 */
	QList<QSpinBox*> spins = view.findChildren<QSpinBox*>();
	QVERIFY(!spins.isEmpty());

	spins.first()->setValue(spins.first()->value() + 1);
	QVERIFY(view.isModified());

	/* Reverting reloads and clears the flag. */
	view.load();
	QVERIFY(!view.isModified());
}

void TestSettings::formChunkSizeIsShownInKibibytes()
{
	using Qdc::ConfigStore;

	/* The core stores bytes; the form offers KiB. */
	m_config->setNumber(ConfigStore::Application, QStringLiteral("Network"),
	                    QStringLiteral("Chunk size"), 2 * 1024 * 1024);
	m_config->save(ConfigStore::Application);

	Qdc::Shares shares(m_config);
	Qdc::SettingsView view(m_config, &shares);

	QVERIFY(view.save());

	/*
	 * Back in bytes, unchanged. A form that showed bytes as KiB and wrote them
	 * back as KiB would divide the chunk size by 1024 every time it was saved.
	 */
	QCOMPARE(m_config->getNumber(ConfigStore::Application, QStringLiteral("Network"),
	                             QStringLiteral("Chunk size")), 2 * 1024 * 1024);
}

void TestSettings::formAddsAFolderWithQtsOwnChooser()
{
	const QString folder = scratch(QStringLiteral("Clicked"));
	QVERIFY(QDir().mkpath(folder));

	Qdc::Shares shares(m_config);
	shares.setFolders({});

	Qdc::SettingsView view(m_config, &shares);

	QPushButton* add = nullptr;
	for (QPushButton* button : view.findChildren<QPushButton*>())
	{
		if (button->text().startsWith(QStringLiteral("Add folder"))) add = button;
	}

	QVERIFY2(add, "the form has no Add folder button");

	QListWidget* list = view.findChildren<QListWidget*>().value(0);
	QVERIFY(list);
	QCOMPARE(list->count(), 0);

	/*
	 * Driven by clicking, unlike the rest of the form, because what broke was the
	 * press and not the store: the chooser was the platform's, and asked for
	 * modally that one returns at once with nothing chosen - a button that did
	 * nothing at all. So the option that refuses it is asserted here as well.
	 */
	bool sawTheChooser = false;
	bool refusedThePlatform = false;
	bool directoriesOnly = false;

	/*
	 * Looked for repeatedly rather than once, because the chooser runs an event loop
	 * of its own: a single shot scheduled before the click is answered before the
	 * chooser is even built, and the loop it then enters is where this has to fire.
	 */
	QTimer watcher;
	watcher.setInterval(20);

	QObject::connect(&watcher, &QTimer::timeout,
	                 [&folder, &sawTheChooser, &refusedThePlatform, &directoriesOnly] {
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			QFileDialog* dialog = qobject_cast<QFileDialog*>(widget);
			if (!dialog) continue;

			refusedThePlatform =
				dialog->testOption(QFileDialog::DontUseNativeDialog);
			directoriesOnly = dialog->fileMode() == QFileDialog::Directory;

			/*
			 * Typed rather than picked out of the listing. The listing is
			 * filled in as the model reads the directory, so selecting a row
			 * that is not there yet leaves whichever folder is open as the
			 * answer; accept() takes the typed path from the filesystem.
			 */
			if (QLineEdit* typed =
				dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit")))
			{
				typed->setText(folder);
			}

			sawTheChooser = true;
			QMetaObject::invokeMethod(dialog, "accept");
			return;
		}
	});

	watcher.start();

	add->click();

	/* Opened a turn later, so the click is over before there is anything to choose. */
	QCOMPARE(list->count(), 0);

	QTRY_VERIFY(sawTheChooser);
	QTRY_COMPARE(list->count(), 1);

	watcher.stop();

	QVERIFY2(refusedThePlatform,
	         "the chooser was left to the platform, which is what did nothing");
	QVERIFY2(directoriesOnly, "the chooser offered files as well as folders");

	QCOMPARE(shares.count(), 1);
	QCOMPARE(shares.folders().first().path, folder);
	QCOMPARE(shares.folders().first().name, QStringLiteral("Clicked"));

	shares.setFolders({});
}

/**
 * Saving the form without touching it leaves active mode unforced.
 *
 * The key is removed first, so the form is showing what is really in force rather than
 * what some other test left behind. The default is then asked for as true, so that the
 * answer cannot be right by accident: the core reads this before it consults the
 * measurement at all, and a client that arrived at it by installing the program would
 * be telling every hub something nothing has established.
 */
void TestSettings::activeModeIsNotForcedUntilItIsAskedFor()
{
	using Qdc::ConfigStore;

	const QString group = QStringLiteral("Network");
	const QString key = QStringLiteral("Force active mode");

	m_config->remove(ConfigStore::Application, group, key);
	m_config->save(ConfigStore::Application);

	Qdc::Shares shares(m_config);
	Qdc::SettingsView view(m_config, &shares);

	QVERIFY(view.save());

	QCOMPARE(m_config->getBool(ConfigStore::Application, group, key, true), false);
}

/**
 * A forced active mode read by the form is written back by it.
 *
 * The key is taken back out of the store between the load() and the save(), so that
 * finding it there afterwards can only mean the form carried it: left in place, this
 * would pass against a page that read the key and never wrote it again. A page that
 * offered the switch but dropped it on the way back would turn the setting off every
 * time anything else on the form was saved.
 */
void TestSettings::forcedActiveModeSurvivesTheForm()
{
	using Qdc::ConfigStore;

	const QString group = QStringLiteral("Network");
	const QString key = QStringLiteral("Force active mode");

	m_config->setBool(ConfigStore::Application, group, key, true);

	Qdc::Shares shares(m_config);
	Qdc::SettingsView view(m_config, &shares);

	view.load();

	m_config->remove(ConfigStore::Application, group, key);
	QCOMPARE(m_config->getBool(ConfigStore::Application, group, key, false), false);

	QVERIFY(view.save());

	QCOMPARE(m_config->getBool(ConfigStore::Application, group, key, false), true);

	m_config->remove(ConfigStore::Application, group, key);
	m_config->save(ConfigStore::Application);
}

/**
 * The connection page says in words what forcing active mode costs.
 *
 * On the page rather than in a tooltip, because this is the one setting here whose cost
 * is paid somewhere the user cannot see it: the peer that believed the claim is the one
 * whose transfer fails, so the symptom never appears beside the switch that caused it.
 * The page has to say that too, and that the claim defeats the measurement shown
 * directly above it.
 */
void TestSettings::theConnectionPageSaysWhatForcingActiveModeCosts()
{
	Qdc::Shares shares(m_config);
	Qdc::SettingsView view(m_config, &shares);

	QStringList said;

	for (const QLabel* label : view.findChildren<QLabel*>())
		said.append(label->text());

	const QString page = said.join(QLatin1Char('\n'));

	QVERIFY2(page.contains(QStringLiteral("every peer that acts on it by dialling this "
	                                      "client fails to reach it")),
	         "the page does not say that peers fail to connect when the claim is wrong");

	QVERIFY2(page.contains(QStringLiteral("a download that never starts rather than a "
	                                      "setting to turn off")),
	         "the page does not say what the failure looks like");

	QVERIFY2(page.contains(QStringLiteral("whatever the check above concluded")),
	         "the page does not say that the claim overrides the measurement");
}

void TestSettings::boardsFetchNothingAheadUntilTheyAreTold()
{
	using Qdc::ConfigStore;

	/* Nothing written for either, so the form is showing what is really in force. */
	m_config->remove(ConfigStore::Application, QStringLiteral("Boards"),
	                 QStringLiteral("Fetch a thread ahead"));
	m_config->remove(ConfigStore::Application, QStringLiteral("Boards"),
	                 QStringLiteral("Cache size"));
	m_config->save(ConfigStore::Application);

	Qdc::Shares shares(m_config);
	Qdc::SettingsView view(m_config, &shares);

	QVERIFY(view.save());

	/*
	 * Off, and not as a preference: a post is fetched from whoever holds it, and until
	 * somebody else has read it the only person who holds it is its author - so
	 * anything fetched without being asked for tells an author who read their post and
	 * when.
	 */
	QCOMPARE(m_config->getBool(ConfigStore::Application, QStringLiteral("Boards"),
	                           QStringLiteral("Fetch a thread ahead"), true), false);

	/* And the bound on what is held is in bytes, whatever the form shows. */
	QCOMPARE(m_config->getNumber(ConfigStore::Application, QStringLiteral("Boards"),
	                             QStringLiteral("Cache size")), 256 * 1024 * 1024);

	m_config->setNumber(ConfigStore::Application, QStringLiteral("Boards"),
	                    QStringLiteral("Cache size"), 128 * 1024 * 1024);

	view.load();
	QVERIFY(view.save());

	/*
	 * Back in bytes, unchanged. A page that showed bytes as mebibytes and wrote them
	 * back as mebibytes would divide the cache by a million every time it was saved.
	 */
	QCOMPARE(m_config->getNumber(ConfigStore::Application, QStringLiteral("Boards"),
	                             QStringLiteral("Cache size")), 128 * 1024 * 1024);
}

void TestSettings::theBoardsPageSaysThatReadingAPostMayBeSeen()
{
	Qdc::Shares shares(m_config);
	Qdc::SettingsView view(m_config, &shares);

	QStringList said;

	for (const QLabel* label : view.findChildren<QLabel*>())
		said.append(label->text());

	const QString page = said.join(QLatin1Char('\n'));

	/*
	 * In words on the page rather than in a tooltip, because it is the thing about a
	 * board that somebody would not think of and cannot find out afterwards: the
	 * request goes to whoever has the post, and to begin with that is only its author.
	 */
	QVERIFY2(page.contains(QStringLiteral("the only person who has it is whoever "
	                                      "wrote it")),
	         "the page does not say who a post is fetched from");

	QVERIFY2(page.contains(QStringLiteral("may tell its author that you have read it")),
	         "the page does not say that reading a post can be seen");

	/* And that answering for one says the same thing to anybody who asks. */
	QVERIFY(page.contains(QStringLiteral("Offering a post says the same thing to "
	                                     "anybody who asks for it")));

	/* And which hubs answer for nothing until they are told to. */
	QVERIFY(page.contains(QStringLiteral("A hub you have not bookmarked offers "
	                                     "nothing until you say so on that hub")));
}

/**
 * Saving the form without touching it leaves the local hub off.
 *
 * The key is removed first, so the form is showing what is really in force. The default
 * is then asked for as true, so that the answer cannot be right by accident: running a
 * hub means this computer answering strangers, which is a thing to be chosen rather than
 * arrived at by installing the program. The rest of the page is asserted to be what the
 * core would have used anyway, so that turning the hub on later does not also silently
 * change how it behaves.
 */
void TestSettings::theLocalHubIsOffUntilItIsAskedFor()
{
	using Qdc::ConfigStore;

	const QString group = QStringLiteral("Local hub");

	m_config->remove(ConfigStore::Application, group, QStringLiteral("Enabled"));
	m_config->save(ConfigStore::Application);

	Qdc::Shares shares(m_config);
	Qdc::SettingsView view(m_config, &shares);

	QVERIFY(view.save());

	QCOMPARE(m_config->getBool(ConfigStore::Application, group,
	                           QStringLiteral("Enabled"), true), false);

	QCOMPARE(m_config->getNumber(ConfigStore::Application, group,
	                             QStringLiteral("Max users")), 128);
	QCOMPARE(m_config->getString(ConfigStore::Application, group,
	                             QStringLiteral("Name")),
	         QStringLiteral("A QuickDC hub"));
	QCOMPARE(m_config->getBool(ConfigStore::Application, group,
	                           QStringLiteral("Check reachability claims"), false), true);
}

/**
 * The local hub page writes its figures back in the units the core reads them in.
 *
 * The core reads bytes whatever the form shows, and the default is 48 KiB of them. A page
 * that showed bytes as kibibytes and wrote them back as kibibytes would divide the queue
 * by a thousand every time it was saved.
 *
 * A figure edited into the file outside the core's own clamp has to come back as what the
 * hub will actually use rather than as what was typed, because a field that accepts a
 * number the program will not use is a field that lies about what it did.
 */
void TestSettings::theLocalHubKeepsItsFiguresInTheUnitsTheCoreReads()
{
	using Qdc::ConfigStore;

	const QString group = QStringLiteral("Local hub");
	const QString sendq = QStringLiteral("Max send queue bytes");

	m_config->remove(ConfigStore::Application, group, sendq);
	m_config->save(ConfigStore::Application);

	Qdc::Shares shares(m_config);
	Qdc::SettingsView view(m_config, &shares);

	QVERIFY(view.save());

	QCOMPARE(m_config->getNumber(ConfigStore::Application, group, sendq), 48 * 1024);

	m_config->setNumber(ConfigStore::Application, group, sendq, 96 * 1024);

	view.load();
	QVERIFY(view.save());

	QCOMPARE(m_config->getNumber(ConfigStore::Application, group, sendq), 96 * 1024);

	m_config->setNumber(ConfigStore::Application, group,
	                    QStringLiteral("Max users"), 999999);

	view.load();
	QVERIFY(view.save());

	QCOMPARE(m_config->getNumber(ConfigStore::Application, group,
	                             QStringLiteral("Max users")), 65536);
}

/**
 * The local hub page says in words what turning the hub on does.
 *
 * On the page rather than in a tooltip. This is the one switch in the settings that makes
 * the computer answer strangers, and none of what that means can be worked out from the
 * switch itself: who can join, that there is no password, that it shares the port the
 * Connection tab sets since there is no other, and that the switch acts at once - which a
 * settings page does not usually promise - including what turning it off does to whoever
 * is on it.
 */
void TestSettings::theLocalHubPageSaysWhatTurningItOnDoes()
{
	Qdc::Shares shares(m_config);
	Qdc::SettingsView view(m_config, &shares);

	QStringList said;

	for (const QLabel* label : view.findChildren<QLabel*>())
		said.append(label->text());

	const QString page = said.join(QLatin1Char('\n'));

	QVERIFY2(page.contains(QStringLiteral("Anyone who can reach that port can join")),
	         "the page does not say who can join a hub run here");

	QVERIFY2(page.contains(QStringLiteral("there is no password")),
	         "the page does not say that the hub has no password");

	QVERIFY(page.contains(QStringLiteral("the same port this client already listens on")));

	QVERIFY2(page.contains(QStringLiteral("applies it straight away")),
	         "the page does not say that saving takes effect without a restart");

	QVERIFY2(page.contains(QStringLiteral("disconnects whoever is on it")),
	         "the page does not say what turning the hub off does to its users");
}

QTEST_MAIN(TestSettings)

#include "tst_settings.moc"

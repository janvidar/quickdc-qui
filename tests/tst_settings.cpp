/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/configbridge.h>
#include <core/corethread.h>
#include <core/dropbridge.h>
#include <core/shares.h>
#include <gui/reachabilitypanel.h>
#include <gui/settingsview.h>

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QComboBox>
#include <QImage>
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
	void theGatewayIsNotAskedForAMappingUntilItIsAskedFor();
	void askingTheGatewayToForwardSurvivesTheForm();
	void theConnectionPageSaysWhatSearchingForAGatewayDiscloses();

	void eachConnectionModeRoundTripsThroughTheForm();
	void automaticDecidesTheSettingsUnderIt();
	void passiveTurnsOffWhatItCannotUse();
	void aModeThatHidesThePortDoesNotRewriteIt();

	void thePanelDrawsItsThreeAnswersApart();
	void aFamilyNobodyMeasuredIsNotDrawnAsAFailure();
	void aVerdictForOneFamilyLeavesTheOtherAlone();
	void aServersOwnWordsCannotBecomeMarkupInATooltip();
	void hidingThePortsLeavesTheAddress();
	void everyRunThatStartsAlsoEndsTheCheckingLine();
	void anUnconfiguredServerSaysSoRatherThanNothing();

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
 *
 * Pinned to advanced, which is the only mode where the switch means anything. The
 * other two derive it, and derive it off - so this asks whether the form carries a
 * value it was given, which is a question only advanced poses.
 */
void TestSettings::forcedActiveModeSurvivesTheForm()
{
	using Qdc::ConfigStore;

	const QString group = QStringLiteral("Network");
	const QString key = QStringLiteral("Force active mode");

	m_config->setString(ConfigStore::Application, group, QStringLiteral("Mode"),
	                    QStringLiteral("advanced"));
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

/**
 * Nothing is asked of a gateway until somebody asks for it.
 *
 * The default has to be off in the form as well as in the core: finding a gateway means
 * searching the local segment, which tells every host on it that this machine is here
 * and what it runs, and a form that saved the switch on would make that disclosure on
 * behalf of a user who never chose it.
 */
void TestSettings::theGatewayIsNotAskedForAMappingUntilItIsAskedFor()
{
	using Qdc::ConfigStore;

	const QString group = QStringLiteral("Network");
	const QString key = QStringLiteral("UseUPnP");

	m_config->remove(ConfigStore::Application, group, key);
	m_config->save(ConfigStore::Application);

	Qdc::Shares shares(m_config);
	Qdc::SettingsView view(m_config, &shares);

	QVERIFY(view.save());

	QCOMPARE(m_config->getBool(ConfigStore::Application, group, key, true), false);
}

/**
 * A gateway mapping asked for in the file is still asked for after the form saves.
 *
 * The key is taken back out of the store between the load() and the save(), for the
 * reason the forced-active test gives: left in place, this would pass against a page
 * that read the switch and never wrote it back, and such a page would turn forwarding
 * off every time anything else was saved - leaving a user passive with no sign of why.
 *
 * Advanced, for the reason the forced-active case above gives: it is the one mode
 * that answers with the switch rather than deciding for it.
 */
void TestSettings::askingTheGatewayToForwardSurvivesTheForm()
{
	using Qdc::ConfigStore;

	const QString group = QStringLiteral("Network");
	const QString key = QStringLiteral("UseUPnP");

	m_config->setString(ConfigStore::Application, group, QStringLiteral("Mode"),
	                    QStringLiteral("advanced"));
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
 * The connection page says what turning forwarding on tells the local network.
 *
 * On the page rather than in the tooltip, because the cost is not paid where the switch
 * is: a search is a broadcast to every host on the segment, and nothing on screen would
 * ever show that it happened. The page also has to say that a router granting a mapping
 * settles nothing on its own, so that a user reading a passive verdict with forwarding
 * on looks above the router rather than at the switch.
 */
void TestSettings::theConnectionPageSaysWhatSearchingForAGatewayDiscloses()
{
	Qdc::Shares shares(m_config);
	Qdc::SettingsView view(m_config, &shares);

	QStringList said;

	for (const QLabel* label : view.findChildren<QLabel*>())
		said.append(label->text());

	const QString page = said.join(QLatin1Char('\n'));

	QVERIFY2(page.contains(QStringLiteral("tells every host on it that this machine is "
	                                      "here and what it runs")),
	         "the page does not say what a gateway search discloses");

	QVERIFY2(page.contains(QStringLiteral("does not know whether anything above it "
	                                      "honours one")),
	         "the page does not say that a granted mapping settles nothing on its own");
}

/**
 * A verdict, as one would arrive from a DROP server.
 *
 * Built here rather than measured, which is the only way the drawing gets covered
 * at all: a real one takes a server outside willing to connect back.
 */
Qdc::ReachabilityVerdict measured(bool ipv6, Qdc::ReachabilityReach tcp,
                                  Qdc::ReachabilityReach udp)
{
	Qdc::ReachabilityVerdict verdict;

	verdict.ran = true;
	verdict.ipv6 = ipv6;
	verdict.publicAddress = ipv6 ? QStringLiteral("2001:db8::1")
	                             : QStringLiteral("198.51.100.7");
	verdict.classification = QStringLiteral("global");
	verdict.port = 1411;

	verdict.tcpReached = tcp;
	verdict.udpReached = udp;

	/* The server's own word for each outcome; empty means it did not test. */
	verdict.tcp = tcp == Qdc::ReachabilityReach::Reached ? QStringLiteral("open")
	                                                     : QStringLiteral("filtered");
	verdict.udp = udp == Qdc::ReachabilityReach::Reached ? QStringLiteral("open")
	                                                     : QStringLiteral("no_response");

	verdict.observedAt = QDateTime::fromSecsSinceEpoch(1700000000);

	return verdict;
}

/** What a cell is showing, as an image, so two states can be told apart. */
QImage cellShape(const Qdc::ReachabilityPanel& panel, const QString& name)
{
	const QLabel* icon = panel.findChild<QLabel*>(name + QStringLiteral("Icon"));
	if (!icon) return QImage();

	return icon->pixmap().toImage();
}

/**
 * Reached, blocked and unmeasured are three different pictures.
 *
 * The whole reason the panel draws a shape at all is that colour is not enough:
 * somebody who cannot tell the green from the red has to be able to read the answer
 * off the form of it. So it is not enough that the three states differ - they must
 * differ in the pixels, and a change that gave two of them the same glyph in
 * different inks would pass every other test here.
 */
void TestSettings::thePanelDrawsItsThreeAnswersApart()
{
	Qdc::ReachabilityPanel panel;

	const QImage unmeasured = cellShape(panel, QStringLiteral("reachabilityTcp4"));
	QVERIFY(!unmeasured.isNull());

	panel.setVerdict(measured(false, Qdc::ReachabilityReach::Reached,
	                          Qdc::ReachabilityReach::Blocked));

	const QImage reached = cellShape(panel, QStringLiteral("reachabilityTcp4"));
	const QImage blocked = cellShape(panel, QStringLiteral("reachabilityUdp4"));

	QVERIFY2(reached != blocked, "reached and blocked are drawn the same");
	QVERIFY2(reached != unmeasured, "reached and unmeasured are drawn the same");
	QVERIFY2(blocked != unmeasured, "blocked and unmeasured are drawn the same");
}

/**
 * A family nobody measured is drawn as nothing known, not as a port that failed.
 *
 * This is the distinction the whole panel turns on. A host with no IPv6 route
 * reports nothing for IPv6, and drawing that as a failure would send somebody off
 * to fix a network that is working perfectly - so the unmeasured family has to
 * carry the same picture it had before either family was measured, and not the one
 * the measured-and-blocked family carries.
 */
void TestSettings::aFamilyNobodyMeasuredIsNotDrawnAsAFailure()
{
	Qdc::ReachabilityPanel panel;

	const QImage nothing = cellShape(panel, QStringLiteral("reachabilityTcp6"));

	/* IPv4 measured and blocked; IPv6 not measured at all. */
	panel.setVerdict(measured(false, Qdc::ReachabilityReach::Blocked,
	                          Qdc::ReachabilityReach::Blocked));

	QCOMPARE(panel.addressText(true), QStringLiteral("not measured"));
	QCOMPARE(panel.portText(true, false), QStringLiteral("not measured"));
	QCOMPARE(panel.portText(true, true), QStringLiteral("not measured"));

	QCOMPARE(cellShape(panel, QStringLiteral("reachabilityTcp6")), nothing);

	QVERIFY2(cellShape(panel, QStringLiteral("reachabilityTcp4")) != nothing,
	         "a measured failure is drawn the same as never having asked");

	/*
	 * And a probe the server ran but could not classify is the same picture again.
	 * It carries a word, so the run did happen, but 'Unknown' is not a soft
	 * 'Blocked': the server having failed at its end says nothing whatever about
	 * this client, and withdrawing an active claim over somebody else's outage is
	 * the one mistake here that cannot be seen from the outside.
	 */
	Qdc::ReachabilityVerdict unclassified = measured(true,
		Qdc::ReachabilityReach::Unknown, Qdc::ReachabilityReach::Unknown);

	unclassified.tcp = QStringLiteral("error");
	unclassified.udp = QStringLiteral("error");

	panel.setVerdict(unclassified);

	QCOMPARE(panel.portText(true, false), QStringLiteral("error"));
	QCOMPARE(cellShape(panel, QStringLiteral("reachabilityTcp6")), nothing);
}

/**
 * The second family to arrive does not wipe the first.
 *
 * One run answers for one family and they finish one after the other, so a panel
 * that drew each as it came would spend most of its life describing half the
 * client while claiming to describe all of it.
 */
void TestSettings::aVerdictForOneFamilyLeavesTheOtherAlone()
{
	Qdc::ReachabilityPanel panel;

	panel.setVerdict(measured(false, Qdc::ReachabilityReach::Reached,
	                          Qdc::ReachabilityReach::Reached));
	panel.setVerdict(measured(true, Qdc::ReachabilityReach::Blocked,
	                          Qdc::ReachabilityReach::Blocked));

	QVERIFY(panel.addressText(false).contains(QStringLiteral("198.51.100.7")));
	QVERIFY(panel.addressText(true).contains(QStringLiteral("2001:db8::1")));

	QCOMPARE(panel.portText(false, false), QStringLiteral("open"));
	QCOMPARE(panel.portText(true, false), QStringLiteral("filtered"));
}

/**
 * A reason from the server reaches a tooltip as text, never as markup.
 *
 * Every word of it was written by a third party, and a tooltip cannot be put on
 * plain text the way the labels can: Qt hands the string to a label on
 * Qt::AutoText, which would read markup in it as markup. So a server that returned
 * a bold tag would be setting the type in this client's own chrome, and one that
 * returned an image tag would have it fetch a resource.
 */
void TestSettings::aServersOwnWordsCannotBecomeMarkupInATooltip()
{
	Qdc::ReachabilityPanel panel;

	Qdc::ReachabilityVerdict verdict = measured(false,
		Qdc::ReachabilityReach::Blocked, Qdc::ReachabilityReach::Blocked);

	verdict.reason = QStringLiteral("<b>bold</b> & <img src=x>");

	panel.setVerdict(verdict);

	const QLabel* cell =
		panel.findChild<QLabel*>(QStringLiteral("reachabilityTcp4"));
	QVERIFY(cell);

	const QString tip = cell->toolTip();

	QVERIFY2(tip.contains(QStringLiteral("&lt;b&gt;")), "the tag was not escaped");
	QVERIFY2(!tip.contains(QStringLiteral("<b>")), "the tag survived as markup");
	QVERIFY2(!tip.contains(QStringLiteral("<img")), "an image tag survived");
}

/**
 * With the ports hidden the address stays, and the port rows go entirely.
 *
 * For a client that is not trying to be reached: the address is still worth
 * showing, and an inbound answer is a question nobody asked. Hidden rather than
 * drawn as dashes, because a dash invites somebody to go and find out and there is
 * nothing to find out.
 */
void TestSettings::hidingThePortsLeavesTheAddress()
{
	Qdc::ReachabilityPanel panel;

	panel.setVerdict(measured(false, Qdc::ReachabilityReach::Reached,
	                          Qdc::ReachabilityReach::Reached));
	panel.setShowPorts(false);

	QVERIFY(panel.addressText(false).contains(QStringLiteral("198.51.100.7")));

	const QLabel* tcp =
		panel.findChild<QLabel*>(QStringLiteral("reachabilityTcp4"));
	QVERIFY(tcp);
	QVERIFY2(tcp->isHidden(), "a port row is still shown");

	panel.setShowPorts(true);
	QVERIFY2(!tcp->isHidden(), "the port rows did not come back");
}

/**
 * Whatever ends a run also takes the checking line down.
 *
 * The line is the one piece of this page that a user waits on, so a state it can
 * enter and not leave is a page that looks busy for the life of the window. Both
 * ways out are checked because they are different code paths: a verdict replaces
 * what is shown, and a failure leaves every row alone and only says nobody asked.
 */
void TestSettings::everyRunThatStartsAlsoEndsTheCheckingLine()
{
	const auto checkingIsShown = [](const Qdc::ReachabilityPanel& panel) {
		for (const QLabel* label : panel.findChildren<QLabel*>())
		{
			if (label->text().contains(QStringLiteral("Checking"))) return true;
		}
		return false;
	};

	{
		Qdc::ReachabilityPanel panel;

		panel.setChecking(true);
		QVERIFY(checkingIsShown(panel));

		panel.setVerdict(measured(false, Qdc::ReachabilityReach::Reached,
		                          Qdc::ReachabilityReach::Reached));

		QVERIFY2(!checkingIsShown(panel), "a verdict left the page saying it was checking");
	}

	{
		Qdc::ReachabilityPanel panel;

		panel.setChecking(true);
		QVERIFY(checkingIsShown(panel));

		panel.setFailure(QStringLiteral("the server could not be reached"));

		QVERIFY2(!checkingIsShown(panel), "a failure left the page saying it was checking");
	}
}

/**
 * With no server named, asking for a check says why rather than going quiet.
 *
 * The core's own call answers a bare false for this, which is indistinguishable
 * from a run already being in flight - so the bridge has to recognise the
 * unconfigured case itself and say so. Silence here is what would leave somebody
 * looking at a page that never answers, and no measurement is attempted, so this
 * costs no network: the refusal happens before anything is dialled.
 *
 * The other half - a run that does start, and says so - cannot be reached from
 * here. A core built with named preference files, which is every core a test has,
 * deliberately has no reachability check at all: it has no listening socket, so
 * there is nothing to measure. That half is pinned in the core's own suite, where
 * a check can be constructed directly; see dropcheck_a_run_says_that_it_started.
 */
void TestSettings::anUnconfiguredServerSaysSoRatherThanNothing()
{
	using Qdc::ConfigStore;

	m_config->setString(ConfigStore::Application, QStringLiteral("Network"),
	                    QStringLiteral("Reachability server"), QString());
	m_config->save(ConfigStore::Application);

	Qdc::DropBridge bridge(m_thread);

	QSignalSpy failed(&bridge, &Qdc::DropBridge::checkFailed);
	QSignalSpy started(&bridge, &Qdc::DropBridge::checkStarted);

	bridge.recheck();

	QVERIFY2(failed.wait(5000), "asking with no server named reported nothing at all");

	const QString reason = failed.takeFirst().at(0).toString();
	QVERIFY2(!reason.isEmpty(), "the refusal carried no sentence to show");

	QCOMPARE(started.count(), 0);
}

/** The stored mode, as the core would read it. */
QString storedMode(Qdc::ConfigBridge* config)
{
	return config->getString(Qdc::ConfigStore::Application,
	                         QStringLiteral("Network"), QStringLiteral("Mode"));
}

/** Put the form into 'mode' as though somebody had picked it from the list. */
void chooseMode(Qdc::SettingsView& view, const QString& token)
{
	QComboBox* combo = nullptr;

	for (QComboBox* candidate : view.findChildren<QComboBox*>())
	{
		if (candidate->findData(token) >= 0) { combo = candidate; break; }
	}

	QVERIFY(combo);
	combo->setCurrentIndex(combo->findData(token));
}

/**
 * Every mode survives being chosen, saved and read back.
 *
 * The one setting on the page that everything else is derived from, so a mode that
 * did not survive the round trip would take the whole group with it.
 */
void TestSettings::eachConnectionModeRoundTripsThroughTheForm()
{
	for (const QString& token : {QStringLiteral("automatic"),
	                             QStringLiteral("advanced"),
	                             QStringLiteral("passive")})
	{
		Qdc::Shares shares(m_config);
		Qdc::SettingsView view(m_config, &shares);

		chooseMode(view, token);
		QVERIFY(view.save());

		QCOMPARE(storedMode(m_config), token);

		/* And read back into the form it came from, not merely into the file. */
		Qdc::SettingsView reopened(m_config, &shares);
		QVERIFY(reopened.save());
		QCOMPARE(storedMode(m_config), token);
	}
}

/**
 * Automatic settles the plumbing itself, whatever the switches under it held.
 *
 * The whole of the mode: a user who picks it is saying they do not want to answer
 * these questions, so the answers must not depend on what the hidden switches
 * happened to be left at. Forwarding on, the claim never forced - because automatic
 * establishes reachability rather than asserting it - and a server to ask, since
 * without one there is no verdict to show.
 */
void TestSettings::automaticDecidesTheSettingsUnderIt()
{
	using Qdc::ConfigStore;

	const QString group = QStringLiteral("Network");

	/* The opposite of every value automatic implies. */
	m_config->setBool(ConfigStore::Application, group, QStringLiteral("UseUPnP"), false);
	m_config->setBool(ConfigStore::Application, group,
	                  QStringLiteral("Force active mode"), true);
	m_config->setString(ConfigStore::Application, group,
	                    QStringLiteral("Reachability server"), QString());
	m_config->save(ConfigStore::Application);

	Qdc::Shares shares(m_config);
	Qdc::SettingsView view(m_config, &shares);

	chooseMode(view, QStringLiteral("automatic"));
	QVERIFY(view.save());

	QVERIFY2(m_config->getBool(ConfigStore::Application, group,
	                           QStringLiteral("UseUPnP"), false),
	         "automatic did not ask the gateway to forward the port");

	QVERIFY2(m_config->getBool(ConfigStore::Application, group,
	                           QStringLiteral("Active mode"), false),
	         "automatic did not intend to be reachable");

	QVERIFY2(!m_config->getBool(ConfigStore::Application, group,
	                            QStringLiteral("Force active mode"), true),
	         "automatic forced a claim it is supposed to establish");

	QVERIFY2(!m_config->getString(ConfigStore::Application, group,
	                              QStringLiteral("Reachability server")).isEmpty(),
	         "automatic left nobody to ask");
}

/**
 * Passive turns off the two things it has no use for.
 *
 * Nothing is told to connect here, so a forwarded port is a hole in somebody's
 * network that nothing is using, and a forced claim would have peers dial a port
 * this client is not offering - which fails at their end, where nobody can see why.
 */
void TestSettings::passiveTurnsOffWhatItCannotUse()
{
	using Qdc::ConfigStore;

	const QString group = QStringLiteral("Network");

	m_config->setBool(ConfigStore::Application, group, QStringLiteral("UseUPnP"), true);
	m_config->setBool(ConfigStore::Application, group,
	                  QStringLiteral("Force active mode"), true);
	m_config->save(ConfigStore::Application);

	Qdc::Shares shares(m_config);
	Qdc::SettingsView view(m_config, &shares);

	chooseMode(view, QStringLiteral("passive"));
	QVERIFY(view.save());

	QVERIFY(!m_config->getBool(ConfigStore::Application, group,
	                           QStringLiteral("Active mode"), true));
	QVERIFY(!m_config->getBool(ConfigStore::Application, group,
	                           QStringLiteral("UseUPnP"), true));
	QVERIFY(!m_config->getBool(ConfigStore::Application, group,
	                           QStringLiteral("Force active mode"), true));
}

/**
 * A mode that does not show the port leaves the stored one alone.
 *
 * The port is not only bound from this key - it is also the number handed to every
 * peer on every hub. So a save that wrote it from a field nobody could see would
 * have the client advertise whatever the form last loaded, and a peer that dials a
 * port nothing is listening on fails at their end rather than at ours. Leaving it
 * also means the answer to "what did I set the port to" survives a trip through
 * automatic and back.
 */
void TestSettings::aModeThatHidesThePortDoesNotRewriteIt()
{
	using Qdc::ConfigStore;

	const QString group = QStringLiteral("Network");

	m_config->setNumber(ConfigStore::Application, group, QStringLiteral("Port"), 1409);
	m_config->setString(ConfigStore::Application, group, QStringLiteral("Bind"),
	                    QStringLiteral("10.0.0.5"));
	m_config->save(ConfigStore::Application);

	{
		Qdc::Shares shares(m_config);
		Qdc::SettingsView view(m_config, &shares);

		chooseMode(view, QStringLiteral("automatic"));
		QVERIFY(view.save());
	}

	QCOMPARE(m_config->getNumber(ConfigStore::Application, group,
	                             QStringLiteral("Port")), 1409);
	QCOMPARE(m_config->getString(ConfigStore::Application, group,
	                             QStringLiteral("Bind")),
	         QStringLiteral("10.0.0.5"));

	/* And back in advanced it is the form's again. */
	{
		Qdc::Shares shares(m_config);
		Qdc::SettingsView view(m_config, &shares);

		chooseMode(view, QStringLiteral("advanced"));
		QVERIFY(view.save());
	}

	QCOMPARE(m_config->getNumber(ConfigStore::Application, group,
	                             QStringLiteral("Port")), 1409);
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

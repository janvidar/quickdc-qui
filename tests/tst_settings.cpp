/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/configbridge.h>
#include <core/corethread.h>
#include <core/dropbridge.h>
#include <core/shares.h>
#include <core/testing.h>
#include <gui/reachabilitypanel.h>
#include <gui/settingsview.h>

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QCheckBox>
#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QScrollArea>
#include <QToolButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <QTest>

/**
 * Settings: the shared folder list, and the form's binding to the configuration.
 *
 * The form is driven through its fields, found by name, and
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
	void anUnsetFieldShowsWhatTheCoreUses();
	void openingThePageWritesNothing();
	void formDoesNotWaitForABusyCore();
	void aTextFieldAppliesWhenItIsLeft();
	void numbersAndSwitchesApplyAtOnce();
	void figuresAreWrittenInTheUnitsTheCoreReads();
	void aSettingChangedElsewhereShowsOnThePage();
	void aFigureTheCoreWouldClampShowsAsClamped();
	void formAddsAFolderWithQtsOwnChooser();

	void eachConnectionModeAppliesWhenChosen();
	void automaticDecidesTheSettingsUnderIt();
	void passiveTurnsOffWhatItCannotUse();
	void aModeThatHidesThePortDoesNotRewriteIt();
	void aModeShowsOnlyThePlumbingItIsAbout();
	void theSwitchesUnderAdvancedApplyWhenTicked();
	void eachModeSaysInAWordWhatItDoesInstead();
	void thePageSaysWhichPortIsInUse();
	void noModeOffersAFieldForTheReachabilityServer();
	void aServerNamedByHandSurvivesChoosingAMode();
	void theCostlySwitchesSayWhatTheyCost();

	void thePanelDrawsItsThreeAnswersApart();
	void aFamilyNobodyMeasuredIsNotDrawnAsAFailure();
	void aVerdictForOneFamilyLeavesTheOtherAlone();
	void aServersOwnWordsCannotBecomeMarkupInATooltip();
	void hidingThePortsLeavesTheAddress();
	void everyRunThatStartsAlsoEndsTheCheckingLine();
	void anUnconfiguredServerSaysSoRatherThanNothing();

	void thePagesThatDiscloseSaySoInOneLine();
	void updatesAreTurnedOffWhereTheCoreReadsIt();

private:
	QString scratch(const QString& name) const;

	/** Let the core carry out what was asked of it, and its answers arrive here. */
	void settle();

	void clearShares(Qdc::Shares& shares);

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

void TestSettings::settle()
{
	QVERIFY(Qdc::Testing::runOnCore(m_thread, [] {}));
	QCoreApplication::sendPostedEvents();
	QCoreApplication::processEvents();
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

/** Take every folder out of the share, and wait for the core to say so. */
void TestSettings::clearShares(Qdc::Shares& shares)
{
	while (shares.count() > 0)
	{
		const int before = shares.count();
		shares.removeAt(0);
		QTRY_COMPARE(shares.count(), before - 1);
	}
}

void TestSettings::sharesRoundTripThroughTheStore()
{
	const QString music = scratch(QStringLiteral("Music"));
	const QString video = scratch(QStringLiteral("Video"));
	QVERIFY(QDir().mkpath(music));
	QVERIFY(QDir().mkpath(video));

	{
		Qdc::Shares shares(m_thread);
		QVERIFY(shares.folders().isEmpty());

		QSignalSpy changes(&shares, &Qdc::Shares::changed);

		shares.add(music);
		shares.add(video, QStringLiteral("Films"));
		QTRY_COMPARE(changes.count(), 2);
		QCOMPARE(shares.count(), 2);
	}

	/* A second one reads back what the first asked for, in order. */
	Qdc::Shares reloaded(m_thread);
	QCOMPARE(reloaded.count(), 2);
	QCOMPARE(reloaded.folders().at(0).path, music);
	QCOMPARE(reloaded.folders().at(0).name, QStringLiteral("Music"));
	QCOMPARE(reloaded.folders().at(1).path, video);
	QCOMPARE(reloaded.folders().at(1).name, QStringLiteral("Films"));

	/*
	 * Stored as it always was: the virtual name, a colon, the real path - so a
	 * list from before the core kept it reads the same.
	 */
	QTRY_COMPARE(m_config->getString(Qdc::ConfigStore::Application,
		QStringLiteral("Shares"), QStringLiteral("Share 1")), QStringLiteral("Films:") + video);

	QCOMPARE(m_config->getNumber(Qdc::ConfigStore::Application,
		QStringLiteral("Shares"), QStringLiteral("Total shares")), 2);

	reloaded.renameAt(1, QStringLiteral("Movies"));
	QTRY_COMPARE(reloaded.folders().at(1).name, QStringLiteral("Movies"));

	reloaded.removeAt(0);
	QTRY_COMPARE(reloaded.count(), 1);
	QCOMPARE(reloaded.folders().at(0).path, video);

	clearShares(reloaded);
}

void TestSettings::sharesRejectDuplicatesAndDescendants()
{
	const QString parent = scratch(QStringLiteral("Library"));
	const QString child = scratch(QStringLiteral("Library/Records"));
	QVERIFY(QDir().mkpath(child));

	Qdc::Shares shares(m_thread);
	clearShares(shares);

	QSignalSpy refused(&shares, &Qdc::Shares::refused);

	shares.add(parent);
	QTRY_COMPARE(shares.count(), 1);

	/* The same folder twice, a trailing separator naming the same folder, and one
	   inside it - which would put every file in it into the file list twice. */
	shares.add(parent);
	shares.add(parent + QLatin1Char('/'));
	shares.add(child);
	QTRY_COMPARE(refused.count(), 3);
	for (const QList<QVariant>& one : refused)
		QCOMPARE(one.at(1).value<Qdc::ShareRefusal>(), Qdc::ShareRefusal::AlreadyShared);
	QCOMPARE(shares.count(), 1);

	/* The other way round is allowed: the parent was not shared before. */
	clearShares(shares);
	shares.add(child);
	shares.add(parent);
	QTRY_COMPARE(shares.count(), 2);

	refused.clear();
	shares.add(QString());
	QTRY_COMPARE(refused.count(), 1);
	QCOMPARE(refused.at(0).at(1).value<Qdc::ShareRefusal>(), Qdc::ShareRefusal::NotAFolder);
	QCOMPARE(shares.count(), 2);

	clearShares(shares);
}

void TestSettings::sharesNameDefaultsToTheFolderName()
{
	const QString folder = scratch(QStringLiteral("Photographs"));
	QVERIFY(QDir().mkpath(folder));

	Qdc::Shares shares(m_thread);
	clearShares(shares);

	shares.add(folder);
	QTRY_COMPARE(shares.count(), 1);
	QCOMPARE(shares.folders().first().name, QStringLiteral("Photographs"));

	clearShares(shares);
}

void TestSettings::sharesStripTheSeparatorFromNames()
{
	const QString folder = scratch(QStringLiteral("Odd"));
	QVERIFY(QDir().mkpath(folder));

	Qdc::Shares shares(m_thread);
	clearShares(shares);

	/*
	 * A colon in the virtual name would be read back as the end of the name, so it
	 * is removed rather than stored and mis-parsed.
	 */
	shares.add(folder, QStringLiteral("a:b:c"));
	QTRY_COMPARE(shares.count(), 1);
	QCOMPARE(shares.folders().first().name, QStringLiteral("abc"));

	Qdc::Shares reloaded(m_thread);
	QCOMPARE(reloaded.folders().first().name, QStringLiteral("abc"));
	QCOMPARE(reloaded.folders().first().path, folder);

	clearShares(reloaded);
}

void TestSettings::sharesShrinkWithoutLeavingATail()
{
	Qdc::Shares shares(m_thread);
	clearShares(shares);

	for (int n = 0; n < 5; ++n)
	{
		const QString path = scratch(QStringLiteral("bulk%1").arg(n));
		QVERIFY(QDir().mkpath(path));
		shares.add(path, QStringLiteral("share%1").arg(n));
	}
	QTRY_COMPARE(shares.count(), 5);

	for (int left = 4; left >= 2; --left)
	{
		shares.removeAt(2);
		QTRY_COMPARE(shares.count(), left);
	}

	/*
	 * The group is numbered, so a shorter list has to clear the keys the longer one
	 * used - otherwise reloading finds the old tail still there.
	 */
	QTRY_VERIFY(!m_config->contains(Qdc::ConfigStore::Application, QStringLiteral("Shares"),
	                                QStringLiteral("Share 2")));
	QCOMPARE(m_config->getNumber(Qdc::ConfigStore::Application, QStringLiteral("Shares"),
	                             QStringLiteral("Total shares")), 2);

	clearShares(shares);
}

void TestSettings::formLoadsTheStoredValues()
{
	using Qdc::ConfigStore;

	m_config->setString(ConfigStore::Application, QStringLiteral("User"),
	                    QStringLiteral("Nickname"), QStringLiteral("tester"));
	m_config->setNumber(ConfigStore::Application, QStringLiteral("Network"),
	                    QStringLiteral("Slots"), 9);

	Qdc::Shares shares(m_thread);
	settle();
	Qdc::SettingsView view(m_config, &shares);

	QCOMPARE(view.findChild<QLineEdit*>(QStringLiteral("identityNickname"))->text(), QStringLiteral("tester"));
	QCOMPARE(view.findChild<QSpinBox*>(QStringLiteral("transfersSlots"))->value(), 9);
}

/* The page kept defaults of its own once, and they drifted from the core's. */
void TestSettings::anUnsetFieldShowsWhatTheCoreUses()
{
	using Qdc::ConfigStore;

	m_config->remove(ConfigStore::Application, QStringLiteral("User"), QStringLiteral("Nickname"));
	m_config->remove(ConfigStore::Application, QStringLiteral("User"), QStringLiteral("Description"));
	m_config->remove(ConfigStore::Application, QStringLiteral("Local hub"), QStringLiteral("Max users"));
	m_config->remove(ConfigStore::Application, QStringLiteral("Local hub"), QStringLiteral("Enabled"));

	Qdc::Shares shares(m_thread);
	settle();
	Qdc::SettingsView view(m_config, &shares);

	QCOMPARE(view.findChild<QLineEdit*>(QStringLiteral("identityNickname"))->text(), QStringLiteral("QuickDC"));
	QCOMPARE(view.findChild<QLineEdit*>(QStringLiteral("identityDescription"))->text(), QStringLiteral("QuickDC user"));
	QCOMPARE(view.findChild<QSpinBox*>(QStringLiteral("localHubUsers"))->value(), 128);
	QVERIFY(!view.findChild<QCheckBox*>(QStringLiteral("localHubEnabled"))->isChecked());
}

/*
 * Nothing is written by looking: a forced claim, a gateway search, a hub answering
 * strangers and posts fetched ahead are each a disclosure somebody has to choose.
 */
void TestSettings::openingThePageWritesNothing()
{
	using Qdc::ConfigStore;

	const QList<QPair<QString, QString>> keys = {
		{QStringLiteral("Network"), QStringLiteral("Force active mode")},
		{QStringLiteral("Network"), QStringLiteral("UseUPnP")},
		{QStringLiteral("Local hub"), QStringLiteral("Enabled")},
		{QStringLiteral("Boards"), QStringLiteral("Fetch a thread ahead")},
		{QStringLiteral("User"), QStringLiteral("Nickname")},
	};
	for (const auto& [group, key] : keys) m_config->remove(ConfigStore::Application, group, key);

	Qdc::Shares shares(m_thread);
	settle();
	{
		Qdc::SettingsView view(m_config, &shares);
		view.load();
		settle();
	}
	settle();

	for (const auto& [group, key] : keys)
		QVERIFY2(!m_config->contains(ConfigStore::Application, group, key),
		         qPrintable(group + QLatin1Char('/') + key + QStringLiteral(" was written by opening the page")));
}

void TestSettings::formDoesNotWaitForABusyCore()
{
	Qdc::ConfigBridge config(m_thread);
	Qdc::Shares shares(m_thread);

	/* The page reads the bridge's copy; a blocking read per key once waited out a core turn each. */
	Qdc::Testing::post(m_thread, [] { QThread::msleep(1500); });

	QElapsedTimer timer;
	timer.start();

	Qdc::SettingsView view(&config, &shares);
	view.load();
	view.findChild<QSpinBox*>(QStringLiteral("transfersSlots"))->setValue(4);

	const qint64 spent = timer.elapsed();
	QVERIFY2(spent < 100, qPrintable(QStringLiteral("the page took %1 ms while the core was busy").arg(spent)));
}

void TestSettings::aTextFieldAppliesWhenItIsLeft()
{
	using Qdc::ConfigStore;

	Qdc::Shares shares(m_thread);
	settle();
	Qdc::SettingsView view(m_config, &shares);

	QLineEdit* nick = view.findChild<QLineEdit*>(QStringLiteral("identityNickname"));
	nick->setText(QStringLiteral("  typed  "));
	settle();
	QVERIFY(m_config->getString(ConfigStore::Application, QStringLiteral("User"), QStringLiteral("Nickname")) != QStringLiteral("typed"));

	emit nick->editingFinished();
	settle();
	QCOMPARE(m_config->getString(ConfigStore::Application, QStringLiteral("User"), QStringLiteral("Nickname")),
	         QStringLiteral("typed"));
}

void TestSettings::numbersAndSwitchesApplyAtOnce()
{
	using Qdc::ConfigStore;

	Qdc::Shares shares(m_thread);
	settle();
	Qdc::SettingsView view(m_config, &shares);

	view.findChild<QSpinBox*>(QStringLiteral("transfersDownloads"))->setValue(7);
	QCheckBox* fetch = view.findChild<QCheckBox*>(QStringLiteral("chatFetch"));
	fetch->setChecked(!fetch->isChecked());
	const bool fetching = fetch->isChecked();
	settle();

	QCOMPARE(m_config->getNumber(ConfigStore::Application, QStringLiteral("Network"), QStringLiteral("Max downloads")), 7);
	QCOMPARE(m_config->getBool(ConfigStore::Application, QStringLiteral("Chat media"), QStringLiteral("Fetch automatically"), !fetching),
	         fetching);
}

/* On by default, as the core has it, and off where the updater looks. */
void TestSettings::updatesAreTurnedOffWhereTheCoreReadsIt()
{
	using Qdc::ConfigStore;

	Qdc::Shares shares(m_thread);
	Qdc::SettingsView view(m_config, &shares);
	settle();

	QCheckBox* enabled = view.findChild<QCheckBox*>(QStringLiteral("updatesEnabled"));
	QVERIFY(enabled);
	QVERIFY(enabled->isChecked());

	enabled->setChecked(false);
	settle();
	QCOMPARE(m_config->getBool(ConfigStore::Application, QStringLiteral("Updates"), QStringLiteral("Enabled"), true), false);
}

/* The core reads bytes; a page that wrote back what it showed would shrink these on every edit. */
void TestSettings::figuresAreWrittenInTheUnitsTheCoreReads()
{
	using Qdc::ConfigStore;

	m_config->setNumber(ConfigStore::Application, QStringLiteral("Network"), QStringLiteral("Chunk size"), 2 * 1024 * 1024);
	m_config->setNumber(ConfigStore::Application, QStringLiteral("Boards"), QStringLiteral("Cache size"), 128 * 1024 * 1024);

	Qdc::Shares shares(m_thread);
	settle();
	Qdc::SettingsView view(m_config, &shares);

	QSpinBox* chunk = view.findChild<QSpinBox*>(QStringLiteral("transfersChunk"));
	QCOMPARE(chunk->value(), 2048);
	chunk->setValue(4096);

	QSpinBox* cache = view.findChild<QSpinBox*>(QStringLiteral("boardsCache"));
	QCOMPARE(cache->value(), 128);
	cache->setValue(64);

	QSpinBox* queue = view.findChild<QSpinBox*>(QStringLiteral("localHubSendQueue"));
	queue->setValue(96);
	settle();

	QCOMPARE(m_config->getNumber(ConfigStore::Application, QStringLiteral("Network"), QStringLiteral("Chunk size")), 4 * 1024 * 1024);
	QCOMPARE(m_config->getNumber(ConfigStore::Application, QStringLiteral("Boards"), QStringLiteral("Cache size")), 64 * 1024 * 1024);
	QCOMPARE(m_config->getNumber(ConfigStore::Application, QStringLiteral("Local hub"), QStringLiteral("Max send queue bytes")), 96 * 1024);
}

void TestSettings::aSettingChangedElsewhereShowsOnThePage()
{
	using Qdc::ConfigStore;

	Qdc::Shares shares(m_thread);
	settle();
	Qdc::SettingsView view(m_config, &shares);

	m_config->setNumber(ConfigStore::Application, QStringLiteral("Network"), QStringLiteral("Slots"), 5);
	QTRY_COMPARE(view.findChild<QSpinBox*>(QStringLiteral("transfersSlots"))->value(), 5);

	m_config->setNumber(ConfigStore::Application, QStringLiteral("Network"), QStringLiteral("Slots"), 6);
	QTRY_COMPARE(view.findChild<QSpinBox*>(QStringLiteral("transfersSlots"))->value(), 6);
}

/* Shown as what the hub will use, and not written back until somebody edits it. */
void TestSettings::aFigureTheCoreWouldClampShowsAsClamped()
{
	using Qdc::ConfigStore;

	m_config->setNumber(ConfigStore::Application, QStringLiteral("Local hub"), QStringLiteral("Max users"), 999999);

	Qdc::Shares shares(m_thread);
	settle();
	Qdc::SettingsView view(m_config, &shares);
	settle();

	QCOMPARE(view.findChild<QSpinBox*>(QStringLiteral("localHubUsers"))->value(), 65536);
	QCOMPARE(m_config->getNumber(ConfigStore::Application, QStringLiteral("Local hub"), QStringLiteral("Max users")), 999999);

	m_config->remove(ConfigStore::Application, QStringLiteral("Local hub"), QStringLiteral("Max users"));
}

void TestSettings::formAddsAFolderWithQtsOwnChooser()
{
	const QString folder = scratch(QStringLiteral("Clicked"));
	QVERIFY(QDir().mkpath(folder));

	Qdc::Shares shares(m_thread);
	clearShares(shares);

	settle();
	Qdc::SettingsView view(m_config, &shares);

	QPushButton* add = nullptr;
	for (QPushButton* button : view.findChildren<QPushButton*>())
	{
		if (button->text().startsWith(QStringLiteral("Add folder"))) add = button;
	}

	QVERIFY2(add, "the form has no Add folder button");

	QListWidget* list = view.findChild<QListWidget*>(QStringLiteral("sharingFolders"));
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

	clearShares(shares);
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

/** Pick 'token' from the list as somebody would: activated, not merely set. */
void chooseMode(Qdc::SettingsView& view, const QString& token)
{
	const Qdc::NetworkMode mode = token == QLatin1String("automatic") ? Qdc::NetworkMode::Automatic
		: token == QLatin1String("passive") ? Qdc::NetworkMode::Passive
		: Qdc::NetworkMode::Advanced;

	QComboBox* combo = view.findChild<QComboBox*>(QStringLiteral("connectionMode"));
	QVERIFY(combo);
	const int index = combo->findData(static_cast<int>(mode));
	combo->setCurrentIndex(index);
	emit combo->activated(index);
}

/** Bring a page to the front, so that hidden means hidden by the page and not by the stack. */
void openPage(Qdc::SettingsView& view, const QString& name)
{
	QListWidget* pages = view.findChild<QListWidget*>(QStringLiteral("settingsPages"));
	QVERIFY(pages);
	for (int i = 0; i < pages->count(); i++)
		if (pages->item(i)->text() == name) pages->setCurrentRow(i);
	QCOMPARE(pages->currentItem()->text(), name);
}

/* A view built on the stack is never shown, so ask whether anything between widget and view is hidden. */
bool shown(Qdc::SettingsView& view, const QWidget* widget)
{
	return widget && widget->isVisibleTo(&view);
}

QString visibleText(Qdc::SettingsView& view)
{
	QStringList said;
	for (const QLabel* label : view.findChildren<QLabel*>())
		if (label->isVisibleTo(&view)) said.append(label->text());
	return said.join(QLatin1Char('\n'));
}

QToolButton* moreSettings(Qdc::SettingsView& view, const QWidget* inside)
{
	for (QToolButton* button : view.findChildren<QToolButton*>())
		if (button->text() == QStringLiteral("More settings") && inside->isAncestorOf(button)) return button;
	return nullptr;
}

void TestSettings::eachConnectionModeAppliesWhenChosen()
{
	for (const QString& token : {QStringLiteral("automatic"), QStringLiteral("advanced"), QStringLiteral("passive")})
	{
		Qdc::Shares shares(m_thread);
		settle();
		{
			Qdc::SettingsView view(m_config, &shares);
			chooseMode(view, token);
			QTRY_COMPARE(storedMode(m_config), token);
		}

		settle();
		Qdc::SettingsView reopened(m_config, &shares);
		QComboBox* combo = reopened.findChild<QComboBox*>(QStringLiteral("connectionMode"));
		QTRY_COMPARE(combo->currentText().toLower(), token);
	}
}

/* Picking automatic says "don't ask me", so nothing may depend on what the hidden switches held. */
void TestSettings::automaticDecidesTheSettingsUnderIt()
{
	using Qdc::ConfigStore;
	const QString group = QStringLiteral("Network");

	m_config->setString(ConfigStore::Application, group, QStringLiteral("Mode"), QStringLiteral("advanced"));
	m_config->setBool(ConfigStore::Application, group, QStringLiteral("UseUPnP"), false);
	m_config->setBool(ConfigStore::Application, group, QStringLiteral("Force active mode"), true);
	m_config->setString(ConfigStore::Application, group, QStringLiteral("Reachability server"), QString());

	Qdc::Shares shares(m_thread);
	settle();
	Qdc::SettingsView view(m_config, &shares);

	chooseMode(view, QStringLiteral("automatic"));
	QTRY_VERIFY2(m_config->getBool(ConfigStore::Application, group, QStringLiteral("UseUPnP"), false),
	             "automatic did not ask the gateway to forward the port");
	QTRY_VERIFY(m_config->getBool(ConfigStore::Application, group, QStringLiteral("Active mode"), false));
	QTRY_VERIFY2(!m_config->getBool(ConfigStore::Application, group, QStringLiteral("Force active mode"), true),
	             "automatic forced a claim it is supposed to establish");
	QTRY_VERIFY2(!m_config->getString(ConfigStore::Application, group, QStringLiteral("Reachability server")).isEmpty(),
	             "automatic left nobody to ask");
}

void TestSettings::passiveTurnsOffWhatItCannotUse()
{
	using Qdc::ConfigStore;
	const QString group = QStringLiteral("Network");

	m_config->setString(ConfigStore::Application, group, QStringLiteral("Mode"), QStringLiteral("advanced"));
	m_config->setBool(ConfigStore::Application, group, QStringLiteral("UseUPnP"), true);
	m_config->setBool(ConfigStore::Application, group, QStringLiteral("Force active mode"), true);

	Qdc::Shares shares(m_thread);
	settle();
	Qdc::SettingsView view(m_config, &shares);

	/* Each waited for: the core writes the keys one at a time. */
	chooseMode(view, QStringLiteral("passive"));
	QTRY_VERIFY(!m_config->getBool(ConfigStore::Application, group, QStringLiteral("Active mode"), true));
	QTRY_VERIFY(!m_config->getBool(ConfigStore::Application, group, QStringLiteral("UseUPnP"), true));
	QTRY_VERIFY(!m_config->getBool(ConfigStore::Application, group, QStringLiteral("Force active mode"), true));
}

/* The port is handed to every peer; a mode that does not show it must not move it. */
void TestSettings::aModeThatHidesThePortDoesNotRewriteIt()
{
	using Qdc::ConfigStore;
	const QString group = QStringLiteral("Network");

	m_config->setString(ConfigStore::Application, group, QStringLiteral("Mode"), QStringLiteral("advanced"));
	m_config->setNumber(ConfigStore::Application, group, QStringLiteral("Port"), 1409);
	m_config->setString(ConfigStore::Application, group, QStringLiteral("Bind"), QStringLiteral("10.0.0.5"));

	Qdc::Shares shares(m_thread);
	settle();
	{
		Qdc::SettingsView view(m_config, &shares);
		chooseMode(view, QStringLiteral("automatic"));
		QTRY_COMPARE(storedMode(m_config), QStringLiteral("automatic"));
		chooseMode(view, QStringLiteral("advanced"));
		QTRY_COMPARE(storedMode(m_config), QStringLiteral("advanced"));
	}

	QCOMPARE(m_config->getNumber(ConfigStore::Application, group, QStringLiteral("Port")), 1409);
	QCOMPARE(m_config->getString(ConfigStore::Application, group, QStringLiteral("Bind")), QStringLiteral("10.0.0.5"));
	m_config->setString(ConfigStore::Application, group, QStringLiteral("Bind"), QStringLiteral("127.0.0.1"));
}

void TestSettings::aModeShowsOnlyThePlumbingItIsAbout()
{
	using Qdc::ConfigStore;

	m_config->setString(ConfigStore::Application, QStringLiteral("Network"), QStringLiteral("Mode"), QStringLiteral("automatic"));

	Qdc::Shares shares(m_thread);
	settle();
	Qdc::SettingsView view(m_config, &shares);
	openPage(view, QStringLiteral("Connection"));

	const QSpinBox* port = view.findChild<QSpinBox*>(QStringLiteral("connectionPort"));
	const QLabel* inUse = view.findChild<QLabel*>(QStringLiteral("connectionPortInUse"));
	const QCheckBox* upnp = view.findChild<QCheckBox*>(QStringLiteral("connectionUpnp"));
	const QCheckBox* forced = view.findChild<QCheckBox*>(QStringLiteral("connectionForceActive"));
	QVERIFY(port && inUse && upnp && forced);

	QVERIFY2(!shown(view, port), "automatic showed the port it chooses itself");
	QVERIFY2(shown(view, inUse), "automatic did not say which port it is using");
	QVERIFY2(!shown(view, upnp), "automatic showed the forwarding switch it decides");
	QVERIFY2(!shown(view, forced), "automatic showed a claim it never forces");
	QVERIFY2(!visibleText(view).contains(QStringLiteral("Bind to")), "a hidden field left its label standing");

	chooseMode(view, QStringLiteral("advanced"));
	QVERIFY2(shown(view, port), "advanced did not show the port");
	QVERIFY2(!shown(view, inUse), "advanced said the port twice");
	QVERIFY2(shown(view, upnp), "advanced did not show the forwarding switch");
	QVERIFY2(!shown(view, forced), "the forced claim was not under More settings");

	const QWidget* connectionPage = port;
	while (connectionPage && !qobject_cast<const QScrollArea*>(connectionPage)) connectionPage = connectionPage->parentWidget();
	QToolButton* more = moreSettings(view, connectionPage);
	QVERIFY(more && shown(view, more));
	more->setChecked(true);
	QVERIFY2(shown(view, forced), "More settings did not show the forced claim");

	chooseMode(view, QStringLiteral("passive"));
	QVERIFY2(!shown(view, port), "passive showed a port nothing connects to");
	QVERIFY2(!shown(view, upnp), "passive showed a switch that forwards nothing");
	QVERIFY2(!shown(view, forced), "passive showed a claim it cannot honour");

	const QLabel* tcp = view.findChild<QLabel*>(QStringLiteral("reachabilityTcp4"));
	const QLabel* address = view.findChild<QLabel*>(QStringLiteral("reachabilityAddress4"));
	QVERIFY(tcp && address);
	QVERIFY2(!shown(view, tcp), "passive asked whether an unoffered port was reached");
	QVERIFY2(shown(view, address), "passive hid the address as well as the ports");

	m_config->remove(ConfigStore::Application, QStringLiteral("Network"), QStringLiteral("Mode"));
}

void TestSettings::theSwitchesUnderAdvancedApplyWhenTicked()
{
	using Qdc::ConfigStore;
	const QString group = QStringLiteral("Network");

	m_config->setString(ConfigStore::Application, group, QStringLiteral("Mode"), QStringLiteral("advanced"));
	m_config->setBool(ConfigStore::Application, group, QStringLiteral("UseUPnP"), false);
	m_config->setBool(ConfigStore::Application, group, QStringLiteral("Force active mode"), false);

	Qdc::Shares shares(m_thread);
	settle();
	Qdc::SettingsView view(m_config, &shares);
	settle();

	view.findChild<QCheckBox*>(QStringLiteral("connectionUpnp"))->setChecked(true);
	QTRY_VERIFY(m_config->getBool(ConfigStore::Application, group, QStringLiteral("UseUPnP"), false));

	view.findChild<QCheckBox*>(QStringLiteral("connectionForceActive"))->setChecked(true);
	QTRY_VERIFY(m_config->getBool(ConfigStore::Application, group, QStringLiteral("Force active mode"), false));
	QVERIFY(m_config->getBool(ConfigStore::Application, group, QStringLiteral("UseUPnP"), false));

	m_config->setBool(ConfigStore::Application, group, QStringLiteral("UseUPnP"), false);
	m_config->setBool(ConfigStore::Application, group, QStringLiteral("Force active mode"), false);
}

/* A mode that hides the switches says, briefly, what it does in their place; and only its own. */
void TestSettings::eachModeSaysInAWordWhatItDoesInstead()
{
	Qdc::Shares shares(m_thread);
	settle();
	Qdc::SettingsView view(m_config, &shares);
	openPage(view, QStringLiteral("Connection"));

	chooseMode(view, QStringLiteral("automatic"));
	QTRY_VERIFY(visibleText(view).contains(QStringLiteral("checks it with drop.quickdc.org")));
	const QString automatic = visibleText(view);
	QVERIFY(automatic.contains(QStringLiteral("asks the router to forward it")));
	QVERIFY(!automatic.contains(QStringLiteral("passive users")));

	chooseMode(view, QStringLiteral("passive"));
	const QString passive = visibleText(view);
	QVERIFY(passive.contains(QStringLiteral("Peers cannot connect to you")));
	QVERIFY(passive.contains(QStringLiteral("transfers with other passive users do not work")));
	QVERIFY(!passive.contains(QStringLiteral("asks the router")));
}

void TestSettings::thePageSaysWhichPortIsInUse()
{
	using Qdc::ConfigStore;
	const QString group = QStringLiteral("Network");

	m_config->setString(ConfigStore::Application, group, QStringLiteral("Mode"), QStringLiteral("advanced"));
	m_config->setString(ConfigStore::Application, group, QStringLiteral("Bind"), QStringLiteral("127.0.0.1"));
	m_config->setString(ConfigStore::Application, group, QStringLiteral("Bind6"), QString());
	m_config->setNumber(ConfigStore::Application, group, QStringLiteral("Port"), 41711);

	Qdc::Shares shares(m_thread);
	settle();
	Qdc::SettingsView view(m_config, &shares);
	const QLabel* status = view.findChild<QLabel*>(QStringLiteral("connectionPortStatus"));
	QVERIFY(status);

	settle();
	const Qdc::PeerPort port = m_config->peerPort();
	const QString expected = !port.bound ? QStringLiteral("Not listening")
		: port.bound == port.configured ? QStringLiteral("Listening")
		: QStringLiteral("%1 is in use, listening on %2").arg(port.configured).arg(port.bound);
	QTRY_COMPARE(status->text(), expected);
}

void TestSettings::noModeOffersAFieldForTheReachabilityServer()
{
	Qdc::Shares shares(m_thread);
	settle();
	Qdc::SettingsView view(m_config, &shares);
	openPage(view, QStringLiteral("Connection"));

	for (const QString& token : {QStringLiteral("automatic"), QStringLiteral("advanced"), QStringLiteral("passive")})
	{
		chooseMode(view, token);
		QVERIFY2(!visibleText(view).contains(QStringLiteral("Reachability server")),
		         qPrintable(QStringLiteral("a server field is offered in ") + token));
	}
}

/* The only way to measure against a server of one's own; choosing a mode must not replace it. */
void TestSettings::aServerNamedByHandSurvivesChoosingAMode()
{
	using Qdc::ConfigStore;
	const QString group = QStringLiteral("Network");
	const QString key = QStringLiteral("Reachability server");

	m_config->setString(ConfigStore::Application, group, key, QStringLiteral("drop.example.org"));

	for (const QString& token : {QStringLiteral("automatic"), QStringLiteral("advanced"), QStringLiteral("passive")})
	{
		Qdc::Shares shares(m_thread);
		settle();
		Qdc::SettingsView view(m_config, &shares);
		chooseMode(view, token);
		QTRY_COMPARE(storedMode(m_config), token);
		QCOMPARE(m_config->getString(ConfigStore::Application, group, key), QStringLiteral("drop.example.org"));
	}

	m_config->setString(ConfigStore::Application, group, key, QString());
	{
		Qdc::Shares shares(m_thread);
		settle();
		Qdc::SettingsView view(m_config, &shares);
		chooseMode(view, QStringLiteral("automatic"));
		QTRY_COMPARE(m_config->getString(ConfigStore::Application, group, key), QStringLiteral("drop.quickdc.org"));
	}
}

/* The cost of these is paid out of sight, so the switch itself says it. */
void TestSettings::theCostlySwitchesSayWhatTheyCost()
{
	Qdc::Shares shares(m_thread);
	settle();
	Qdc::SettingsView view(m_config, &shares);

	QVERIFY(view.findChild<QCheckBox*>(QStringLiteral("connectionForceActive"))->toolTip()
		.contains(QStringLiteral("peers fail to reach you")));
	QVERIFY(view.findChild<QCheckBox*>(QStringLiteral("connectionUpnp"))->toolTip()
		.contains(QStringLiteral("local network")));
}

/* Said on the page, in a line: what reading a post, fetching a picture and running a hub give away. */
void TestSettings::thePagesThatDiscloseSaySoInOneLine()
{
	Qdc::Shares shares(m_thread);
	settle();
	Qdc::SettingsView view(m_config, &shares);

	openPage(view, QStringLiteral("Boards"));
	QVERIFY(visibleText(view).contains(QStringLiteral("can tell others you have read it")));

	openPage(view, QStringLiteral("Chat"));
	QVERIFY(visibleText(view).contains(QStringLiteral("tells its sender you are online")));

	openPage(view, QStringLiteral("Local hub"));
	const QString hub = visibleText(view);
	QVERIFY(hub.contains(QStringLiteral("can join")));
	QVERIFY(hub.contains(QStringLiteral("there is no password")));
}

QTEST_MAIN(TestSettings)

#include "tst_settings.moc"

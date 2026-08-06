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

QTEST_MAIN(TestSettings)

#include "tst_settings.moc"

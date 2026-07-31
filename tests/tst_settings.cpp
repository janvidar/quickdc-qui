/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include <core/configbridge.h>
#include <core/corethread.h>
#include <core/shares.h>
#include <gui/settingsview.h>

#include <QDir>
#include <QLineEdit>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
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
	 * The page is read a group at a time. Read key by key it was fourteen blocking trips
	 * to the core thread, so opening it while the core was busy - a transfer moving
	 * chunks, a finished hash - waited a whole core turn fourteen times over.
	 *
	 * Four groups still means four waits, so this does not assert that the page is
	 * instant; it asserts that it costs a handful of turns rather than a page of them.
	 */
	m_thread->post([] { QThread::msleep(300); });

	QElapsedTimer timer;
	timer.start();

	Qdc::SettingsView view(&config, &shares);
	view.load();

	const qint64 spent = timer.elapsed();

	QVERIFY2(spent < 1200,
	         qPrintable(QStringLiteral("loading the page took %1 ms while the core was "
	                                   "busy").arg(spent)));
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

QTEST_MAIN(TestSettings)

#include "tst_settings.moc"

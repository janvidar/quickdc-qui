/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "configbridge.h"

#include "corethread.h"

#include <api/config.h>
#include <api/core.h>
#include <samurai/messagehandler.h>

#include <QTimer>

namespace {

/**
 * How long a save waits for the next one before going to disk.
 *
 * A form saves a page of settings and then asks for a write, and every store a
 * numbered list keeps does the same on every change. Writing the file is a
 * rewrite of the whole of it, so the ones that arrive together are better off
 * as one.
 */
const int SAVE_DELAY_MS = 200;

QuickDC::Preferences* store_of(QuickDC::Core* core, Qdc::ConfigStore store)
{
	if (!core) return nullptr;

	return (store == Qdc::ConfigStore::Session) ? core->session : core->config;
}

bool text_is_true(const QString& text)
{
	return text == QLatin1String("true") || text == QLatin1String("1");
}

}

Qdc::ConfigBridge::ConfigBridge(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
	, m_relay(Relay::create())
	, m_saveTimer(new QTimer(this))
{
	m_saveTimer->setSingleShot(true);
	m_saveTimer->setInterval(SAVE_DELAY_MS);
	connect(m_saveTimer, &QTimer::timeout, this, &ConfigBridge::flush);

	if (m_thread)
	{
		connect(m_thread, &CoreThread::coreStarted, this, &ConfigBridge::seed);
		seed();
	}
}

Qdc::ConfigBridge::~ConfigBridge()
{
	m_relay->detach();

	/*
	 * A settings change still sitting behind the timer would otherwise go with the
	 * window, and somebody who typed a nickname and closed the program would find
	 * it had not been kept. Nothing to write it with if the core thread has already
	 * gone, which is a program on its way out rather than a window being closed.
	 */
	if (m_thread && m_saveTimer->isActive()) flush();
}

void Qdc::ConfigBridge::seed()
{
	if (!m_thread || !m_thread->isCoreRunning()) return;

	Store application;
	Store session;

	/*
	 * The one call here that waits, and it happens once, before there is a window
	 * to freeze. Reading the whole of both files in a single job is what buys
	 * every later read for nothing; the alternative - filling the copy in the
	 * background - would have every view draw defaults first and redraw when the
	 * settings arrived, for a wait that is over before the first frame.
	 */
	m_thread->runOnCoreBlocking([&] {
		QuickDC::Core* core = m_thread->core();
		if (!core) return;

		const auto take = [](QuickDC::Preferences* preferences, Store& into) {
			if (!preferences) return;

			for (const auto& section : preferences->snapshot())
			{
				Section& group = into[QString::fromUtf8(section.first.c_str())];

				for (const auto& setting : section.second)
				{
					group.insert(QString::fromUtf8(setting.first.c_str()),
					             QString::fromUtf8(setting.second.c_str()));
				}
			}
		};

		take(core->config, application);
		take(core->session, session);
	});

	m_application = application;
	m_session = session;
}

Qdc::ConfigBridge::Store& Qdc::ConfigBridge::mirror(ConfigStore store)
{
	return (store == ConfigStore::Session) ? m_session : m_application;
}

const Qdc::ConfigBridge::Store& Qdc::ConfigBridge::mirror(ConfigStore store) const
{
	return (store == ConfigStore::Session) ? m_session : m_application;
}

const QString* Qdc::ConfigBridge::find(ConfigStore store, const QString& group,
                                       const QString& key) const
{
	const Store& from = mirror(store);

	const auto section = from.constFind(group);
	if (section == from.constEnd()) return nullptr;

	const auto setting = section->constFind(key);
	if (setting == section->constEnd()) return nullptr;

	return &setting.value();
}

QString Qdc::ConfigBridge::getString(ConfigStore store, const QString& group,
                                     const QString& key,
                                     const QString& defaultValue) const
{
	const QString* value = find(store, group, key);
	return value ? *value : defaultValue;
}

int Qdc::ConfigBridge::getNumber(ConfigStore store, const QString& group,
                                 const QString& key, int defaultValue) const
{
	const QString* value = find(store, group, key);
	if (!value) return defaultValue;

	bool numeric = false;
	const int read = value->toInt(&numeric);

	return numeric ? read : defaultValue;
}

bool Qdc::ConfigBridge::getBool(ConfigStore store, const QString& group,
                                const QString& key, bool defaultValue) const
{
	const QString* value = find(store, group, key);
	return value ? text_is_true(*value) : defaultValue;
}

bool Qdc::ConfigBridge::contains(ConfigStore store, const QString& group,
                                 const QString& key) const
{
	return find(store, group, key) != nullptr;
}

void Qdc::ConfigBridge::write(ConfigStore store, const QString& group,
                              const QString& key, const QString& value)
{
	mirror(store)[group].insert(key, value);

	if (m_thread)
	{
		const std::string section = group.toStdString();
		const std::string name = key.toStdString();
		const std::string text = value.toStdString();

		CoreThread* thread = m_thread.data();

		thread->post([thread, store, section, name, text] {
			if (QuickDC::Preferences* preferences = store_of(thread->core(), store))
				preferences->setString(section, name, text);
		});
	}

	emit changed(store, group, key);
}

void Qdc::ConfigBridge::setString(ConfigStore store, const QString& group,
                                  const QString& key, const QString& value)
{
	write(store, group, key, value);
}

void Qdc::ConfigBridge::setNumber(ConfigStore store, const QString& group,
                                  const QString& key, int value)
{
	write(store, group, key, QString::number(value));
}

void Qdc::ConfigBridge::setBool(ConfigStore store, const QString& group,
                                const QString& key, bool value)
{
	write(store, group, key,
	      value ? QStringLiteral("true") : QStringLiteral("false"));
}

void Qdc::ConfigBridge::remove(ConfigStore store, const QString& group,
                               const QString& key)
{
	Store& from = mirror(store);

	const auto section = from.find(group);
	if (section != from.end()) section->remove(key);

	if (m_thread)
	{
		const std::string name = group.toStdString();
		const std::string setting = key.toStdString();

		CoreThread* thread = m_thread.data();

		thread->post([thread, store, name, setting] {
			if (QuickDC::Preferences* preferences = store_of(thread->core(), store))
				preferences->remove(name, setting);
		});
	}

	emit changed(store, group, key);
}

QVariantHash Qdc::ConfigBridge::readGroup(ConfigStore store, const QString& group,
                                          const QVariantHash& wanted) const
{
	QVariantHash read;

	for (auto one = wanted.constBegin(); one != wanted.constEnd(); ++one)
	{
		const QVariant& fallback = one.value();

		switch (fallback.typeId())
		{
			case QMetaType::Bool:
				read.insert(one.key(),
				            getBool(store, group, one.key(), fallback.toBool()));
				break;

			case QMetaType::Int:
			case QMetaType::UInt:
			case QMetaType::LongLong:
			case QMetaType::ULongLong:
				read.insert(one.key(),
				            getNumber(store, group, one.key(), fallback.toInt()));
				break;

			default:
				read.insert(one.key(),
				            getString(store, group, one.key(), fallback.toString()));
				break;
		}
	}

	return read;
}

void Qdc::ConfigBridge::replaceGroup(ConfigStore store, const QString& group,
                                     const QStringList& removeKeys,
                                     const QList<QPair<QString, QString>>& values)
{
	Store& from = mirror(store);

	const auto section = from.find(group);
	if (section != from.end())
	{
		for (const QString& key : removeKeys)
			section->remove(key);
	}

	Section& into = from[group];
	for (const QPair<QString, QString>& value : values)
		into.insert(value.first, value.second);

	if (m_thread)
	{
		const std::string name = group.toStdString();

		std::vector<std::string> drop;
		drop.reserve(static_cast<size_t>(removeKeys.size()));
		for (const QString& key : removeKeys)
			drop.push_back(key.toStdString());

		std::vector<std::pair<std::string, std::string>> set;
		set.reserve(static_cast<size_t>(values.size()));
		for (const QPair<QString, QString>& value : values)
			set.emplace_back(value.first.toStdString(), value.second.toStdString());

		CoreThread* thread = m_thread.data();

		thread->post([thread, store, name, drop, set] {
			QuickDC::Preferences* preferences = store_of(thread->core(), store);
			if (!preferences) return;

			for (const std::string& key : drop)
				preferences->remove(name, key);

			for (const auto& value : set)
				preferences->setString(name, value.first, value.second);
		});
	}

	/*
	 * One report for the group rather than one per key: this is what a numbered
	 * list is rewritten through, and a listener wanting a key at a time would be
	 * told about several hundred of them.
	 */
	emit changed(store, group, QString());
}

void Qdc::ConfigBridge::save(ConfigStore store)
{
	if (!m_thread) return;

	if (store == ConfigStore::Session)
		m_saveSession = true;
	else
		m_saveApplication = true;

	if (!m_saveTimer->isActive()) m_saveTimer->start();
}

void Qdc::ConfigBridge::flush()
{
	m_saveTimer->stop();

	const bool application = m_saveApplication;
	const bool session = m_saveSession;

	m_saveApplication = false;
	m_saveSession = false;

	CoreThread* thread = m_thread.data();
	if (!thread) return;

	const auto put = [this, thread](ConfigStore store) {
		thread->call(m_relay, [thread, store] {
			QuickDC::Preferences* preferences = store_of(thread->core(), store);
			return preferences ? preferences->write() : false;
		}, [this, store](bool written) {
			if (written)
				emit saved(store);
			else
				emit saveFailed(store);
		});
	};

	if (application) put(ConfigStore::Application);
	if (session) put(ConfigStore::Session);
}

void Qdc::ConfigBridge::notifyChanged()
{
	if (!m_thread) return;

	m_thread->post([] {
		/*
		 * The core's own event bus rather than anything of ours: this is what
		 * the sessions already listen for, and the handler belongs to the core
		 * thread, which is where a posted job runs.
		 */
		if (Samurai::MessageHandler* handler = Samurai::MessageHandler::getInstance())
			handler->postMessage(QuickDC::MsgConfigUpdated, nullptr, 0, 0);
	});
}

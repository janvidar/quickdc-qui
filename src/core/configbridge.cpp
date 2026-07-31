/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "configbridge.h"

#include "corethread.h"

#include <api/config.h>
#include <api/core.h>
#include <samurai/messagehandler.h>

namespace {

/**
 * The store, selected on the group in the same breath.
 *
 * Not a plain accessor: the group is shared mutable state, so it is only ever
 * safe to set it and use it without returning to the caller in between - which
 * is why every entry point below is one job that does both.
 */
QuickDC::Preferences* select(QuickDC::Core* core, Qdc::ConfigStore store,
                             const QString& group)
{
	if (!core) return nullptr;

	QuickDC::Preferences* preferences =
		(store == Qdc::ConfigStore::Session) ? core->session : core->config;

	if (!preferences) return nullptr;

	preferences->setGroup(group.toStdString());
	return preferences;
}

}

Qdc::ConfigBridge::ConfigBridge(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
{
}

QString Qdc::ConfigBridge::getString(ConfigStore store, const QString& group,
                                     const QString& key,
                                     const QString& defaultValue) const
{
	QString value = defaultValue;
	if (!m_thread) return value;

	m_thread->invoke([&] {
		QuickDC::Preferences* preferences = select(m_thread->core(), store, group);
		if (!preferences) return;

		/*
		 * getString() returns a pointer into the store's own map, good only
		 * until the next write, so it is copied before the job ends.
		 */
		const char* stored = preferences->getString(key.toStdString(),
		                                            defaultValue.toStdString());
		if (stored) value = QString::fromUtf8(stored);
	});

	return value;
}

int Qdc::ConfigBridge::getNumber(ConfigStore store, const QString& group,
                                 const QString& key, int defaultValue) const
{
	int value = defaultValue;
	if (!m_thread) return value;

	m_thread->invoke([&] {
		QuickDC::Preferences* preferences = select(m_thread->core(), store, group);
		if (preferences)
			value = preferences->getNumber(key.toStdString(), defaultValue);
	});

	return value;
}

bool Qdc::ConfigBridge::getBool(ConfigStore store, const QString& group,
                                const QString& key, bool defaultValue) const
{
	bool value = defaultValue;
	if (!m_thread) return value;

	m_thread->invoke([&] {
		QuickDC::Preferences* preferences = select(m_thread->core(), store, group);
		if (preferences)
			value = preferences->getBool(key.toStdString(), defaultValue);
	});

	return value;
}

bool Qdc::ConfigBridge::contains(ConfigStore store, const QString& group,
                                 const QString& key) const
{
	bool present = false;
	if (!m_thread) return present;

	m_thread->invoke([&] {
		QuickDC::Preferences* preferences = select(m_thread->core(), store, group);
		if (preferences)
			present = preferences->haveSetting(key.toStdString());
	});

	return present;
}

void Qdc::ConfigBridge::setString(ConfigStore store, const QString& group,
                                  const QString& key, const QString& value)
{
	if (!m_thread) return;

	m_thread->invoke([&] {
		QuickDC::Preferences* preferences = select(m_thread->core(), store, group);
		if (preferences)
			preferences->setString(key.toStdString(), value.toStdString());
	});
}

void Qdc::ConfigBridge::setNumber(ConfigStore store, const QString& group,
                                  const QString& key, int value)
{
	if (!m_thread) return;

	m_thread->invoke([&] {
		QuickDC::Preferences* preferences = select(m_thread->core(), store, group);
		if (preferences)
			preferences->setNumber(key.toStdString(), value);
	});
}

void Qdc::ConfigBridge::setBool(ConfigStore store, const QString& group,
                                const QString& key, bool value)
{
	if (!m_thread) return;

	m_thread->invoke([&] {
		QuickDC::Preferences* preferences = select(m_thread->core(), store, group);
		if (preferences)
			preferences->setBool(key.toStdString(), value);
	});
}

void Qdc::ConfigBridge::remove(ConfigStore store, const QString& group,
                               const QString& key)
{
	if (!m_thread) return;

	m_thread->invoke([&] {
		QuickDC::Preferences* preferences = select(m_thread->core(), store, group);
		if (preferences)
			preferences->remove(key.toStdString());
	});
}

QVariantHash Qdc::ConfigBridge::readGroup(ConfigStore store, const QString& group,
                                          const QVariantHash& wanted) const
{
	QVariantHash read;
	if (!m_thread || wanted.isEmpty()) return read;

	m_thread->invoke([&] {
		QuickDC::Preferences* preferences = select(m_thread->core(), store, group);
		if (!preferences) return;

		/*
		 * One group selection for the whole batch, as replaceGroup() does and for the
		 * same reason: nothing else can select another group inside a single job.
		 */
		for (auto one = wanted.constBegin(); one != wanted.constEnd(); ++one)
		{
			const std::string key = one.key().toStdString();
			const QVariant& fallback = one.value();

			switch (fallback.typeId())
			{
				case QMetaType::Bool:
					read.insert(one.key(),
					            preferences->getBool(key, fallback.toBool()));
					break;

				case QMetaType::Int:
				case QMetaType::UInt:
				case QMetaType::LongLong:
				case QMetaType::ULongLong:
					read.insert(one.key(),
					            preferences->getNumber(key, fallback.toInt()));
					break;

				default:
					read.insert(one.key(), QString::fromUtf8(preferences->getString(
						key, fallback.toString().toStdString())));
					break;
			}
		}
	});

	return read;
}

void Qdc::ConfigBridge::replaceGroup(ConfigStore store, const QString& group,
                                     const QStringList& removeKeys,
                                     const QList<QPair<QString, QString>>& values)
{
	if (!m_thread) return;

	m_thread->invoke([&] {
		QuickDC::Preferences* preferences = select(m_thread->core(), store, group);
		if (!preferences) return;

		/*
		 * One group selection for the whole batch, which is safe precisely because
		 * this is a single job - nothing else can select another group in between.
		 */
		for (const QString& key : removeKeys)
			preferences->remove(key.toStdString());

		for (const QPair<QString, QString>& value : values)
			preferences->setString(value.first.toStdString(),
			                       value.second.toStdString());
	});
}

bool Qdc::ConfigBridge::save(ConfigStore store)
{
	bool written = false;
	if (!m_thread) return written;

	m_thread->invoke([&] {
		QuickDC::Core* core = m_thread->core();
		if (!core) return;

		QuickDC::Preferences* preferences =
			(store == ConfigStore::Session) ? core->session : core->config;

		if (preferences) written = preferences->write();
	});

	if (written) emit saved(store);

	return written;
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

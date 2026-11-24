/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "configbridge.h"

#include "corechannel.h"
#include "corethread.h"


#include <QTimer>

namespace {

const int SAVE_DELAY_MS = 200;

bool text_is_true(const QString& text)
{
	return text == QLatin1String("true") || text == QLatin1String("1");
}

QuickDC::Api::SettingsStore converted(Qdc::ConfigStore store)
{
	return store == Qdc::ConfigStore::Session ? QuickDC::Api::SettingsStore::Session
	                                          : QuickDC::Api::SettingsStore::Application;
}

std::string to_std(const QString& text)
{
	const QByteArray utf8 = text.toUtf8();
	return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

}

Qdc::ConfigBridge::ConfigBridge(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
	, m_saveTimer(new QTimer(this))
{
	m_saveTimer->setSingleShot(true);
	m_saveTimer->setInterval(SAVE_DELAY_MS);
	connect(m_saveTimer, &QTimer::timeout, this, &ConfigBridge::flush);

	if (!m_thread) return;

	connect(m_thread->channel(), &CoreChannel::settingChanged, this, &ConfigBridge::changed);
	connect(m_thread->channel(), &CoreChannel::received, this, &ConfigBridge::onCoreEvent);
}

Qdc::ConfigBridge::~ConfigBridge()
{
	if (m_thread && m_saveTimer->isActive()) flush();
}

const QString* Qdc::ConfigBridge::find(ConfigStore store, const QString& group,
                                       const QString& key) const
{
	return m_thread ? m_thread->channel()->setting(store, group, key) : nullptr;
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
	replaceGroup(store, group, {}, {qMakePair(key, value)}, key);
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

QString Qdc::ConfigBridge::read(const Settings::Text& setting) const
{
	return getString(ConfigStore::Application, QString::fromUtf8(setting.group),
	                 QString::fromUtf8(setting.key), QString::fromUtf8(setting.fallback));
}

int Qdc::ConfigBridge::read(const Settings::Number& setting) const
{
	return getNumber(ConfigStore::Application, QString::fromUtf8(setting.group),
	                 QString::fromUtf8(setting.key), setting.fallback);
}

bool Qdc::ConfigBridge::read(const Settings::Flag& setting) const
{
	return getBool(ConfigStore::Application, QString::fromUtf8(setting.group),
	               QString::fromUtf8(setting.key), setting.fallback);
}

void Qdc::ConfigBridge::write(const Settings::Text& setting, const QString& value)
{
	setString(ConfigStore::Application, QString::fromUtf8(setting.group),
	          QString::fromUtf8(setting.key), value);
}

void Qdc::ConfigBridge::write(const Settings::Number& setting, int value)
{
	setNumber(ConfigStore::Application, QString::fromUtf8(setting.group),
	          QString::fromUtf8(setting.key), value);
}

void Qdc::ConfigBridge::write(const Settings::Flag& setting, bool value)
{
	setBool(ConfigStore::Application, QString::fromUtf8(setting.group),
	        QString::fromUtf8(setting.key), value);
}

void Qdc::ConfigBridge::remove(ConfigStore store, const QString& group,
                               const QString& key)
{
	replaceGroup(store, group, {key}, {}, key);
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
				read.insert(one.key(), getBool(store, group, one.key(), fallback.toBool()));
				break;

			case QMetaType::Int:
			case QMetaType::UInt:
			case QMetaType::LongLong:
			case QMetaType::ULongLong:
				read.insert(one.key(), getNumber(store, group, one.key(), fallback.toInt()));
				break;

			default:
				read.insert(one.key(), getString(store, group, one.key(), fallback.toString()));
				break;
		}
	}

	return read;
}

void Qdc::ConfigBridge::replaceGroup(ConfigStore store, const QString& group,
                                     const QStringList& removeKeys,
                                     const QList<QPair<QString, QString>>& values)
{
	replaceGroup(store, group, removeKeys, values, QString());
}

void Qdc::ConfigBridge::replaceGroup(ConfigStore store, const QString& group,
                                     const QStringList& removeKeys,
                                     const QList<QPair<QString, QString>>& values,
                                     const QString& announced)
{
	if (m_thread)
	{
		const std::uint64_t origin =
			m_thread->channel()->writeAhead(store, group, removeKeys, values);

		QuickDC::Api::Commands::ReplaceGroup command;
		command.store = converted(store);
		command.group = to_std(group);
		command.origin = origin;
		for (const QString& key : removeKeys) command.removeKeys.push_back(to_std(key));
		for (const QPair<QString, QString>& value : values)
			command.values.emplace_back(to_std(value.first), to_std(value.second));

		m_thread->submit(command);
	}

	emit changed(store, group, announced);
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

	if (!m_thread) return;

	const auto put = [this](ConfigStore store) {
		/* Every bridge hears every answer, so the numbers are unique across them. */
		static std::uint64_t saves = 0;
		const std::uint64_t request = ++saves;
		if (m_thread->submit(QuickDC::Api::Commands::SaveSettings{converted(store), request}))
			m_saving.insert(request);
	};

	if (application) put(ConfigStore::Application);
	if (session) put(ConfigStore::Session);
}

void Qdc::ConfigBridge::onCoreEvent(const QuickDC::Api::Event& event)
{
	const auto* saved_ = event.as<QuickDC::Api::Events::SettingsSaved>();
	if (!saved_ || !m_saving.remove(saved_->request)) return;

	const ConfigStore store = saved_->store == QuickDC::Api::SettingsStore::Session
		? ConfigStore::Session : ConfigStore::Application;

	if (saved_->ok) emit saved(store);
	else emit saveFailed(store);
}


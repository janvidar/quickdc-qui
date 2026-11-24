/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "corechannel.h"

#include <utility>

namespace {

QString from(const std::string& text)
{
	return QString::fromUtf8(text.c_str(), static_cast<qsizetype>(text.size()));
}

QString pendingKey(const QString& group, const QString& key)
{
	return group + QLatin1Char('\n') + key;
}

Qdc::ConfigStore converted(QuickDC::Api::SettingsStore store)
{
	return store == QuickDC::Api::SettingsStore::Session ? Qdc::ConfigStore::Session
	                                                    : Qdc::ConfigStore::Application;
}

}

Qdc::CoreChannel::CoreChannel(QObject* parent)
	: QObject(parent)
{
	qRegisterMetaType<QuickDC::Api::Event>();

	/* First, so that every other receiver of an event finds the copy up to date. */
	connect(this, &CoreChannel::received, this, &CoreChannel::absorb, Qt::QueuedConnection);
	connect(this, &CoreChannel::hubReceived, this, &CoreChannel::dispatchToHub, Qt::QueuedConnection);
}

void Qdc::CoreChannel::deliver(const QuickDC::Api::Event& event)
{
	if (hubOf(event))
		emit hubReceived(event, QPrivateSignal());
	else
		emit received(event);
}

std::optional<QuickDC::Api::HubHandle> Qdc::CoreChannel::hubOf(const QuickDC::Api::Event& event)
{
	return std::visit([](const auto& e) -> std::optional<QuickDC::Api::HubHandle> {
		if constexpr (requires { e.hub; })
			return e.hub;
		else
			return std::nullopt;
	}, event.value);
}

Qdc::HubRoute* Qdc::CoreChannel::route(QuickDC::Api::HubHandle hub)
{
	HubRoute*& found = m_routes[hub];
	if (!found) found = new HubRoute(this);
	return found;
}

void Qdc::CoreChannel::releaseRoute(QuickDC::Api::HubHandle hub)
{
	delete m_routes.take(hub);
}

void Qdc::CoreChannel::dispatchToHub(const QuickDC::Api::Event& event)
{
	const std::optional<QuickDC::Api::HubHandle> hub = hubOf(event);
	if (!hub) return;

	/* A hub nobody is listening to any more: its events end here. */
	if (HubRoute* to = m_routes.value(*hub)) emit to->received(event);
}

void Qdc::CoreChannel::offerSnapshot(QuickDC::Api::SettingsSnapshot snapshot)
{
	std::lock_guard<std::mutex> guard(m_offered);
	m_snapshot = std::move(snapshot);
}

void Qdc::CoreChannel::adoptSnapshot()
{
	std::optional<QuickDC::Api::SettingsSnapshot> taken;
	{
		std::lock_guard<std::mutex> guard(m_offered);
		taken.swap(m_snapshot);
	}
	if (!taken) return;

	const auto copy = [](const QuickDC::Api::SettingsGroups& from_, Store& into, std::uint64_t revision) {
		into.groups.clear();
		for (const auto& [group, settings] : from_)
		{
			Section& section = into.groups[from(group)];
			for (const auto& [key, value] : settings) section.insert(from(key), from(value));
		}
		into.revision = revision;
		into.pending.clear();
	};

	copy(taken->application, m_application, taken->applicationRevision);
	copy(taken->session, m_session, taken->sessionRevision);
}

Qdc::CoreChannel::Store& Qdc::CoreChannel::of(ConfigStore store)
{
	return store == ConfigStore::Session ? m_session : m_application;
}

const QString* Qdc::CoreChannel::setting(ConfigStore store, const QString& group, const QString& key)
{
	adoptSnapshot();

	const Store& from_ = of(store);
	const auto section = from_.groups.constFind(group);
	if (section == from_.groups.constEnd()) return nullptr;

	const auto value = section->constFind(key);
	return value == section->constEnd() ? nullptr : &value.value();
}

std::uint64_t Qdc::CoreChannel::writeAhead(ConfigStore store, const QString& group,
                                          const QStringList& removeKeys,
                                          const QList<QPair<QString, QString>>& values)
{
	adoptSnapshot();

	Store& into = of(store);
	const std::uint64_t write = ++m_nextWrite;

	const auto section = into.groups.find(group);
	for (const QString& key : removeKeys)
	{
		if (section != into.groups.end()) section->remove(key);
		into.pending.insert(pendingKey(group, key), write);
	}

	Section& target = into.groups[group];
	for (const QPair<QString, QString>& value : values)
	{
		target.insert(value.first, value.second);
		into.pending.insert(pendingKey(group, value.first), write);
	}

	return write;
}

void Qdc::CoreChannel::absorb(const QuickDC::Api::Event& event)
{
	adoptSnapshot();

	if (const auto* written = event.as<QuickDC::Api::Events::SettingsWritten>())
	{
		Store& store = of(converted(written->store));
		for (auto at = store.pending.begin(); at != store.pending.end(); )
			at = (at.value() == written->origin) ? store.pending.erase(at) : std::next(at);
		return;
	}

	const auto* changed = event.as<QuickDC::Api::Events::SettingChanged>();
	if (!changed) return;

	const ConfigStore which = converted(changed->store);
	Store& store = of(which);

	/* Already in the snapshot this copy was seeded from. */
	if (changed->revision <= store.revision) return;
	store.revision = changed->revision;

	const QString group = from(changed->group);
	const QString key = from(changed->key);

	/* A write made here since supersedes it, and holds the value already. */
	if (store.pending.contains(pendingKey(group, key))) return;

	if (changed->removed)
	{
		const auto section = store.groups.find(group);
		if (section != store.groups.end()) section->remove(key);
	}
	else
	{
		store.groups[group].insert(key, from(changed->value));
	}

	emit settingChanged(which, group, key);
}

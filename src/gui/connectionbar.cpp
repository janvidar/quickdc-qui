/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "connectionbar.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QShowEvent>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>

namespace {

/** How often the wait is redrawn, which is what "in 11 seconds" has to keep up with. */
constexpr int TICK_MS = 1000;

}

Qdc::ConnectionBar::ConnectionBar(QWidget* parent)
	: QWidget(parent)
	, m_label(new QLabel(this))
	, m_now(new QPushButton(tr("Reconnect now"), this))
	, m_tick(new QTimer(this))
{
	setObjectName(QStringLiteral("connectionBar"));

	/*
	 * Opaque, and coloured by the stylesheet. Both are needed: a plain QWidget draws
	 * no background from a stylesheet at all without the attribute - which is what
	 * left this band with amber text on the conversation's own white - and the fill
	 * is what keeps the feed from showing through if a theme sets no colour for it,
	 * which matters here because this is laid over the feed rather than beside it.
	 */
	setAttribute(Qt::WA_StyledBackground, true);
	setAutoFillBackground(true);

	m_label->setObjectName(QStringLiteral("connectionBarText"));
	m_label->setTextInteractionFlags(Qt::TextSelectableByMouse);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(10, 5, 10, 5);
	layout->setSpacing(6);
	layout->addWidget(m_label, 1);

	m_now->setObjectName(QStringLiteral("connectionBarAction"));
	m_now->setCursor(Qt::PointingHandCursor);
	m_now->setFlat(true);

	/* Or it takes the return key from the message being written underneath. */
	m_now->setAutoDefault(false);
	m_now->setFocusPolicy(Qt::NoFocus);

	m_now->hide();
	layout->addWidget(m_now, 0);

	connect(m_now, &QPushButton::clicked, this, [this] {
		/*
		 * Hidden at once. The attempt reports a status of its own within the
		 * moment, and a button still offering to start one that is already
		 * running invites a second press.
		 */
		m_remaining = -1;
		m_tick->stop();
		refresh();

		emit reconnectRequested();
	});

	m_tick->setInterval(TICK_MS);
	m_tick->setTimerType(Qt::CoarseTimer);

	connect(m_tick, &QTimer::timeout, this, [this] {
		if (m_remaining > 0) --m_remaining;

		/*
		 * Stopped at zero rather than counting into the negative: what happens next
		 * is an attempt, and the attempt reports itself.
		 */
		if (m_remaining <= 0) m_tick->stop();

		refresh();
	});

	/*
	 * Hidden to begin with. A pane is built before anything is known about the hub,
	 * and a band that flashes up on every pane that opens is worse than one that
	 * appears a moment late.
	 */
	hide();
}

void Qdc::ConnectionBar::attachTo(QTabWidget* tabs)
{
	if (!tabs) return;

	m_tabs = tabs;
	setParent(tabs);

	/*
	 * The tab widget is watched for being resized, and its bar for changing height -
	 * a theme with a larger font moves the line this sits under. Both arrive here as
	 * a resize on the object that was installed on.
	 */
	tabs->installEventFilter(this);
	tabs->tabBar()->installEventFilter(this);

	place();
}

bool Qdc::ConnectionBar::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::Resize && m_tabs
	    && (watched == m_tabs || watched == m_tabs->tabBar()))
	{
		place();
	}

	return QWidget::eventFilter(watched, event);
}

void Qdc::ConnectionBar::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);

	/* A band hidden through a resize has to be placed before it is seen. */
	place();
}

void Qdc::ConnectionBar::place()
{
	if (!m_tabs) return;

	const QTabBar* bar = m_tabs->tabBar();
	const int top = (bar && bar->isVisible()) ? bar->height() : 0;

	setGeometry(0, top, m_tabs->width(), sizeHint().height());

	/* Above the pages, which are siblings of this rather than parents of it. */
	raise();
}

void Qdc::ConnectionBar::setStatus(NetStatus net, HubStatus hub)
{
	/*
	 * A wait belongs to the session that ended, so a new network status retires it -
	 * the attempt has either happened or been overtaken. Only a network status does:
	 * the login states arrive on their own and would otherwise cancel a countdown
	 * that is still perfectly good.
	 */
	if (net != m_net)
	{
		m_remaining = -1;
		m_tick->stop();
	}

	m_net = net;
	m_hub = hub;

	refresh();
}

void Qdc::ConnectionBar::setAbsent(const QString& who)
{
	if (m_absent == who) return;

	m_absent = who;
	refresh();
}

void Qdc::ConnectionBar::setReconnectIn(int seconds)
{
	m_remaining = seconds;

	if (seconds > 0) m_tick->start();
	else m_tick->stop();

	refresh();
}

QAbstractButton* Qdc::ConnectionBar::reconnectButton() const
{
	return m_now;
}

QString Qdc::ConnectionBar::text() const
{
	/*
	 * The label keeps whatever it last said, so a hidden band would answer with the
	 * reason it was hidden for. Empty is the honest answer: nothing is being said.
	 */
	return isHidden() ? QString() : m_label->text();
}

QString Qdc::ConnectionBar::reason() const
{
	/*
	 * Ahead of the connection, because it is the more specific answer. A hub that is
	 * up and has let us in says nothing about whether the person being written to is
	 * still there, and that is the case where the band would otherwise be hidden.
	 */
	if (!m_absent.isEmpty())
		return tr("%1 has left the hub").arg(m_absent);

	switch (m_net)
	{
		case NetStatus::Connected:
		case NetStatus::TlsConnected:
			break;

		case NetStatus::Lookup:
			return tr("Looking up the address…");

		case NetStatus::Connecting:
			return tr("Connecting…");

		case NetStatus::Invalid:
			return tr("Not connected");

		case NetStatus::Disconnected:
		case NetStatus::TlsDisconnected:
			return tr("Disconnected");

		case NetStatus::ErrorTimeout:
			return tr("The hub did not answer");

		case NetStatus::ErrorConnectionRefused:
			return tr("The connection was refused");

		case NetStatus::ErrorHostNotFound:
			return tr("No such host");

		case NetStatus::ErrorHostUnreachable:
			return tr("The hub could not be reached");

		case NetStatus::ErrorIO:
			return tr("The connection failed");

		case NetStatus::TlsError:
			return tr("The secure connection failed");
	}

	/*
	 * Reachable, so whether this conversation can be used is down to the login. The
	 * empty answer is the only one that hides the band, and only LoggedIn gives it.
	 */
	switch (m_hub)
	{
		case HubStatus::LoggedIn:
			return QString();

		case HubStatus::Password:
			return tr("Waiting for the password…");

		case HubStatus::None:
		case HubStatus::Login:
			return tr("Signing in…");

		default:
			return tr("The hub would not let us in");
	}
}

QString Qdc::ConnectionBar::countdown() const
{
	if (m_remaining < 0) return QString();

	/*
	 * Zero is the moment the timer is due rather than a wait of no seconds, and it
	 * can be sat on for as long as the attempt takes to report anything.
	 */
	if (m_remaining == 0) return tr("trying again now…");

	/*
	 * Both forms written out rather than left to tr()'s %n. Without a translation
	 * loaded that returns the source string as it stands, so a plural marker in it
	 * is read out as one - and this band is the one place the wait is stated, where
	 * "in 1 second(s)" is what everyone sees rather than a tooltip nobody opens.
	 */
	if (m_remaining == 1) return tr("trying again in 1 second");

	return tr("trying again in %1 seconds").arg(m_remaining);
}

void Qdc::ConnectionBar::refresh()
{
	const QString text = reason();

	if (text.isEmpty())
	{
		/*
		 * Ready. The band is hidden rather than emptied, so the conversation under it
		 * is uncovered and there is nothing left to notice.
		 */
		m_tick->stop();
		hide();
		return;
	}

	const QString wait = countdown();

	m_label->setText(wait.isEmpty() ? text
	                                : tr("%1 — %2").arg(text, wait));

	/* Only while there is a wait to cut short. */
	m_now->setVisible(m_remaining > 0);

	/*
	 * Placed after the text rather than before it: the band is as tall as what it
	 * says, and a longer phrase in a translation is taller than this one.
	 */
	place();
	show();
}

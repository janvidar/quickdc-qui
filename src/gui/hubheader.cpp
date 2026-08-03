/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "hubheader.h"

#include "glyphs.h"
#include "theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int GLYPH = 16;

/**
 * A glyph as an icon, at the device pixel ratio of the widget that will show it.
 *
 * QIcon rather than a painted button, because these are ordinary tool buttons and
 * the style should keep drawing their pressed and hovered states. The ratio has
 * to be applied by hand: a pixmap made at the logical size is blurred on a retina
 * display.
 */
QIcon glyph_icon(Qdc::Glyph glyph, const QColor& colour, qreal ratio)
{
	QPixmap pixmap(QSize(GLYPH, GLYPH) * ratio);
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(Qt::transparent);

	QPainter painter(&pixmap);
	Qdc::paint_glyph(painter, glyph, QRectF(0, 0, GLYPH, GLYPH), colour);
	painter.end();

	return QIcon(pixmap);
}

QString net_status_text(Qdc::NetStatus status)
{
	switch (status)
	{
		case Qdc::NetStatus::Invalid:                 return QString();
		case Qdc::NetStatus::Lookup:                  return QObject::tr("Looking up the address…");
		case Qdc::NetStatus::Connecting:              return QObject::tr("Connecting…");
		case Qdc::NetStatus::Connected:               return QObject::tr("Connected");
		case Qdc::NetStatus::Disconnected:            return QObject::tr("Disconnected");
		case Qdc::NetStatus::TlsConnected:            return QObject::tr("Connected securely");
		case Qdc::NetStatus::TlsDisconnected:         return QObject::tr("Disconnected");
		case Qdc::NetStatus::ErrorTimeout:            return QObject::tr("The hub did not answer");
		case Qdc::NetStatus::ErrorConnectionRefused:  return QObject::tr("The connection was refused");
		case Qdc::NetStatus::ErrorHostNotFound:       return QObject::tr("No such host");
		case Qdc::NetStatus::ErrorHostUnreachable:
			return QObject::tr("The hub could not be reached; trying again");
		case Qdc::NetStatus::ErrorIO:                 return QObject::tr("The connection failed");
		case Qdc::NetStatus::TlsError:                return QObject::tr("The secure connection failed");
	}

	return QString();
}

}

Qdc::HubHeader::HubHeader(QWidget* parent)
	: QWidget(parent)
	, m_title(new QLabel(this))
	, m_subtitle(new QLabel(this))
	, m_star(new QToolButton(this))
	, m_lock(new QLabel(this))
	, m_users(new QToolButton(this))
	, m_close(new QToolButton(this))
{
	setObjectName(QStringLiteral("hubHeader"));
	setAutoFillBackground(true);

	QFont titleFont = m_title->font();
	titleFont.setBold(true);
	titleFont.setPointSize(titleFont.pointSize() + 1);
	m_title->setFont(titleFont);
	m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);

	QFont subtitleFont = m_subtitle->font();
	subtitleFont.setPointSize(qMax(1, subtitleFont.pointSize() - 1));
	m_subtitle->setFont(subtitleFont);

	m_subtitle->setObjectName(QStringLiteral("mutedText"));
	m_subtitle->hide();

	m_star->setCheckable(true);
	m_star->setAutoRaise(true);
	m_star->setCursor(Qt::PointingHandCursor);

	m_users->setAutoRaise(true);
	m_users->setCheckable(true);
	m_users->setCursor(Qt::PointingHandCursor);
	m_users->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

	m_close->setAutoRaise(true);
	m_close->setCursor(Qt::PointingHandCursor);
	m_close->setToolTip(tr("Leave"));
	m_close->setAccessibleName(tr("Leave"));

	const qreal ratio = devicePixelRatioF();
	const QColor ink = theme().contentMuted;

	m_users->setIcon(glyph_icon(Glyph::People, ink, ratio));
	m_close->setIcon(glyph_icon(Glyph::Close, ink, ratio));

	QVBoxLayout* text = new QVBoxLayout;
	text->setContentsMargins(0, 0, 0, 0);
	text->setSpacing(0);
	text->addWidget(m_title);
	text->addWidget(m_subtitle);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(10, 6, 8, 6);
	layout->setSpacing(6);
	layout->addLayout(text, 1);
	layout->addWidget(m_star);
	layout->addWidget(m_lock);
	layout->addWidget(m_users);
	layout->addWidget(m_close);

	connect(m_star, &QToolButton::toggled, this, [this](bool on) {
		m_favourite = on;
		refreshStar();
		emit favouriteToggled(on);
	});

	connect(m_users, &QToolButton::toggled, this, &HubHeader::userListToggled);
	connect(m_close, &QToolButton::clicked, this, &HubHeader::closeRequested);

	refreshStar();
	refreshLock();
	setUserCount(0);
}

void Qdc::HubHeader::setTitle(const QString& title)
{
	m_title->setText(title);
	m_title->setToolTip(title);
}

void Qdc::HubHeader::setSubtitle(const QString& subtitle)
{
	m_subtitle->setText(subtitle);
	m_subtitle->setToolTip(subtitle);
	m_subtitle->setVisible(!subtitle.isEmpty());
}

void Qdc::HubHeader::setFavourite(bool favourite)
{
	if (favourite == m_favourite) return;

	m_favourite = favourite;

	/*
	 * The button drives favouriteToggled(), so setting it here would report a
	 * change the caller just made. Blocked, and the state applied by hand.
	 */
	const QSignalBlocker blocker(m_star);
	m_star->setChecked(favourite);
	refreshStar();
}

bool Qdc::HubHeader::isFavourite() const
{
	return m_favourite;
}

void Qdc::HubHeader::setSecure(bool secure)
{
	m_secure = secure;
	refreshLock();
}

void Qdc::HubHeader::setUserCount(int count)
{
	m_userCount = qMax(0, count);

	m_users->setText(QString::number(m_userCount));
	m_users->setToolTip(tr("%n user(s) — click to show the list", nullptr, m_userCount));
	m_users->setAccessibleName(tr("%n user(s)", nullptr, m_userCount));
}

void Qdc::HubHeader::setNetStatus(NetStatus status)
{
	const QString text = net_status_text(status);

	/*
	 * The subtitle carries the hub's own name once there is one, so a status is
	 * only shown while there is not - otherwise connecting to a named hub would
	 * replace its name with "Connected" and never put it back.
	 */
	if (m_subtitle->text().isEmpty() || !text.isEmpty()) m_subtitle->setToolTip(text);

	m_lock->setToolTip(text.isEmpty() ? m_lock->toolTip() : text);
}

void Qdc::HubHeader::setFavouriteVisible(bool visible)
{
	m_star->setVisible(visible);
}

void Qdc::HubHeader::setUserListVisible(bool visible)
{
	m_users->setVisible(visible);
}

void Qdc::HubHeader::setUserListShown(bool shown)
{
	const QSignalBlocker blocker(m_users);
	m_users->setChecked(shown);
}

void Qdc::HubHeader::refreshStar()
{
	const qreal ratio = devicePixelRatioF();

	/*
	 * Filled and gold when set, an outline in the text colour when not. The
	 * outline rather than a dimmed fill, so the two are distinguishable without
	 * relying on colour.
	 */
	const QColor colour = m_favourite ? theme().star : theme().contentMuted;

	m_star->setIcon(glyph_icon(m_favourite ? Glyph::Star : Glyph::StarOutline,
	                           colour, ratio));

	const QString text = m_favourite ? tr("Remove from favourites")
	                                 : tr("Add to favourites");
	m_star->setToolTip(text);
	m_star->setAccessibleName(text);
}

void Qdc::HubHeader::refreshLock()
{
	const qreal ratio = devicePixelRatioF();

	const QColor colour = m_secure ? theme().success : theme().contentMuted;

	QPixmap pixmap(QSize(GLYPH, GLYPH) * ratio);
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(Qt::transparent);

	QPainter painter(&pixmap);
	paint_glyph(painter, m_secure ? Glyph::Lock : Glyph::LockOpen,
	            QRectF(0, 0, GLYPH, GLYPH), colour);
	painter.end();

	m_lock->setPixmap(pixmap);

	const QString text = m_secure
		? tr("The connection to this hub is encrypted")
		: tr("The connection to this hub is not encrypted");

	m_lock->setToolTip(text);
	m_lock->setAccessibleName(text);
}

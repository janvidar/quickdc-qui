/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "hubheader.h"

#include "glyphs.h"
#include "richtext.h"
#include "theme.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QStringList>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int GLYPH = 16;

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
	, m_presence(new QLabel(this))
	, m_lock(new QLabel(this))
	, m_settings(new QToolButton(this))
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

	/*
	 * Both lines hold a name somebody else chose, and a QLabel left to itself guesses at
	 * whether what it was given is rich text - so a hub calling itself "<b>Best</b> Hub"
	 * would be setting the type in the client's own header. Said outright rather than
	 * guessed, which is the same reason renderMarkdown() exists.
	 *
	 * This says nothing about the tooltip on either of them. QToolTip draws in a label of
	 * its own, which nothing here can reach, so a tooltip has to say what it is itself -
	 * see RichText::literalHtml().
	 */
	m_title->setTextFormat(Qt::PlainText);
	m_subtitle->setTextFormat(Qt::PlainText);

	QFont subtitleFont = m_subtitle->font();
	subtitleFont.setPointSize(qMax(1, subtitleFont.pointSize() - 1));
	m_subtitle->setFont(subtitleFont);

	m_subtitle->setObjectName(QStringLiteral("mutedText"));
	m_subtitle->hide();

	m_star->setCheckable(true);
	m_star->setAutoRaise(true);
	m_star->setCursor(Qt::PointingHandCursor);

	/*
	 * The star's place in a conversation. A label rather than a button, because it
	 * reports what somebody else is doing and there is nothing to press.
	 */
	m_presence->setObjectName(QStringLiteral("hubPresence"));
	m_presence->setAlignment(Qt::AlignCenter);
	m_presence->hide();

	m_lock->setObjectName(QStringLiteral("hubPadlock"));

	m_settings->setObjectName(QStringLiteral("hubSettingsButton"));
	m_settings->setCheckable(true);
	m_settings->setAutoRaise(true);
	m_settings->setCursor(Qt::PointingHandCursor);

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

	m_users->setIcon(glyph_icon(Glyph::People, ink, ratio, GLYPH));
	m_close->setIcon(glyph_icon(Glyph::Close, ink, ratio, GLYPH));

	QVBoxLayout* text = new QVBoxLayout;
	text->setContentsMargins(0, 0, 0, 0);
	text->setSpacing(0);
	text->addWidget(m_title);
	text->addWidget(m_subtitle);

	/*
	 * The star leads, because it acts on the hub and the hub is what the two lines
	 * name. Outside the text block rather than in the title's own row: sharing a row
	 * with the title would put the line below it under the star instead of under the
	 * name, and the two would no longer begin at the same edge. Aligned to the top
	 * for the same reason - it belongs to the name, not to the gap under it.
	 *
	 * The left margin is smaller than the right because a tool button brings its own
	 * padding, and without the difference the title starts further in than it did.
	 */
	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(4, 6, 8, 6);
	layout->setSpacing(6);
	layout->addWidget(m_star, 0, Qt::AlignTop);
	layout->addWidget(m_presence, 0, Qt::AlignTop);
	layout->addLayout(text, 1);
	layout->addWidget(m_lock);
	layout->addWidget(m_settings);
	layout->addWidget(m_users);
	layout->addWidget(m_close);

	connect(m_star, &QToolButton::toggled, this, [this](bool on) {
		m_favourite = on;
		refreshStar();
		emit favouriteToggled(on);
	});

	connect(m_settings, &QToolButton::toggled, this, &HubHeader::settingsToggled);
	connect(m_users, &QToolButton::toggled, this, &HubHeader::userListToggled);
	connect(m_close, &QToolButton::clicked, this, &HubHeader::closeRequested);

	refreshStar();
	refreshLock();
	refreshPresence();
	refreshSettings();
	setUserCount(0);

	/*
	 * The figure takes exactly the box the star would have, so that a conversation
	 * and a hub begin their titles at the same place. A tool button carries padding
	 * a bare label does not, and without this the two headers are indented
	 * differently - which is visible the moment one is opened from the other.
	 */
	m_presence->setFixedSize(m_star->sizeHint());
}

void Qdc::HubHeader::setTitle(const QString& title, const QString& tooltip)
{
	/*
	 * Disarmed in the setters rather than at each caller, because every string that
	 * reaches these two lines is something a hub or another user named itself - a hub's
	 * name and description, or a nickname across the top of a conversation - and this
	 * strip is the client's own chrome, which is exactly what RTF0 says such a name must
	 * not be able to be mistaken for.
	 */
	m_title->setText(RichText::displayText(title));
	m_title->setToolTip(RichText::literalHtml(
		RichText::displayText(tooltip.isEmpty() ? title : tooltip)));
}

void Qdc::HubHeader::setHubIdentity(const QString& name,
                                    const QString& description,
                                    const QString& address)
{
	setTitle(name.isEmpty() ? address : name, address);

	setSubtitle(description.isEmpty()
		? (name.isEmpty() ? QString() : address)
		: description);
}

QString Qdc::HubHeader::title() const
{
	return m_title->text();
}

QString Qdc::HubHeader::subtitle() const
{
	return m_subtitle->text();
}

void Qdc::HubHeader::setSubtitle(const QString& subtitle)
{
	const QString shown = RichText::displayText(subtitle);

	m_subtitle->setText(shown);
	m_subtitle->setToolTip(RichText::literalHtml(shown));

	/* A description that came to nothing hides the line rather than leaving a blank. */
	m_subtitle->setVisible(!shown.isEmpty());
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
	m_status = status;
	refreshLock();
}

void Qdc::HubHeader::setPresence(bool away, bool op)
{
	m_away = away;
	m_op = op;
	refreshPresence();
}

void Qdc::HubHeader::setFavouriteVisible(bool visible)
{
	m_star->setVisible(visible);
}

void Qdc::HubHeader::setPresenceVisible(bool visible)
{
	m_presence->setVisible(visible);
}

void Qdc::HubHeader::setSettingsVisible(bool visible)
{
	m_settings->setVisible(visible);
}

void Qdc::HubHeader::setSettingsShown(bool shown)
{
	const QSignalBlocker blocker(m_settings);
	m_settings->setChecked(shown);
}

void Qdc::HubHeader::setSettingsUnsaved(bool unsaved)
{
	if (unsaved == m_settingsUnsaved) return;

	m_settingsUnsaved = unsaved;
	refreshSettings();
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

void Qdc::HubHeader::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);

	/* Both are pixmaps painted in the ink of the scheme that was current. */
	if (event->type() == QEvent::PaletteChange
	    || event->type() == QEvent::StyleChange)
	{
		refreshStar();
		refreshLock();
		refreshPresence();
		refreshSettings();
	}
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
	                           colour, ratio, GLYPH));

	const QString text = m_favourite ? tr("Remove from favourites")
	                                 : tr("Add to favourites");
	m_star->setToolTip(text);
	m_star->setAccessibleName(text);
}

void Qdc::HubHeader::refreshPresence()
{
	const qreal ratio = devicePixelRatioF();

	/*
	 * The same figure the user list draws for the same person, at the size the rest
	 * of this strip is drawn at - so a conversation and the row it was opened from
	 * say the same thing about somebody in the same way.
	 */
	m_presence->setPixmap(presence_icon(m_away, m_op, GLYPH)
		.pixmap(QSize(GLYPH, GLYPH), ratio));

	QStringList said;
	if (m_op) said << tr("Operator");
	said << (m_away ? tr("Away") : tr("Here"));

	m_presence->setToolTip(said.join(QStringLiteral(" · ")));
	m_presence->setAccessibleName(m_presence->toolTip());
}

void Qdc::HubHeader::refreshSettings()
{
	const qreal ratio = devicePixelRatioF();

	/*
	 * Marked while there is something unsaved behind it, because the page it opens
	 * can be left with the Save still lit and nothing else would say so.
	 */
	const QColor colour = m_settingsUnsaved ? theme().accent : theme().contentMuted;

	m_settings->setIcon(glyph_icon(Glyph::Settings, colour, ratio, GLYPH));

	const QString text = m_settingsUnsaved
		? tr("Settings for this hub — unsaved changes")
		: tr("Settings for this hub");

	m_settings->setToolTip(text);
	m_settings->setAccessibleName(text);
}

void Qdc::HubHeader::refreshLock()
{
	const qreal ratio = devicePixelRatioF();

	/*
	 * Three states rather than two. A hub that is not connected has nothing to
	 * secure and nothing failing to secure it, so it gets neither the green nor the
	 * red - the same judgement the tree row makes by drawing no padlock at all; see
	 * HubsView::refreshItem(). Kept visible and muted rather than hidden, because
	 * this sits in a fixed strip and taking it away would shuffle the buttons beside
	 * it sideways every time a hub connected or dropped.
	 */
	const bool live = m_status == NetStatus::Connected
		|| m_status == NetStatus::TlsConnected;

	const QColor colour = !live
		? theme().contentMuted
		: (m_secure ? theme().success : theme().danger);

	QPixmap pixmap(QSize(GLYPH, GLYPH) * ratio);
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(Qt::transparent);

	QPainter painter(&pixmap);
	paint_glyph(painter, (live && m_secure) ? Glyph::Lock : Glyph::LockOpen,
	            QRectF(0, 0, GLYPH, GLYPH), colour);
	painter.end();

	m_lock->setPixmap(pixmap);

	/*
	 * The glyph differs as well as the colour, so which of the three this is does
	 * not rest on telling red from green.
	 */
	const QString text = !live
		? net_status_text(m_status)
		: (m_secure
			? tr("The connection to this hub is encrypted")
			: tr("This hub is not using TLS. Everything sent to it, including "
			     "your password, travels in the clear."));

	m_lock->setToolTip(text.isEmpty() ? tr("Not connected") : text);
	m_lock->setAccessibleName(m_lock->toolTip());
}

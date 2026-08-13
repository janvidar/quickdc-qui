/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "hubpane.h"

#include "composer.h"
#include "connectionbar.h"
#include "filelistview.h"
#include "glyphs.h"
#include "hubheader.h"
#include "richtext.h"
#include "theme.h"
#include "usermenu.h"
#include "usermodel.h"
#include "widgets.h"

#include <core/configbridge.h>
#include <core/hubbridge.h>

#include <QClipboard>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMetaEnum>
#include <QListView>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

/** How wide the user list starts, and the least it can be dragged to. */
constexpr int USER_LIST_WIDTH = 190;
constexpr int USER_LIST_MINIMUM = 120;

/** The search glyph in the user list's heading. */
constexpr int FILTER_GLYPH = 14;

/*
 * The identity keys, spelled as the core spells them.
 *
 * Per hub they live in session.conf under a group named by the hub's address, and
 * globally in quickdc.conf under [User] - the same four names in both, which is what
 * makes one a fallback for the other.
 */
const QString USER_GROUP = QStringLiteral("User");
const QString NICKNAME_KEY = QStringLiteral("Nickname");
const QString DESCRIPTION_KEY = QStringLiteral("Description");
const QString EMAIL_KEY = QStringLiteral("E-mail");
const QString PASSWORD_KEY = QStringLiteral("Password");

/**
 * Why the composer is not sendable, or empty when it is.
 *
 * Both halves matter. Reaching the hub is not the same as being logged in to it -
 * a hub that accepts the connection and then says nothing leaves the socket
 * connected for as long as one cares to wait - and a message sent in between is
 * refused by the core, which is no way to find out.
 */
QString status_reason(Qdc::NetStatus net, Qdc::HubStatus hub)
{
	switch (net)
	{
		case Qdc::NetStatus::Connected:
		case Qdc::NetStatus::TlsConnected:
			break;

		case Qdc::NetStatus::Lookup:
		case Qdc::NetStatus::Connecting:
			return QObject::tr("Connecting…");

		default:
			return QObject::tr("Not connected");
	}

	switch (hub)
	{
		case Qdc::HubStatus::LoggedIn:
			return QString();

		case Qdc::HubStatus::Password:
			return QObject::tr("Waiting for the password…");

		case Qdc::HubStatus::None:
		case Qdc::HubStatus::Login:
			return QObject::tr("Signing in…");

		default:
			return QObject::tr("The hub would not let us in");
	}
}

}

Qdc::HubPane::HubPane(HubBridge* hub, ConfigBridge* config, QWidget* parent)
	: ChatPane(hub, parent)
	, m_config(config)
	, m_users(new UserModel(this))
	, m_visibleUsers(new QSortFilterProxyModel(this))
	, m_userList(new QListView(this))
	, m_info(new QLabel(this))
{
	header()->setUserListVisible(true);
	header()->setFavouriteVisible(true);

	/*
	 * Never sorted, only filtered. The model keeps itself in order - operators
	 * first, then by name - and a proxy asked to sort would take that over and
	 * lose the first half of it.
	 */
	m_visibleUsers->setSourceModel(m_users);
	m_visibleUsers->setFilterCaseSensitivity(Qt::CaseInsensitive);

	m_userList->setObjectName(QStringLiteral("userList"));
	m_userList->setModel(m_visibleUsers);
	m_userList->setUniformItemSizes(true);
	m_userList->setFrameShape(QFrame::NoFrame);
	m_userList->setSelectionMode(QAbstractItemView::SingleSelection);
	m_userList->setAccessibleName(tr("Users"));
	m_userList->setContextMenuPolicy(Qt::CustomContextMenu);

	connect(m_userList, &QListView::customContextMenuRequested,
	        this, &HubPane::showUserListMenu);

	if (m_config)
	{
		connect(m_config, &ConfigBridge::saveFailed, this, [this](ConfigStore store) {
			if (store == ConfigStore::Session)
				m_sessionStatus->setText(tr("The session file could not be written."));
		});
	}

	/*
	 * The user list is the context sidebar from the layout. It belongs to this pane
	 * rather than to the window, because it is the only view that has one and its
	 * contents are hub specific.
	 */
	addSidebar(buildUserSidebar(), USER_LIST_WIDTH);

	m_info->setWordWrap(true);
	m_info->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);

	/* Inset by the page below rather than by itself, so the form lines up with it. */
	m_info->setMargin(0);

	/* What the hub is. What we are on it is behind the gears; see below. */
	QWidget* info = new QWidget(this);

	QVBoxLayout* stacked = new QVBoxLayout(info);
	stacked->setContentsMargins(12, 12, 12, 12);
	stacked->setSpacing(12);
	stacked->addWidget(m_info);
	stacked->addStretch(1);

	QScrollArea* infoPage = new QScrollArea(this);
	infoPage->setWidget(info);
	infoPage->setWidgetResizable(true);
	infoPage->setFrameShape(QFrame::NoFrame);

	tabs()->addTab(infoPage, tr("Info"));

	/*
	 * Built now rather than when it is first asked for, and kept whether it is on
	 * the tab strip or not: loadSession() fills it during construction, and anything
	 * typed into it has to survive the tab coming and going.
	 */
	m_sessionPage = buildSessionPage();
	m_sessionPage->setParent(this);
	m_sessionPage->hide();

	/*
	 * In the header beside the padlock and the user count rather than in the tab
	 * strip: it is one of the things that can be done to this hub, which is what
	 * that row of buttons is, and a corner widget sat outside the run of them.
	 */
	header()->setSettingsVisible(true);
	connect(header(), &HubHeader::settingsToggled, this, &HubPane::showSessionPage);

	refreshSessionGears();

	/* The whole panel, or the heading and its filter would stay behind. */
	connect(header(), &HubHeader::userListToggled,
	        m_userSidebar, &QWidget::setVisible);
	header()->setUserListShown(true);

	connect(header(), &HubHeader::favouriteToggled, this, [this](bool favourite) {
		emit favouriteToggled(this->hub() ? this->hub()->url() : QString(),
		                      favourite);
	});

	connect(m_users, &UserModel::countChanged, this, [this](int count) {
		header()->setUserCount(count);

		/* The mention list follows whoever is actually here, filter or no. */
		composer()->setNicknames(m_users->nicknames());
		refreshUserHeading();
		refreshInfo();
	});

	connect(m_userList, &QListView::activated, this, [this](const QModelIndex& index) {
		const UserInfo user = userAt(index);
		if (!user.id.isEmpty()) emit directMessageRequested(user);
	});

	if (!hub)
	{
		composer()->setSendable(false, tr("There is no hub."));
		refreshHeaderIdentity();
		refreshInfo();
		return;
	}

	/*
	 * The hub-wide state is subscribed to here rather than routed in from above,
	 * because this pane is the only thing that wants any of it.
	 */
	connect(hub, &HubBridge::userJoined, m_users, &UserModel::noteNickname);
	connect(hub, &HubBridge::userUpdated, m_users, &UserModel::upsert);
	connect(hub, &HubBridge::usersCleared, m_users, &UserModel::clear);

	connect(hub, &HubBridge::userLeft, this,
	        [this](const UserInfo& user, const QString&, bool) {
		m_users->remove(user.id);
	});

	connect(hub, &HubBridge::hubNameChanged, this, [this] {
		refreshHeaderIdentity();
		refreshInfo();
	});

	connect(hub, &HubBridge::hubDescriptionChanged, this, [this] {
		refreshHeaderIdentity();
		refreshInfo();
	});

	connect(hub, &HubBridge::secureChanged, this, [this](bool secure) {
		header()->setSecure(secure);
		refreshInfo();
	});

	connect(hub, &HubBridge::netStatusChanged, this, &HubPane::applyNetStatus);

	connect(hub, &HubBridge::reconnectScheduled,
	        connectionBar(), &ConnectionBar::setReconnectIn);

	/*
	 * The band offers to stop waiting; this is what it means. connectToHub() is
	 * the same call that opened the hub, and the core drops the attempt it had
	 * pending when a connection is asked for.
	 */
	connect(connectionBar(), &ConnectionBar::reconnectRequested, this, [hub] {
		hub->connectToHub();
	});

	connect(hub, &HubBridge::hubStatusChanged, this, [this](HubStatus status) {
		/* Logging in is half of whether a message can be sent; see status_reason. */
		m_hubStatus = status;
		applySendable();
		applyBand();
		refreshInfo();
	});

	header()->setSecure(hub->isSecure());
	m_hubStatus = hub->hubStatus();
	applyNetStatus(hub->netStatus());
	refreshHeaderIdentity();
	refreshInfo();
	loadSession();
}

QWidget* Qdc::HubPane::buildUserSidebar()
{
	m_userSidebar = new QWidget(this);
	m_userSidebar->setObjectName(QStringLiteral("userSidebar"));

	/*
	 * The rule down the left belongs to the panel now that the list is not the whole
	 * of it, and a plain QWidget draws neither a stylesheet background nor a border
	 * without being told to; ConnectionBar says the same two things for the same
	 * reason.
	 */
	m_userSidebar->setAttribute(Qt::WA_StyledBackground, true);
	m_userSidebar->setAutoFillBackground(true);
	m_userSidebar->setMinimumWidth(USER_LIST_MINIMUM);

	m_userHeading = new QLabel(m_userSidebar);
	m_userHeading->setObjectName(QStringLiteral("mutedText"));

	m_userFilterButton = new QToolButton(m_userSidebar);
	m_userFilterButton->setObjectName(QStringLiteral("userFilterButton"));
	m_userFilterButton->setCheckable(true);
	m_userFilterButton->setAutoRaise(true);
	m_userFilterButton->setCursor(Qt::PointingHandCursor);
	m_userFilterButton->setToolTip(tr("Find somebody in this list"));
	m_userFilterButton->setAccessibleName(m_userFilterButton->toolTip());

	m_userFilterButton->setIcon(glyph_icon(Glyph::Search, theme().contentMuted,
	                                       devicePixelRatioF(), FILTER_GLYPH));

	m_userFilter = new QLineEdit(m_userSidebar);
	m_userFilter->setObjectName(QStringLiteral("userFilter"));
	m_userFilter->setPlaceholderText(tr("Filter by name"));
	m_userFilter->setClearButtonEnabled(true);
	m_userFilter->hide();

	/* Not an event filter: one object, and it cannot fire while the field is away. */
	QShortcut* escape = new QShortcut(QKeySequence(Qt::Key_Escape), m_userFilter);
	escape->setContext(Qt::WidgetShortcut);

	connect(escape, &QShortcut::activated, this, [this] {
		m_userFilterButton->setChecked(false);
	});

	connect(m_userFilterButton, &QToolButton::toggled,
	        this, &HubPane::setUserFilterShown);

	connect(m_userFilter, &QLineEdit::textChanged, this, &HubPane::applyUserFilter);

	QHBoxLayout* heading = new QHBoxLayout;
	heading->setContentsMargins(10, 6, 4, 2);
	heading->setSpacing(4);
	heading->addWidget(m_userHeading, 1);
	heading->addWidget(m_userFilterButton);

	QVBoxLayout* stacked = new QVBoxLayout(m_userSidebar);
	stacked->setContentsMargins(0, 0, 0, 0);
	stacked->setSpacing(0);
	stacked->addLayout(heading);
	stacked->addWidget(m_userFilter);
	stacked->addWidget(m_userList, 1);

	refreshUserHeading();

	return m_userSidebar;
}

void Qdc::HubPane::setUserFilterShown(bool shown)
{
	m_userFilter->setVisible(shown);

	if (shown)
	{
		m_userFilter->setFocus(Qt::ShortcutFocusReason);
		return;
	}

	/*
	 * Cleared on the way out rather than remembered. A filter still narrowing a list
	 * nobody can see is the version of this that reads as everybody having left, and
	 * a hub reconnecting would repopulate the list into it with nothing to say why.
	 */
	m_userFilter->clear();
	m_userList->setFocus(Qt::ShortcutFocusReason);
}

void Qdc::HubPane::applyUserFilter(const QString& text)
{
	m_visibleUsers->setFilterFixedString(text.trimmed());
	refreshUserHeading();
}

void Qdc::HubPane::refreshUserHeading()
{
	if (!m_userHeading) return;

	const int total = m_users->count();

	/*
	 * The heading is where a filter says what it did. The count beside the header's
	 * people button keeps meaning how many are on the hub, which is a different
	 * question and not one a filter should be able to change the answer to.
	 */
	if (m_userFilter && !m_userFilter->text().trimmed().isEmpty())
	{
		m_userHeading->setText(tr("%1 of %2")
			.arg(m_visibleUsers->rowCount()).arg(total));
		return;
	}

	m_userHeading->setText(tr("Users"));
}

Qdc::UserInfo Qdc::HubPane::userAt(const QModelIndex& index) const
{
	return index.data(UserModel::UserRole).value<UserInfo>();
}

void Qdc::HubPane::refreshHeaderIdentity()
{
	if (!hub())
	{
		header()->setTitle(tr("No hub"));
		header()->setSubtitle(QString());
		return;
	}

	header()->setHubIdentity(hub()->hubName(), hub()->hubDescription(),
	                         hub()->url());
}

/*
 * The name as the hub gave it, and the address as it was opened with.
 *
 * Not reduced here, and neither is DmPane's. This answers "what is this conversation
 * called", which is asked by things that draw and by things that do not: the fallback is
 * a hub's URL and a conversation's is a session id, and either of those is somewhere else
 * a key. Whoever draws one reduces it - the tree row and the header both do, and the
 * reduction is idempotent, so the one that then passes it on again costs nothing.
 */
QString Qdc::HubPane::paneTitle() const
{
	const QString name = hub() ? hub()->hubName() : QString();
	if (!name.isEmpty()) return name;

	return hub() ? hub()->url() : tr("No hub");
}

Qdc::UserModel* Qdc::HubPane::users() const
{
	return m_users;
}

void Qdc::HubPane::setFavourite(bool favourite)
{
	header()->setFavourite(favourite);
}

void Qdc::HubPane::sendMessage(const QString& message, bool richText)
{
	if (hub()) hub()->sendChat(message, richText);
}

void Qdc::HubPane::applyNetStatus(NetStatus status)
{
	m_netStatus = status;
	header()->setNetStatus(status);
	applySendable();
	applyBand();
	refreshInfo();
}

void Qdc::HubPane::applyBand()
{
	connectionBar()->setStatus(m_netStatus, m_hubStatus);
}

void Qdc::HubPane::applySendable()
{
	if (!hub()) return;

	const QString reason = status_reason(m_netStatus, m_hubStatus);
	composer()->setSendable(reason.isEmpty(), reason);
}

QWidget* Qdc::HubPane::buildSessionPage()
{
	QWidget* page = new QWidget(this);

	QLabel* heading = new QLabel(tr("Who you are on this hub"), page);

	QFont bolder = heading->font();
	bolder.setBold(true);
	heading->setFont(bolder);

	/*
	 * Named, so that the field for a key can be found without relying on the order
	 * they were built in - which is how a test reaches them, the members being this
	 * pane's own business.
	 */
	m_nickname = new QLineEdit(page);
	m_nickname->setObjectName(QStringLiteral("sessionNickname"));
	m_nickname->setMaxLength(63);

	m_description = new QLineEdit(page);
	m_description->setObjectName(QStringLiteral("sessionDescription"));
	m_description->setMaxLength(255);

	m_email = new QLineEdit(page);
	m_email->setObjectName(QStringLiteral("sessionEmail"));
	m_email->setMaxLength(127);

	m_password = new QLineEdit(page);
	m_password->setObjectName(QStringLiteral("sessionPassword"));
	m_password->setEchoMode(QLineEdit::Password);
	m_password->setMaxLength(127);

	QFormLayout* form = new QFormLayout;
	form->setContentsMargins(0, 0, 0, 0);
	form->addRow(heading);
	form->addRow(tr("Nickname"), m_nickname);
	form->addRow(tr("Description"), m_description);
	form->addRow(tr("E-mail"), m_email);
	form->addRow(tr("Password"), m_password);

	/*
	 * Said rather than left to be worked out, because an empty field here means
	 * something: the hub gets whatever Settings says, and clearing one is how a hub
	 * is handed back to the general identity rather than given an empty name.
	 */
	form->addRow(makeNote(tr(
		"Left empty, a field falls back to the identity in Settings — which is what "
		"the greyed text in each one shows. A password is kept in the clear in "
		"session.conf, as the rest of this is."), page));

	m_sessionForm = new QWidget(page);
	m_sessionForm->setLayout(form);

	/* The object name is the stylesheet's, so a test finds this by its text. */
	m_saveSession = new QPushButton(tr("Save"), page);
	m_saveSession->setObjectName(QStringLiteral("primaryButton"));
	m_saveSession->setEnabled(false);

	/*
	 * The explicit discard, so that leaving the page with something typed in it does
	 * not have to be interrupted by a question. Nothing is lost by walking away: the
	 * page is never loaded again on its own, so what was typed is still here.
	 */
	m_revertSession = new QPushButton(tr("Revert"), page);
	m_revertSession->setEnabled(false);

	m_sessionStatus = new QLabel(page);
	m_sessionStatus->setObjectName(QStringLiteral("mutedText"));
	m_sessionStatus->setWordWrap(true);

	QHBoxLayout* buttons = new QHBoxLayout;
	buttons->setContentsMargins(0, 0, 0, 0);
	buttons->addWidget(m_sessionStatus, 1);
	buttons->addWidget(m_revertSession);
	buttons->addWidget(m_saveSession);

	QVBoxLayout* stacked = new QVBoxLayout(page);
	stacked->setContentsMargins(16, 16, 16, 16);
	stacked->setSpacing(12);
	stacked->addWidget(m_sessionForm);
	stacked->addStretch(1);
	stacked->addLayout(buttons);

	for (QLineEdit* field : {m_nickname, m_description, m_email, m_password})
		connect(field, &QLineEdit::textEdited, this, &HubPane::markSessionEdited);

	connect(m_saveSession, &QPushButton::clicked, this, &HubPane::saveSession);
	connect(m_revertSession, &QPushButton::clicked, this, &HubPane::loadSession);

	if (!m_config || !hub())
	{
		/*
		 * The form rather than the page, so that the status label below it stays
		 * readable and says why there is nothing to fill in.
		 */
		m_sessionForm->setEnabled(false);
		m_saveSession->setEnabled(false);
		m_sessionStatus->setText(tr("The configuration is not available."));
	}

	QScrollArea* scroll = new QScrollArea(this);
	scroll->setWidget(page);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);

	return scroll;
}

void Qdc::HubPane::showSessionPage(bool shown)
{
	if (!m_sessionPage) return;

	if (shown)
	{
		tabs()->setCurrentIndex(tabs()->addTab(m_sessionPage, tr("Settings")));
		return;
	}

	const int index = tabs()->indexOf(m_sessionPage);
	if (index < 0) return;

	/*
	 * Said rather than left to the tab widget, which lands on whichever tab happens
	 * to be beside the one that went. The page keeps its parent when the tab is
	 * removed, so nothing typed into it is lost - which is what makes leaving it
	 * safe without a question first.
	 */
	tabs()->setCurrentIndex(0);
	tabs()->removeTab(index);
}

void Qdc::HubPane::refreshSessionGears()
{
	header()->setSettingsUnsaved(m_saveSession && m_saveSession->isEnabled());
}

void Qdc::HubPane::loadSession()
{
	if (!m_config || !hub() || !m_nickname) return;

	m_loadingSession = true;

	/* Canonical, so a keyprint in the address does not split the settings in two. */
	const QString group = HubBridge::settingsUrl(hub()->url());

	/*
	 * A group at a time would be better, as the settings page does it, but these are
	 * two groups rather than one - the hub's own and the global one it falls back to -
	 * and the placeholders need both.
	 */
	const QVariantHash mine = m_config->readGroup(ConfigStore::Session, group, {
		{NICKNAME_KEY, QString()},
		{DESCRIPTION_KEY, QString()},
		{EMAIL_KEY, QString()},
		{PASSWORD_KEY, QString()}
	});

	const QVariantHash general = m_config->readGroup(ConfigStore::Application,
	                                                 USER_GROUP, {
		{NICKNAME_KEY, QString()},
		{DESCRIPTION_KEY, QString()},
		{EMAIL_KEY, QString()}
	});

	m_nickname->setText(mine.value(NICKNAME_KEY).toString());
	m_description->setText(mine.value(DESCRIPTION_KEY).toString());
	m_email->setText(mine.value(EMAIL_KEY).toString());
	m_password->setText(mine.value(PASSWORD_KEY).toString());

	m_nickname->setPlaceholderText(general.value(NICKNAME_KEY).toString());
	m_description->setPlaceholderText(general.value(DESCRIPTION_KEY).toString());
	m_email->setPlaceholderText(general.value(EMAIL_KEY).toString());
	m_password->setPlaceholderText(tr("none"));

	m_loadingSession = false;

	m_saveSession->setEnabled(false);
	m_revertSession->setEnabled(false);
	m_sessionStatus->clear();
	refreshSessionGears();
}

void Qdc::HubPane::markSessionEdited()
{
	if (m_loadingSession || !m_saveSession) return;

	m_saveSession->setEnabled(true);
	m_revertSession->setEnabled(true);
	m_sessionStatus->setText(tr("Unsaved changes."));
	refreshSessionGears();
}

void Qdc::HubPane::saveSession()
{
	if (!m_config || !hub()) return;

	/* Canonical, so a keyprint in the address does not split the settings in two. */
	const QString group = HubBridge::settingsUrl(hub()->url());

	const struct { const QString& key; QLineEdit* field; } fields[] = {
		{ NICKNAME_KEY, m_nickname },
		{ DESCRIPTION_KEY, m_description },
		{ EMAIL_KEY, m_email },
		{ PASSWORD_KEY, m_password }
	};

	for (const auto& entry : fields)
	{
		const QString value = entry.field->text().trimmed();

		/*
		 * Removed rather than stored empty, and the difference is the whole point of
		 * the note under the form: the core falls back to the global identity only
		 * for a key that is absent, and reads a key that is present and empty as an
		 * empty name.
		 */
		if (value.isEmpty())
			m_config->remove(ConfigStore::Session, group, entry.key);
		else
			m_config->setString(ConfigStore::Session, group, entry.key, value);
	}

	/*
	 * Said before the file has been written, because the write happens a moment
	 * later on the core's own thread and waiting for its answer is the freeze this
	 * form used to be. A write that fails says so through saveFailed(), which is
	 * connected where the form is built.
	 */
	m_config->save(ConfigStore::Session);

	m_saveSession->setEnabled(false);
	m_revertSession->setEnabled(false);
	m_sessionStatus->setText(tr("Saved."));
	refreshSessionGears();

	/*
	 * A session reads its identity when it is built and never again, so this changes
	 * nothing about the connection that is up. Offered rather than done, because
	 * reconnecting drops out of the hub and anybody in the middle of a conversation
	 * should be the one to decide that.
	 */
	const bool live = hub()->netStatus() == NetStatus::Connected
		|| hub()->netStatus() == NetStatus::TlsConnected;

	if (!live) return;

	const auto answer = QMessageBox::question(this, tr("Reconnect"),
		tr("Saved. This hub already knows you by the old identity, and will keep "
		   "using it until the next connection.\n\nReconnect now?"),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

	if (answer != QMessageBox::Yes) return;

	hub()->disconnectFromHub();
	hub()->connectToHub();
}

void Qdc::HubPane::showUserListMenu(const QPoint& where)
{
	const QModelIndex index = m_userList->indexAt(where);
	if (!index.isValid()) return;

	const UserInfo user = userAt(index);
	if (user.nick.isEmpty() && user.id.isEmpty()) return;

	showUserMenu(user, m_userList->viewport()->mapToGlobal(where));
}

void Qdc::HubPane::showUserMenu(const UserInfo& user, const QPoint& globalPos)
{
	/*
	 * A name in the feed is who they were when they said it, so it is looked up
	 * again: on ADC the id in it lasts only as long as they stay on the hub. A row
	 * of the list resolves to itself, which is why both callers come through here.
	 */
	const UserInfo here = m_users->resolve(user);

	UserMenuOptions options;

	if (here.id.isEmpty())
	{
		/*
		 * Reduced before the sentence is chosen rather than after: a name that is
		 * nothing but invisible characters leaves nothing to draw, and "They" is the
		 * sentence for a name there is nothing to say - not "%1" with a hole in it.
		 */
		const QString who = RichText::displayText(user.nick);

		options.unavailable = who.isEmpty()
			? tr("They are no longer on this hub")
			: tr("%1 is no longer on this hub").arg(who);
	}

	const UserInfo& about = here.id.isEmpty() ? user : here;

	switch (Qdc::showUserMenu(this, about, globalPos, options))
	{
		case UserChoice::Talk:   emit directMessageRequested(about); break;
		case UserChoice::Browse: emit filesRequested(about); break;
		case UserChoice::None:   break;
	}
}

void Qdc::HubPane::refreshInfo()
{
	if (!hub())
	{
		m_info->setText(tr("There is no hub."));
		return;
	}

	const QMetaEnum net = QMetaEnum::fromType<NetStatus>();
	const QMetaEnum state = QMetaEnum::fromType<HubStatus>();

	QStringList rows;

	/*
	 * Reduced and then escaped, in that order: the bound is on the name the hub chose
	 * and not on whatever it grows into once the ampersands are entities. Every row here
	 * is one of our own labels beside a value, so a name carrying an override would read
	 * as the label of the row below it - which is exactly what displayText() is for.
	 */
	const auto shown = [](const QString& text) {
		return RichText::displayText(text).toHtmlEscaped();
	};

	rows << tr("<b>Address</b><br>%1").arg(shown(hub()->url()));

	if (!hub()->hubName().isEmpty())
		rows << tr("<b>Name</b><br>%1").arg(shown(hub()->hubName()));

	if (!hub()->hubDescription().isEmpty())
		rows << tr("<b>Description</b><br>%1").arg(shown(hub()->hubDescription()));

	rows << tr("<b>Users</b><br>%1").arg(m_users->count());

	rows << tr("<b>Encryption</b><br>%1").arg(hub()->isSecure()
		? tr("TLS") : tr("None"));

	/*
	 * The enumerator names, by way of Q_ENUM_NS - which is the reason the bridge
	 * mirrors the core's enums rather than passing the raw values through.
	 */
	rows << tr("<b>Network</b><br>%1")
		.arg(QString::fromUtf8(net.valueToKey(static_cast<int>(hub()->netStatus()))));

	rows << tr("<b>Session</b><br>%1")
		.arg(QString::fromUtf8(state.valueToKey(static_cast<int>(hub()->hubStatus()))));

	quint64 shared = 0;
	for (int n = 0; n < m_users->count(); ++n) shared += m_users->at(n).sharedBytes;

	if (shared > 0)
	{
		rows << tr("<b>Shared here</b><br>%1")
			.arg(QLocale().formattedDataSize(static_cast<qint64>(shared)));
	}

	m_info->setText(rows.join(QStringLiteral("<br><br>")));
}

Qdc::DmPane::DmPane(HubBridge* hub, const UserInfo& user,
                    FilelistBridge* filelists, QWidget* parent)
	: ChatPane(hub, parent)
	, m_user(user)
	, m_files(new FilelistView(filelists, hub ? hub->url() : QString(),
	                           user.sourceId, this))
{
	/* No star and no user list: a conversation with one person has neither. */
	header()->setFavouriteVisible(false);
	header()->setUserListVisible(false);
	header()->setSettingsVisible(false);

	/*
	 * The figure takes the star's place, so a conversation is not missing the one
	 * thing every hub has at the start of its strip - and it says the same about
	 * this person as their row in the user list does.
	 */
	header()->setPresenceVisible(true);

	tabs()->addTab(m_files, tr("Files"));

	connect(m_files, &FilelistView::fileActivated,
	        this, &DmPane::downloadRequested);

	setUser(user);

	if (!hub)
	{
		composer()->setSendable(false, tr("There is no hub."));
		return;
	}

	/*
	 * The padlock says the same thing here as on the hub itself: a private message
	 * travels through the hub and is exactly as encrypted as the hub is.
	 */
	connect(hub, &HubBridge::netStatusChanged, this, [this](NetStatus status) {
		m_netStatus = status;
		header()->setNetStatus(status);
		applySendable();
		applyBand();
	});

	connect(hub, &HubBridge::secureChanged, header(), &HubHeader::setSecure);

	connect(hub, &HubBridge::hubStatusChanged, this, [this](HubStatus status) {
		m_hubStatus = status;
		applySendable();
		applyBand();
	});

	/*
	 * The same band as the hub's own pane gets: a private message goes through the
	 * hub, so a conversation with one person is as unusable as the hub is.
	 */
	connect(hub, &HubBridge::reconnectScheduled,
	        connectionBar(), &ConnectionBar::setReconnectIn);

	/*
	 * The band offers to stop waiting; this is what it means. connectToHub() is
	 * the same call that opened the hub, and the core drops the attempt it had
	 * pending when a connection is asked for.
	 */
	connect(connectionBar(), &ConnectionBar::reconnectRequested, this, [hub] {
		hub->connectToHub();
	});

	/*
	 * Who we are talking to, followed for as long as this is open. A message can only
	 * be sent to somebody the hub will relay it to, so a conversation with somebody
	 * who has gone is closed for writing until they come back.
	 */
	connect(hub, &HubBridge::userLeft, this,
	        [this](const UserInfo& user, const QString&, bool) {
		if (isWith(user)) setPresent(false, m_user.nick);
	});

	connect(hub, &HubBridge::userUpdated, this, [this](const UserInfo& user) {
		if (!isWith(user)) return;

		const QString was = m_user.id;
		const QString wasCalled = m_user.nick;

		setUser(user);

		/*
		 * A new session id for the same client ID is them having come back, and the
		 * conversation is filed by that id - so whoever holds it has to be told.
		 */
		if (!was.isEmpty() && !user.id.isEmpty() && user.id != was)
			emit contextChanged(was, user.id);

		setPresent(true, wasCalled);
	});

	/*
	 * The session ended, so nothing is known about anybody any more. Said as an
	 * absence rather than left alone: the hub coming back re-announces everyone, and
	 * until it has there is no one to write to.
	 */
	connect(hub, &HubBridge::usersCleared, this, [this] {
		setPresent(false, m_user.nick);
	});

	m_netStatus = hub->netStatus();
	m_hubStatus = hub->hubStatus();
	header()->setSecure(hub->isSecure());
	header()->setNetStatus(m_netStatus);
	applySendable();
	applyBand();

	/* Mentioning the other party is the only completion that makes sense here. */
	composer()->setNicknames({m_user.nick});
}

void Qdc::DmPane::applyBand()
{
	QString absent;

	if (!m_present)
	{
		/*
		 * The band writes this name into a sentence of its own and has nothing but the
		 * string to go on, so it is reduced here rather than there. An empty one is also
		 * how the band is told there is nobody absent, though, and a name of nothing but
		 * invisible characters reduces to none at all - so the marker stands in for one
		 * of those, and a doctored name is not a way of stopping the band from speaking.
		 */
		absent = RichText::displayText(m_user.nick);

		if (absent.isEmpty() && !m_user.nick.isEmpty())
			absent = QChar(QChar::ReplacementCharacter);
	}

	connectionBar()->setAbsent(absent);
	connectionBar()->setStatus(m_netStatus, m_hubStatus);
}

void Qdc::DmPane::applySendable()
{
	QString reason = status_reason(m_netStatus, m_hubStatus);

	/*
	 * The hub being usable is not enough here: a private message is relayed to one
	 * person, and a hub cannot relay it to somebody who is not on it. Said after the
	 * connection reasons, which are the more fundamental ones - there is no point
	 * saying who has left while the hub itself is unreachable.
	 */
	if (reason.isEmpty() && !m_present)
	{
		const QString who = RichText::displayText(m_user.nick);

		reason = who.isEmpty()
			? tr("They are no longer on this hub")
			: tr("%1 is no longer on this hub").arg(who);
	}

	composer()->setSendable(reason.isEmpty(), reason);
}

bool Qdc::DmPane::isWith(const UserInfo& user) const
{
	if (!m_user.sourceId.isEmpty() && !user.sourceId.isEmpty())
		return user.sourceId == m_user.sourceId;

	return !m_user.id.isEmpty() && user.id == m_user.id;
}

void Qdc::DmPane::applyPresence()
{
	/*
	 * The user list's own reading of somebody, and the same figure it draws. Off the
	 * hub counts as away here: the shape says "not reading this", which is true
	 * either way, and the band above the conversation is what says which of the two
	 * it is.
	 */
	header()->setPresence(m_user.away || !m_present, m_user.op);
}

void Qdc::DmPane::setPresent(bool present, const QString& nickname)
{
	const bool changed = m_present != present;
	m_present = present;

	applySendable();
	applyBand();
	applyPresence();

	if (!changed) return;

	/*
	 * Written into the conversation as well as shown across the top, because the band
	 * says what is true now and this is a record of when it changed - which is what
	 * somebody scrolling back through the morning is reading.
	 */
	const QString shown = RichText::displayText(nickname);
	const QString who = shown.isEmpty() ? tr("They") : shown;

	if (!present)
	{
		postNotice(tr("%1 has left the hub.").arg(who));
		return;
	}

	/*
	 * Whether they came back under a new name is asked of the names as they arrived,
	 * since two that differ only in what the reduction takes off are still two names.
	 */
	postNotice(m_user.nick == nickname
		? tr("%1 is back.").arg(who)
		: tr("%1 is back, now called %2.")
			.arg(who, RichText::displayText(m_user.nick)));
}

QString Qdc::DmPane::paneTitle() const
{
	return m_user.nick.isEmpty() ? m_user.id : m_user.nick;
}

QString Qdc::DmPane::context() const
{
	return m_user.id;
}

QString Qdc::DmPane::peerId() const
{
	return m_user.sourceId.isEmpty() ? m_user.id : m_user.sourceId;
}

void Qdc::DmPane::restoreOffline()
{
	/*
	 * The field rather than setPresent(), which writes the change into the
	 * conversation - and there is no change here, only a conversation being put
	 * back the way it was left.
	 */
	m_present = false;

	applySendable();
	applyBand();
}

void Qdc::DmPane::showFiles()
{
	/* By widget rather than by index: the base class owns the tabs before this one. */
	const int index = tabs()->indexOf(m_files);
	if (index >= 0) tabs()->setCurrentIndex(index);
}

const Qdc::UserInfo& Qdc::DmPane::peer() const
{
	return m_user;
}

void Qdc::DmPane::showUserMenu(const UserInfo& user, const QPoint& globalPos)
{
	/*
	 * No offer to open a conversation: this is the conversation. Browsing lands on
	 * the tab beside this one, which is where it would have landed anyway.
	 */
	UserMenuOptions options;
	options.offerTalk = false;

	if (!m_present)
	{
		const QString who = RichText::displayText(m_user.nick);

		options.unavailable = who.isEmpty()
			? tr("They are no longer on this hub")
			: tr("%1 is no longer on this hub").arg(who);
	}

	/*
	 * A line of our own shows our own name, and that is not the other party - so
	 * what the message carried is used as it stands rather than taken for them.
	 */
	const UserInfo& about = isWith(user) ? m_user : user;

	if (Qdc::showUserMenu(this, about, globalPos, options) == UserChoice::Browse)
		showFiles();
}

void Qdc::DmPane::setUser(const UserInfo& user)
{
	/*
	 * A record about somebody else is ignored rather than applied. Which records are
	 * about this conversation is isWith()'s judgement, and it is deliberately not
	 * "the same session id": somebody who leaves and comes back is the same person
	 * under a new session, and often under a new nickname, which is precisely the
	 * case this conversation has to follow rather than lose.
	 */
	if (!m_user.id.isEmpty() && !user.id.isEmpty() && !isWith(user)) return;

	m_user = user;

	header()->setTitle(paneTitle());
	header()->setSubtitle(user.description);
	applyPresence();

	m_files->setSource(hub() ? hub()->url() : QString(), user.sourceId);
	composer()->setNicknames({m_user.nick});
}

void Qdc::DmPane::sendMessage(const QString& message, bool richText)
{
	if (hub()) hub()->sendPrivateChat(m_user.id, message, richText);
}

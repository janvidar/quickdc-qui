/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "mainwindow.h"

#include "cornermask.h"
#include "homeview.h"
#include "hubsview.h"
#include "nativewindow.h"
#include "navigationrail.h"
#include "searchview.h"
#include "settingsview.h"
#include "transfersview.h"

#include <core/completed.h>
#include <core/configbridge.h>
#include <core/corethread.h>
#include <core/favourites.h>
#include <core/filelistbridge.h>
#include <core/hubdirectory.h>
#include <core/recenthubs.h>
#include <core/searchbridge.h>
#include <core/shares.h>
#include <core/transferbridge.h>

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QShowEvent>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QTimer>

namespace {

/**
 * How far the content is held off the right-hand and bottom edges of the window.
 *
 * The frame closes on all four sides: the top bar does it above and the rail to the
 * left, and these two are the others. Matched to the large corner radius, so the gap
 * and the curve are the same size.
 *
 * The bottom used to be a status bar, which said "Ready." and nothing else for the
 * life of the window. A plain gap says the same amount and takes less room.
 */
constexpr int CONTENT_INSET = 8;

}

Qdc::MainWindow::MainWindow(CoreThread* thread, QWidget* parent)
	: QMainWindow(parent)
	, m_thread(thread)
	, m_config(new ConfigBridge(thread, this))
	, m_transfers(new TransferBridge(thread, this))
	, m_filelists(new FilelistBridge(thread, this))
	, m_searches(new SearchBridge(thread, this))
	, m_favourites(new Favourites(m_config, this))
	, m_completed(new Completed(m_config, this))
	, m_shares(new Shares(m_config, this))
	, m_recent(new RecentHubs(m_config, this))
	, m_directory(new HubDirectory(thread, m_config, QString(), this))
	, m_hubs(new HubsView(thread, m_favourites, m_filelists, m_transfers, m_config,
	                      m_completed, m_recent))
	, m_home(new HomeView(m_config, m_favourites, m_recent, m_directory,
	                      m_completed, m_transfers))
	, m_search(new SearchView(m_searches, m_transfers, m_filelists, m_config))
	, m_rail(new NavigationRail)
	, m_stack(new QStackedWidget)
{
	m_stack->setObjectName(QStringLiteral("contentStack"));
	m_stack->setAutoFillBackground(true);

	/*
	 * The frame: the window's own surface, which the top bar and the rail share and
	 * the content is inset into. No divider between rail and content - the colour
	 * change is the division, and a line through it would only break the corner.
	 */
	QWidget* central = new QWidget(this);
	central->setObjectName(QStringLiteral("appFrame"));
	central->setAutoFillBackground(true);

	QHBoxLayout* body = new QHBoxLayout;

	/*
	 * A band of frame colour above the content where the title bar is not ours to
	 * paint - the same width as the gap already left to the right and below, so
	 * the panel sits in a frame rather than starting against a window manager's
	 * decoration with nothing in between. It is also what the rounded top corners
	 * are rounded against.
	 *
	 * None on macOS, where the title bar is that band: it is painted in the frame
	 * colour and carries the window's title, so a second one below it would be the
	 * strip this was taken away to be rid of.
	 */
	const int above = titleBarIsOurs() ? 0 : CONTENT_INSET;

	body->setContentsMargins(0, above, CONTENT_INSET, CONTENT_INSET);
	body->setSpacing(0);
	body->addWidget(m_rail);
	body->addWidget(m_stack, 1);

	QVBoxLayout* layout = new QVBoxLayout(central);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addLayout(body, 1);

	/*
	 * Built after the stack and as a sibling of it, which is what puts its corners
	 * over the top. It follows the stack around by watching it for resizes rather
	 * than by being laid out, since a layout would give it space of its own.
	 */
	m_mask = new CornerMask(central);
	m_stack->installEventFilter(this);

	setCentralWidget(central);

	/*
	 * Home cannot open a hub itself - they live in another view - so the window
	 * routes the request and follows it, which is what someone pressing Connect on
	 * a favourite expects to happen.
	 */
	connect(m_home, &HomeView::hubRequested, this, [this](const QString& url) {
		if (m_hubs->openHub(url)) showView(m_views.indexOf(m_hubs));
	});

	/*
	 * A search goes out to every hub at once, but a reply arrives on the hub it came
	 * in on - so the view that shows them cannot collect them itself. This is the
	 * only place that can see both, which is why the routing is one line here.
	 */
	connect(m_hubs, &HubsView::searchResultsReceived,
	        m_search, &SearchView::addHits);

	addView(m_home);
	addView(m_hubs);
	addView(m_search);
	addView(new TransfersView(m_transfers, m_completed));

	/* Settings sits apart from the destinations, at the foot of the rail. */
	m_rail->addTrailingSpace();
	addView(new SettingsView(m_config, m_shares));

	connect(m_rail, &NavigationRail::currentIndexChanged,
	        this, &MainWindow::showView);

	showView(m_rail->currentIndex());
	resize(1100, 720);

	/*
	 * Last, and queued rather than called: opening a hub connects to it, and the
	 * window should be on screen before that starts rather than after however
	 * long the first connection takes.
	 */
	QTimer::singleShot(0, m_hubs, [this] {
		/*
		 * And onto the hubs, the way pressing Connect on a favourite goes there:
		 * somebody who left a conversation on screen expects to find it there
		 * rather than behind the home page.
		 */
		if (m_hubs->restoreSession()) showView(m_views.indexOf(m_hubs));
	});
}

Qdc::MainWindow::~MainWindow()
{
	/*
	 * The bridges have to be gone before the core thread stops, because each one
	 * unregisters something from the core as it goes and blocks until that has
	 * happened. They are children of this window, so ~QObject would destroy them
	 * anyway - but only after this body has run, and a subclass member destroyed
	 * before then could take a view with it that is still connected to one.
	 * Doing it here fixes the order rather than relying on it.
	 *
	 * The thread itself is not ours; whoever built it shuts it down.
	 */
	/*
	 * The hub view before the bridges: it owns hub bridges of its own and panes
	 * connected to them, and its destructor unwinds that in the right order.
	 */
	/*
	 * What is open, before any of it is closed. It is written as conversations
	 * come and go as well, so this is only the tidy exit - but a hub closed by
	 * the teardown below would otherwise be recorded as having been closed by
	 * somebody, and would not come back.
	 */
	if (m_hubs) m_hubs->saveSession();

	delete m_hubs;
	m_hubs = nullptr;

	/*
	 * And Home before the stores it was handed, for the same reason: it would
	 * otherwise be destroyed with the rest of the widgets, which happens after this
	 * body has run and so after everything below has gone.
	 */
	delete m_home;
	m_home = nullptr;

	/* Holds the search, transfer, filelist and config bridges; all are below. */
	delete m_search;
	m_search = nullptr;

	delete m_searches;
	m_searches = nullptr;

	/* Fetches over the core thread, so it goes while the core is still up. */
	delete m_directory;
	m_directory = nullptr;

	delete m_filelists;
	m_filelists = nullptr;

	delete m_transfers;
	m_transfers = nullptr;

	/* Every store writes through the config bridge, so they go before it. */
	delete m_shares;
	m_shares = nullptr;

	delete m_recent;
	m_recent = nullptr;

	delete m_completed;
	m_completed = nullptr;

	delete m_favourites;
	m_favourites = nullptr;

	delete m_config;
	m_config = nullptr;
}

bool Qdc::MainWindow::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_stack && m_mask
		&& (event->type() == QEvent::Resize || event->type() == QEvent::Move))
	{
		m_mask->cover(m_stack->geometry());
	}

	return QMainWindow::eventFilter(watched, event);
}

bool Qdc::MainWindow::event(QEvent* event)
{
	const bool handled = QMainWindow::event(event);

	switch (event->type())
	{
		/*
		 * Not only on the way up. Qt rewrites the window's style mask whenever the
		 * size constraints change - switching view is enough to do it - and undoes
		 * the title bar's colour on the way past, so it is put back after anything
		 * that might have disturbed it. styleTitleBar() returns straight away when
		 * there is nothing to do, which is almost always.
		 */
		case QEvent::Show:
		case QEvent::Resize:
		case QEvent::LayoutRequest:
		case QEvent::WindowStateChange:
		case QEvent::WindowActivate:

		/*
		 * And on a change of theme, which arrives as this. The title bar is painted
		 * from the frame colour by platform code that no stylesheet reaches, so it is
		 * the one surface that would otherwise keep the colours of the theme before.
		 */
		case QEvent::ApplicationPaletteChange:
			styleTitleBar(this);
			break;

		default:
			break;
	}

	return handled;
}

void Qdc::MainWindow::addView(View* view)
{
	const int index = m_rail->addDestination(view->glyph(), view->title());

	m_stack->addWidget(view);
	m_views.append(view);

	connect(view, &View::badgeCountChanged, this, [this, index](int count) {
		m_rail->setBadgeCount(index, count);
	});

	/* Whatever the view already counted while it was being built. */
	m_rail->setBadgeCount(index, view->badgeCount());
}

void Qdc::MainWindow::showView(int index)
{
	if (index < 0 || index >= m_views.size()) return;

	m_rail->setCurrentIndex(index);
	m_stack->setCurrentIndex(index);

	const QString title = m_views.at(index)->title();

	/*
	 * The title bar is where this is read now. It says the same thing the strip
	 * across the top used to, in the row the window already spends on a frame -
	 * which is a row of screen back on a display with far more width than height.
	 */
	setWindowTitle(tr("%1 — QuickDC").arg(title));
}

int Qdc::MainWindow::currentIndex() const
{
	return m_stack->currentIndex();
}

void Qdc::MainWindow::setCurrentIndex(int index)
{
	showView(index);
}

Qdc::NavigationRail* Qdc::MainWindow::rail() const
{
	return m_rail;
}

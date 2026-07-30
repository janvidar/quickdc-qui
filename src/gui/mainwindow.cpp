/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "mainwindow.h"

#include "homeview.h"
#include "hubsview.h"
#include "navigationrail.h"
#include "settingsview.h"
#include "transfersview.h"

#include <core/completed.h>
#include <core/configbridge.h>
#include <core/corethread.h>
#include <core/favourites.h>
#include <core/filelistbridge.h>
#include <core/shares.h>
#include <core/transferbridge.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>

Qdc::MainWindow::MainWindow(CoreThread* thread, QWidget* parent)
	: QMainWindow(parent)
	, m_thread(thread)
	, m_config(new ConfigBridge(thread, this))
	, m_transfers(new TransferBridge(thread, this))
	, m_filelists(new FilelistBridge(thread, this))
	, m_favourites(new Favourites(m_config, this))
	, m_completed(new Completed(m_config, this))
	, m_shares(new Shares(m_config, this))
	, m_hubs(new HubsView(thread, m_favourites, m_filelists, m_transfers, m_config))
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
	body->setContentsMargins(0, 0, 0, 0);
	body->setSpacing(0);
	body->addWidget(m_rail);
	body->addWidget(m_stack, 1);

	QVBoxLayout* layout = new QVBoxLayout(central);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(buildTopBar());
	layout->addLayout(body, 1);

	setCentralWidget(central);

	HomeView* home = new HomeView(m_config, m_favourites, m_completed, m_transfers);

	/*
	 * Home cannot open a hub itself - they live in another view - so the window
	 * routes the request and follows it, which is what someone pressing Connect on
	 * a favourite expects to happen.
	 */
	connect(home, &HomeView::hubRequested, this, [this](const QString& url) {
		if (m_hubs->openHub(url)) showView(m_views.indexOf(m_hubs));
	});

	addView(home);
	addView(m_hubs);
	addView(new TransfersView(m_transfers, m_completed));

	/* Settings sits apart from the destinations, at the foot of the rail. */
	m_rail->addTrailingSpace();
	addView(new SettingsView(m_config, m_shares));

	connect(m_rail, &NavigationRail::currentIndexChanged,
	        this, &MainWindow::showView);

	statusBar()->showMessage(m_thread && m_thread->isCoreRunning()
		? tr("Ready.")
		: tr("The QuickDC core is not running."));

	showView(m_rail->currentIndex());
	resize(1100, 720);
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
	delete m_hubs;
	m_hubs = nullptr;

	delete m_filelists;
	m_filelists = nullptr;

	delete m_transfers;
	m_transfers = nullptr;

	/* Every store writes through the config bridge, so they go before it. */
	delete m_shares;
	m_shares = nullptr;

	delete m_completed;
	m_completed = nullptr;

	delete m_favourites;
	m_favourites = nullptr;

	delete m_config;
	m_config = nullptr;
}

QWidget* Qdc::MainWindow::buildTopBar()
{
	QWidget* bar = new QWidget(this);
	bar->setObjectName(QStringLiteral("topBar"));
	bar->setAutoFillBackground(true);
	bar->setFixedHeight(42);

	QLabel* product = new QLabel(QStringLiteral("QuickDC"), bar);
	product->setObjectName(QStringLiteral("topBarTitle"));

	QFont large = product->font();
	large.setPointSize(large.pointSize() + 1);
	product->setFont(large);

	m_topBarTitle = new QLabel(bar);
	m_topBarTitle->setObjectName(QStringLiteral("topBarSubtitle"));

	QHBoxLayout* layout = new QHBoxLayout(bar);
	layout->setContentsMargins(16, 0, 16, 0);
	layout->setSpacing(10);
	layout->addWidget(product);
	layout->addWidget(m_topBarTitle);
	layout->addStretch(1);

	return bar;
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

	if (m_topBarTitle) m_topBarTitle->setText(QStringLiteral("· ") + title);

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

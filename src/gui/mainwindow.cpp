/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "mainwindow.h"

#include "hubsview.h"
#include "navigationrail.h"
#include "settingsview.h"
#include "transfersview.h"
#include "views.h"

#include <core/completed.h>
#include <core/configbridge.h>
#include <core/corethread.h>
#include <core/favourites.h>
#include <core/filelistbridge.h>
#include <core/shares.h>
#include <core/transferbridge.h>

#include <QFrame>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QStatusBar>

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
	QWidget* central = new QWidget(this);

	QHBoxLayout* layout = new QHBoxLayout(central);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	QFrame* divider = new QFrame(central);
	divider->setFrameShape(QFrame::VLine);
	divider->setFrameShadow(QFrame::Plain);

	layout->addWidget(m_rail);
	layout->addWidget(divider);
	layout->addWidget(m_stack, 1);

	setCentralWidget(central);

	addView(new HomeView);
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

	setWindowTitle(tr("%1 — QuickDC").arg(m_views.at(index)->title()));
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

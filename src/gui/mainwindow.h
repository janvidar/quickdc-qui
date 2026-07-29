/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_MAINWINDOW_H
#define HAVE_QUICKDC_QUI_MAINWINDOW_H

#include <QList>
#include <QMainWindow>

class QStackedWidget;

namespace Qdc {

class CoreThread;
class NavigationRail;
class TransferBridge;
class View;

/**
 * The application window: a navigation rail, and one view at a time beside it.
 *
 * The window owns the bridges and hands each view the ones it needs, so a view
 * never reaches for the core itself and the lifetimes stay in one place. That
 * ordering matters on the way out as well - see the destructor.
 *
 * Secondary navigation and the context sidebar from the layout are deliberately
 * not here. Only the hub view has any use for either, and a window that laid
 * them out would have to keep asking every view whether it wanted them.
 */
class MainWindow final : public QMainWindow {
	Q_OBJECT

public:
	/**
	 * @param thread a started core thread. A stopped one is accepted - the
	 *        bridges report their own failures - but nothing will work.
	 */
	explicit MainWindow(CoreThread* thread, QWidget* parent = nullptr);
	~MainWindow() override;

	/** For tests; the router is otherwise driven by the rail. */
	int currentIndex() const;
	void setCurrentIndex(int index);

	NavigationRail* rail() const;

private:
	/**
	 * Add a view as a rail destination, wiring its badge to the rail and taking
	 * ownership of it.
	 */
	void addView(View* view);

	/** Follow the current view: rail selection, stack, and window title. */
	void showView(int index);

	CoreThread* m_thread;

	/**
	 * Only the transfer bridge so far. The settings view brings a ConfigBridge
	 * with it; there is nothing for one to do until then.
	 */
	TransferBridge* m_transfers;

	NavigationRail* m_rail;
	QStackedWidget* m_stack;
	QList<View*> m_views;
};

}

#endif // HAVE_QUICKDC_QUI_MAINWINDOW_H

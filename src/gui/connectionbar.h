/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_CONNECTIONBAR_H
#define HAVE_QUICKDC_QUI_CONNECTIONBAR_H

#include <core/coretypes.h>

#include <QWidget>

class QAbstractButton;
class QLabel;
class QPushButton;
class QTabWidget;
class QTimer;

namespace Qdc {

/**
 * A band across a conversation that cannot be used, saying why.
 *
 * Shown only while something is pending or wrong: a hub being looked up, a login
 * that has not finished, a session that has ended. A conversation on a hub that is
 * there and has let us in has no band at all, which is the state it is in almost
 * all of the time and the one that must look like nothing was added.
 *
 * The wait before a reconnect counts down here rather than being stated once. A
 * number that was true when the line was written is worse than no number: it reads
 * as current, and the one thing being asked of it is how long is left.
 *
 * Laid over the conversation rather than inserted above it - see attachTo(). A row
 * of its own would have to be a row in every tab page to appear on all of them,
 * and the appearing and disappearing would relayout the feed underneath.
 */
class ConnectionBar final : public QWidget {
	Q_OBJECT

public:
	explicit ConnectionBar(QWidget* parent = nullptr);

	/**
	 * Become @a tabs' overlay, sitting across the top of whichever page is shown.
	 *
	 * A child of the tab widget rather than of a page, so that it is the same band
	 * on every tab: the connection is the conversation's, not one tab's. Positioned
	 * under the tab bar and kept there as the widget is resized, which is why the
	 * tab widget is watched rather than simply measured once.
	 */
	void attachTo(QTabWidget* tabs);

	/**
	 * Follow the connection into the band.
	 *
	 * Both halves, because being connected to a hub is not being logged in to one
	 * and the band has to tell those apart - the same pair the composer follows for
	 * whether a message can be sent at all.
	 */
	void setStatus(NetStatus net, HubStatus hub);

	/**
	 * The core will try again in @a seconds, or not at all if it is -1.
	 *
	 * Kept until the next network status arrives rather than until it reaches zero:
	 * the attempt itself reports a status, and that is what says the wait is over.
	 */
	void setReconnectIn(int seconds);

	/**
	 * The other party in a private conversation is not on the hub.
	 *
	 * Said ahead of anything about the connection, because it is the more specific
	 * answer: a hub that is up and has let us in is no help when the person being
	 * written to has gone, and that is exactly the case the band would otherwise be
	 * hidden for.
	 *
	 * @param who their nickname, or empty when they are back - which is how the
	 *        band is cleared again.
	 */
	void setAbsent(const QString& who);

	/** What the band currently says, which is empty while it is hidden. */
	QString text() const;

	/** The button that asks for the attempt now, for a test to press. */
	QAbstractButton* reconnectButton() const;

signals:
	/**
	 * Somebody would rather not wait out the countdown.
	 *
	 * The band knows an attempt is pending and when, and nothing else on screen
	 * does - but it has no hub to act on, so the asking is passed up to whoever
	 * owns the conversation.
	 */
	void reconnectRequested();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void showEvent(QShowEvent* event) override;

private:
	/** Put the current state into the label, and show or hide the band. */
	void refresh();

	/** Sit across the tab widget, directly under its tab bar. */
	void place();

	/** What the band says about the connection, without the wait. */
	QString reason() const;

	/** The wait as a phrase to append, or empty when there is nothing to append. */
	QString countdown() const;

	QLabel* m_label;

	/**
	 * Shown only while an attempt is pending.
	 *
	 * A wait that is nearly five minutes long is the case this is here for: the
	 * hub is very likely there, the client is simply serving out a penalty for
	 * the attempts before this one, and the person watching knows something the
	 * backoff does not.
	 */
	QPushButton* m_now;

	/**
	 * Ticks while there is a wait to show, and is stopped the moment there is not.
	 * A timer left running would redraw a band nobody can see, once a second, for
	 * as long as the application is open.
	 */
	QTimer* m_tick;

	/** What this is laid over, or null until attachTo(). */
	QTabWidget* m_tabs = nullptr;

	NetStatus m_net = NetStatus::Invalid;
	HubStatus m_hub = HubStatus::None;

	/** Who has left, or empty when nobody has; see setAbsent(). */
	QString m_absent;

	/** Seconds left, or -1 for no attempt pending. */
	int m_remaining = -1;
};

}

#endif // HAVE_QUICKDC_QUI_CONNECTIONBAR_H

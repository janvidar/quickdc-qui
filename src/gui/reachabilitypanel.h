/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_REACHABILITYPANEL_H
#define HAVE_QUICKDC_QUI_REACHABILITYPANEL_H

/* For ReachabilityVerdict by value: this keeps the last one seen per family. */
#include <core/dropbridge.h>

#include <QWidget>

class QAbstractButton;
class QGridLayout;
class QLabel;
class QPushButton;

namespace Qdc {

/**
 * What a reachability measurement established, as a grid to be glanced at.
 *
 * A verdict is four answers about two address families - where the world sees this
 * client, and whether inbound TCP and UDP arrive - and as prose that is four
 * sentences nobody reads. Laid out as a grid it is one look, and the shape of a
 * cell says which of three answers it is before any of it has been read.
 *
 * Knows nothing about how a measurement is taken. It is handed verdicts and asked
 * for one through checkRequested(), which is what keeps it out of the rule that
 * nothing which draws may wait for the core - see tests/nothing-blocks-the-views.sh.
 * It also means a test can put a verdict on screen without a core to produce one,
 * which is the only way the rendering gets covered at all: every existing settings
 * test builds the page with no bridge behind it.
 */
class ReachabilityPanel final : public QWidget {
	Q_OBJECT

public:
	explicit ReachabilityPanel(QWidget* parent = nullptr);

	/**
	 * Show what a run established, keeping the other family's answer.
	 *
	 * One run answers for one family, so the two are held apart: drawing each on
	 * its own would have the second wipe the first off a panel that claims to
	 * describe the client. A verdict whose 'ran' is false is ignored rather than
	 * stored, since it says nothing to store.
	 */
	void setVerdict(const ReachabilityVerdict& verdict);

	/** Back to nothing having been measured, both families. */
	void clear();

	/**
	 * Whether a run is in flight.
	 *
	 * Every row is left exactly as it was. A run that has not finished has not
	 * displaced what was measured last time, and blanking the grid to say so would
	 * throw away the only answer in hand.
	 */
	void setChecking(bool checking);

	/**
	 * Why nobody measured, which says nothing about being reachable.
	 *
	 * Again the rows above are left alone: an earlier measurement is still the best
	 * thing known, and the time it was taken is already beside it. Empty clears the
	 * line.
	 */
	void setFailure(const QString& reason);

	/**
	 * Whether the per-port rows are shown.
	 *
	 * False where this client is not trying to be reached, which has an address
	 * worth showing and no inbound answer worth asking for. The rows are hidden
	 * rather than drawn as dashes, because a dash invites somebody to go and find
	 * out and there is nothing to find out.
	 */
	void setShowPorts(bool shown);

	/** For a test to press, since the members are private. */
	QAbstractButton* checkButton() const;

	/** What the grid currently says, for tests. */
	QString addressText(bool ipv6) const;
	QString portText(bool ipv6, bool udp) const;

signals:
	/** Somebody asked for a measurement. */
	void checkRequested();

protected:
	/* The cells are pixmaps painted in the ink of the scheme that was current. */
	void changeEvent(QEvent* event) override;

private:
	/**
	 * One answer: a shape and a word for it.
	 *
	 * Both, and not a colour. Whether a port can be reached is not a thing to
	 * leave resting on telling green from red, and the same judgement is made
	 * about the padlock in HubHeader::refreshLock().
	 */
	struct Cell {
		QLabel* icon = nullptr;
		QLabel* text = nullptr;
	};

	enum class State { Reached, Blocked, Unmeasured };

	Cell makeCell(const QString& name);
	void paintCell(const Cell& cell, State state) const;
	void setCell(const Cell& cell, State state, const QString& word,
	             const QString& tooltip);

	void refresh();

	/**
	 * Draw one family's rows.
	 *
	 * Takes the slot rather than reading the family off the verdict in it: a
	 * verdict that has not run carries no family either, so both slots would
	 * answer for IPv4 and the IPv6 rows would never be drawn at all.
	 */
	void refreshFamily(int which);

	const Cell& cellFor(bool ipv6, bool udp) const;

	QGridLayout* m_grid;

	QLabel* m_address[2];
	Cell m_tcp[2];
	Cell m_udp[2];
	QLabel* m_family[2];

	/**
	 * Which grid row each family's ports occupy.
	 *
	 * Kept because hiding them means hiding the headings beside them too, and
	 * those belong to the grid rather than to this: a "TCP" left on screen with
	 * nothing after it reads as an answer that failed to arrive.
	 */
	int m_portRow[2] = { 0, 0 };

	QLabel* m_portsHeading;
	QLabel* m_checked;
	QPushButton* m_check;
	QLabel* m_reason;
	QLabel* m_note;
	QLabel* m_attempt;

	/** One per family, because one run answers for one of them. */
	ReachabilityVerdict m_verdict[2];

	bool m_checking = false;
	bool m_showPorts = true;
};

}

#endif // HAVE_QUICKDC_QUI_REACHABILITYPANEL_H

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "gui/reachabilitypanel.h"

#include "gui/glyphs.h"
#include "gui/richtext.h"
#include "gui/theme.h"
#include "gui/widgets.h"

#include <QEvent>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>

namespace {

/** The size a cell's shape is drawn at, matching the padlock in the hub strip. */
constexpr int GLYPH = 16;

/* Which slot of the two-element arrays a family lives in. */
constexpr int V4 = 0;
constexpr int V6 = 1;

int slot(bool ipv6)
{
	return ipv6 ? V6 : V4;
}

}

Qdc::ReachabilityPanel::ReachabilityPanel(QWidget* parent)
	: QWidget(parent)
	, m_grid(new QGridLayout(this))
	, m_portsHeading(new QLabel(tr("Reachable"), this))
	, m_checked(new QLabel(this))
	, m_check(new QPushButton(tr("Check now"), this))
{
	setObjectName(QStringLiteral("reachabilityPanel"));

	m_grid->setContentsMargins(0, 0, 0, 0);
	m_grid->setHorizontalSpacing(12);

	m_family[V4] = new QLabel(tr("IPv4"), this);
	m_family[V6] = new QLabel(tr("IPv6"), this);

	for (const int which : {V4, V6})
	{
		/*
		 * Plain text throughout, and none of it left to guess. Every string on
		 * this panel that is not one of ours came from a DROP server, and a label
		 * on Qt::AutoText consults a heuristic that would let that server set the
		 * type in our chrome or point a label at a resource to fetch.
		 */
		m_address[which] = new QLabel(this);
		m_address[which]->setTextFormat(Qt::PlainText);
		m_address[which]->setTextInteractionFlags(Qt::TextSelectableByMouse);
		m_address[which]->setObjectName(which == V6
			? QStringLiteral("reachabilityAddress6")
			: QStringLiteral("reachabilityAddress4"));

		m_tcp[which] = makeCell(which == V6
			? QStringLiteral("reachabilityTcp6") : QStringLiteral("reachabilityTcp4"));
		m_udp[which] = makeCell(which == V6
			? QStringLiteral("reachabilityUdp6") : QStringLiteral("reachabilityUdp4"));
	}

	m_checked->setTextFormat(Qt::PlainText);
	m_checked->setObjectName(QStringLiteral("reachabilityChecked"));

	m_check->setToolTip(tr("Ask now what a server outside can reach, rather than "
	                       "waiting for the next time QuickDC starts"));

	m_reason = makeNote(QString(), this);
	m_reason->setTextFormat(Qt::PlainText);

	m_note = makeNote(QString(), this);
	m_note->setTextFormat(Qt::PlainText);

	m_attempt = makeNote(QString(), this);
	m_attempt->setTextFormat(Qt::PlainText);

	int row = 0;

	m_grid->addWidget(new QLabel(tr("Address"), this), row, 0);
	m_grid->addWidget(m_family[V4], row, 1);
	m_grid->addWidget(m_address[V4], row, 2, 1, 5);
	row++;

	m_grid->addWidget(m_family[V6], row, 1);
	m_grid->addWidget(m_address[V6], row, 2, 1, 5);
	row++;

	m_grid->addWidget(m_portsHeading, row, 0);

	for (const int which : {V4, V6})
	{
		/*
		 * The family is named again beside the ports rather than the two rows
		 * being read off the block above: a panel where one family is measured
		 * and the other is not would otherwise have the reader counting rows to
		 * find out which of them they are looking at.
		 */
		QLabel* family = new QLabel(which == V6 ? tr("IPv6") : tr("IPv4"), this);
		m_grid->addWidget(family, row, 1);

		m_grid->addWidget(new QLabel(tr("TCP"), this), row, 2);
		m_grid->addWidget(m_tcp[which].icon, row, 3);
		m_grid->addWidget(m_tcp[which].text, row, 4);

		m_grid->addWidget(new QLabel(tr("UDP"), this), row, 5);
		m_grid->addWidget(m_udp[which].icon, row, 6);
		m_grid->addWidget(m_udp[which].text, row, 7);

		m_portRow[which] = row;
		row++;
	}

	m_grid->addWidget(new QLabel(tr("Checked"), this), row, 0);
	m_grid->addWidget(m_checked, row, 1, 1, 4);
	m_grid->addWidget(m_check, row, 5, 1, 3, Qt::AlignRight);
	row++;

	m_grid->addWidget(m_attempt, row++, 0, 1, 8);
	m_grid->addWidget(m_reason, row++, 0, 1, 8);
	m_grid->addWidget(m_note, row++, 0, 1, 8);

	/* The answers take the width; the headings take what they need. */
	m_grid->setColumnStretch(4, 1);
	m_grid->setColumnStretch(7, 1);

	/*
	 * Never taller than the rows need. A grid handed height beyond that shares it
	 * out between its rows, which moves an address away from the family it belongs
	 * to - and this sits in a form with room to spare, so there is always height on
	 * offer. Refusing it leaves the spare space at the foot of the page, where every
	 * other tab already leaves it.
	 */
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

	connect(m_check, &QPushButton::clicked, this,
	        &ReachabilityPanel::checkRequested);

	clear();
}

Qdc::ReachabilityPanel::Cell Qdc::ReachabilityPanel::makeCell(const QString& name)
{
	Cell cell;

	cell.icon = new QLabel(this);
	cell.icon->setFixedSize(GLYPH, GLYPH);
	cell.icon->setObjectName(name + QStringLiteral("Icon"));

	cell.text = new QLabel(this);
	cell.text->setTextFormat(Qt::PlainText);
	cell.text->setObjectName(name);

	return cell;
}

QAbstractButton* Qdc::ReachabilityPanel::checkButton() const
{
	return m_check;
}

QString Qdc::ReachabilityPanel::addressText(bool ipv6) const
{
	return m_address[slot(ipv6)]->text();
}

QString Qdc::ReachabilityPanel::portText(bool ipv6, bool udp) const
{
	return cellFor(ipv6, udp).text->text();
}

const Qdc::ReachabilityPanel::Cell& Qdc::ReachabilityPanel::cellFor(bool ipv6,
                                                                   bool udp) const
{
	return udp ? m_udp[slot(ipv6)] : m_tcp[slot(ipv6)];
}

void Qdc::ReachabilityPanel::setVerdict(const ReachabilityVerdict& verdict)
{
	/* A run that established nothing is not an answer, whatever else it carries. */
	if (!verdict.ran) return;

	m_verdict[slot(verdict.ipv6)] = verdict;

	m_checking = false;
	m_attempt->clear();
	m_attempt->setVisible(false);

	refresh();
}

void Qdc::ReachabilityPanel::clear()
{
	m_verdict[V4] = ReachabilityVerdict();
	m_verdict[V6] = ReachabilityVerdict();

	m_checking = false;

	refresh();
}

void Qdc::ReachabilityPanel::setChecking(bool checking)
{
	m_checking = checking;

	if (checking)
	{
		m_attempt->setText(tr("Checking…"));
		m_attempt->setVisible(true);
	}
	else if (m_attempt->text() == tr("Checking…"))
	{
		m_attempt->clear();
		m_attempt->setVisible(false);
	}
}

void Qdc::ReachabilityPanel::setFailure(const QString& reason)
{
	m_checking = false;

	m_attempt->setText(reason.isEmpty()
		? QString()
		: tr("The check could not be run: %1").arg(reason));

	m_attempt->setVisible(!reason.isEmpty());
}

void Qdc::ReachabilityPanel::setShowPorts(bool shown)
{
	if (m_showPorts == shown) return;

	m_showPorts = shown;
	refresh();
}

void Qdc::ReachabilityPanel::refresh()
{
	m_portsHeading->setVisible(m_showPorts);

	for (const int which : {V4, V6})
	{
		refreshFamily(which);

		for (const Cell* cell : {&m_tcp[which], &m_udp[which]})
		{
			cell->icon->setVisible(m_showPorts);
			cell->text->setVisible(m_showPorts);
		}

		/* Every widget in the row, including the headings the grid owns. */
		for (int column = 1; column < 8; column++)
		{
			if (QLayoutItem* item = m_grid->itemAtPosition(m_portRow[which], column))
			{
				if (QWidget* widget = item->widget())
					widget->setVisible(m_showPorts);
			}
		}
	}

	QStringList explanation;
	QStringList notes;
	QDateTime oldest;

	for (const int which : {V4, V6})
	{
		const ReachabilityVerdict& verdict = m_verdict[which];
		if (!verdict.ran) continue;

		/*
		 * The older of the two, because it is the one that says how much of this
		 * panel can still be relied on: a reader told the time of the newer would
		 * be told the whole of it was that fresh.
		 */
		if (verdict.observedAt.isValid()
		    && (!oldest.isValid() || verdict.observedAt < oldest))
			oldest = verdict.observedAt;

		const QString family = verdict.ipv6 ? tr("IPv6") : tr("IPv4");

		if (m_showPorts && !verdict.reason.isEmpty())
			explanation += tr("%1: %2").arg(family, verdict.reason);

		if (m_showPorts)
		{
			for (const QString& remedy : verdict.remedies)
				explanation += QStringLiteral("• ") + remedy;
		}

		if (m_showPorts && !verdict.note.isEmpty()) notes += verdict.note;
	}

	m_checked->setText(oldest.isValid()
		? QLocale().toString(oldest, QLocale::ShortFormat)
		: tr("never"));

	m_reason->setText(explanation.join(QLatin1Char('\n')));
	m_reason->setVisible(!explanation.isEmpty());

	notes.removeDuplicates();
	m_note->setText(notes.join(QLatin1Char('\n')));
	m_note->setVisible(!notes.isEmpty());
}

void Qdc::ReachabilityPanel::refreshFamily(int which)
{
	const ReachabilityVerdict& verdict = m_verdict[which];

	/*
	 * A family nobody has measured says so, rather than being left out. Absent
	 * from a panel listing the other family reads as a family that failed, and the
	 * two are not the same thing to somebody deciding what to change.
	 */
	if (!verdict.ran || verdict.publicAddress.isEmpty())
	{
		m_address[which]->setText(tr("not measured"));
	}
	else if (verdict.classification.isEmpty())
	{
		m_address[which]->setText(verdict.publicAddress);
	}
	else
	{
		/* The server's own word for what it saw, which need not be an address
		   this host has: behind a carrier-grade NAT it is the provider's. */
		m_address[which]->setText(tr("%1 · %2")
			.arg(verdict.publicAddress, verdict.classification));
	}

	struct Port {
		const Cell* cell;
		ReachabilityReach reach;
		QString named;
	};

	const Port ports[] = {
		{ &m_tcp[which], verdict.tcpReached, verdict.tcp },
		{ &m_udp[which], verdict.udpReached, verdict.udp },
	};

	for (const Port& port : ports)
	{
		if (!verdict.ran)
		{
			setCell(*port.cell, State::Unmeasured, tr("not measured"), QString());
			continue;
		}

		/*
		 * An empty name is a test the server does not offer, rather than a port
		 * that failed. Reporting it as a failure would argue somebody into
		 * turning off a feature that works.
		 */
		if (port.named.isEmpty())
		{
			setCell(*port.cell, State::Unmeasured, tr("not tested"),
			        tr("The server did not run this test, so nothing is known "
			           "about this port either way."));
			continue;
		}

		const State state = port.reach == ReachabilityReach::Reached
			? State::Reached
			: (port.reach == ReachabilityReach::Blocked ? State::Blocked
			                                            : State::Unmeasured);

		/*
		 * The measuring library's own word for the outcome, shown as it arrived.
		 * Somebody who moves between two clients and is given two different
		 * accounts of one network concludes that one of them is broken.
		 */
		setCell(*port.cell, state, port.named, verdict.reason);
	}
}

void Qdc::ReachabilityPanel::setCell(const Cell& cell, State state,
                                     const QString& word, const QString& tooltip)
{
	paintCell(cell, state);

	cell.text->setText(word);

	/*
	 * Escaped, because a tooltip cannot be plain text: QToolTip hands its string
	 * to a label on Qt::AutoText, and every word of a reason came from a server
	 * this client does not control.
	 */
	const QString hint = tooltip.isEmpty() ? word : tooltip;

	cell.text->setToolTip(RichText::literalHtml(hint));
	cell.icon->setToolTip(cell.text->toolTip());

	cell.text->setAccessibleName(hint);
	cell.icon->setAccessibleName(hint);
}

void Qdc::ReachabilityPanel::paintCell(const Cell& cell, State state) const
{
	const qreal ratio = devicePixelRatioF();

	/*
	 * Three shapes for three states, and the colour only reinforces them. Whether
	 * a port can be reached is not a thing to leave resting on telling green from
	 * red - the same judgement HubHeader::refreshLock() makes about the padlock.
	 */
	Glyph glyph = Glyph::Dash;
	QColor colour = theme().contentMuted;

	if (state == State::Reached)
	{
		glyph = Glyph::Check;
		colour = theme().success;
	}
	else if (state == State::Blocked)
	{
		glyph = Glyph::Close;
		colour = theme().danger;
	}

	QPixmap pixmap(QSize(GLYPH, GLYPH) * ratio);
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(Qt::transparent);

	QPainter painter(&pixmap);
	paint_glyph(painter, glyph, QRectF(0, 0, GLYPH, GLYPH), colour);
	painter.end();

	cell.icon->setPixmap(pixmap);
}

void Qdc::ReachabilityPanel::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);

	/* The cells are pixmaps painted in the ink of the scheme that was current. */
	if (event->type() == QEvent::PaletteChange
	    || event->type() == QEvent::StyleChange)
	{
		refresh();
	}
}

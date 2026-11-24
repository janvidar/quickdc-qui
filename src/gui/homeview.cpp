/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "homeview.h"

#include "hubbrowser.h"
#include "richtext.h"

#include <core/completed.h>
#include <core/configbridge.h>
#include <core/transferbridge.h>

#include <QFileInfo>
#include <QFontMetrics>
#include <QTabWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace {

/** How many finished downloads the summary lists. */
constexpr int RECENT_SHOWN = 6;

/** Matches the transfers view; the summary line is polled the same way. */
constexpr int POLL_INTERVAL_MS = 1000;

QString format_size(quint64 bytes)
{
	return QLocale().formattedDataSize(static_cast<qint64>(bytes));
}

/**
 * Size a list to the rows it holds, between a floor and a ceiling.
 *
 * A fixed height leaves a short list sitting in a mostly empty box, which reads as
 * something not yet filled in rather than as a list of two things.
 */
void fit_list(QListWidget* list, int maxRows)
{
	const int rows = qBound(1, list->count(), maxRows);

	/* -1 from an empty list, and the font is the fallback either way. */
	const int measured = list->sizeHintForRow(0);
	const int rowHeight = (measured > 0)
		? measured : QFontMetrics(list->font()).lineSpacing() + 2;

	list->setFixedHeight(rows * rowHeight + 2 * list->frameWidth() + 6);
}

/** Secondary, and named so that the stylesheet says which colour that is. */
QLabel* muted(QLabel* label)
{
	label->setObjectName(QStringLiteral("mutedText"));
	return label;
}

}

Qdc::HomeView::HomeView(ConfigBridge* config, Favourites* favourites,
                        RecentHubs* recent, HubDirectory* directory,
                        Completed* completed, TransferBridge* transfers,
                        QWidget* parent)
	: View(parent)
	, m_config(config)
	, m_completed(completed)
	, m_transfers(transfers)
	, m_greeting(new QLabel(this))
	, m_identity(new QLabel(this))
	, m_address(new QLineEdit(this))
	, m_connect(new QPushButton(tr("Connect"), this))
	, m_hubs(new HubBrowser(directory, favourites, recent, this))
	, m_transferSummary(new QLabel(this))
	, m_recentList(new QListWidget(this))
	, m_recentEmpty(new QLabel(this))
	, m_poll(new QTimer(this))
{
	/*
	 * One row of tabs for the whole page, which is why the hub browser's own strip is
	 * borrowed rather than nested inside a tab of its own: its three lists and the two
	 * pages here are five answers to "what now", and a row of tabs under a row of tabs
	 * is two things to read before either can be used.
	 *
	 * Welcome first, then the hubs somebody has been on, then the rest.
	 */
	QTabWidget* tabs = m_hubs->tabs();

	tabs->insertTab(0, makePage(buildGreeting()), tr("Welcome"));
	tabs->addTab(makePage(buildActivity()), tr("Activity"));
	tabs->setCurrentIndex(0);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_hubs);

	m_poll->setInterval(POLL_INTERVAL_MS);

	/* The timer asks the core for figures; drawing waits for them to arrive. */
	connect(m_poll, &QTimer::timeout, this, [this] {
		if (m_transfers) m_transfers->refresh();
	});

	/* A hub chosen in the browser is opened the same way a typed address is: this
	   view cannot do it, so it asks. */
	connect(m_hubs, &HubBrowser::hubRequested, this, &HomeView::hubRequested);

	if (m_completed)
	{
		connect(m_completed, &Completed::changed, this, &HomeView::refreshActivity);
	}

	if (m_transfers)
	{
		/*
		 * The counts are event driven so the summary is right the moment a transfer
		 * starts or ends; only the rates need the timer.
		 */
		connect(m_transfers, &TransferBridge::transferStarted,
		        this, &HomeView::refreshActivity);
		connect(m_transfers, &TransferBridge::transferFinished,
		        this, &HomeView::refreshActivity);
		connect(m_transfers, &TransferBridge::transferStopped,
		        this, &HomeView::refreshActivity);
		connect(m_transfers, &TransferBridge::transferError,
		        this, &HomeView::refreshActivity);

		/* And drawn again whenever fresh figures arrive from the core. */
		connect(m_transfers, &TransferBridge::snapshotChanged,
		        this, &HomeView::refreshActivity);
	}

	refresh();
}

QString Qdc::HomeView::title() const
{
	return tr("Home");
}

Qdc::Glyph Qdc::HomeView::glyph() const
{
	return Glyph::Home;
}

QWidget* Qdc::HomeView::makePage(QWidget* page)
{
	if (QLayout* content = page->layout())
		content->setContentsMargins(16, 16, 16, 16);

	QScrollArea* scroll = new QScrollArea(this);
	scroll->setWidget(page);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);

	return scroll;
}

QWidget* Qdc::HomeView::buildGreeting()
{
	QWidget* page = new QWidget(this);

	QFont large = m_greeting->font();
	large.setPointSize(large.pointSize() + 5);
	large.setBold(true);
	m_greeting->setFont(large);

	muted(m_identity);
	m_identity->setWordWrap(true);

	/*
	 * Both lines are this user's own nickname and description rather than a stranger's,
	 * but a QLabel left to itself guesses at whether what it was given is rich text, and
	 * a name with an angle bracket in it should read as it was typed.
	 */
	m_greeting->setTextFormat(Qt::PlainText);
	m_identity->setTextFormat(Qt::PlainText);

	m_address->setPlaceholderText(
		tr("dchub://hub.example.org:411 or adcs://hub.example.org:412"));
	m_address->setClearButtonEnabled(true);

	connect(m_address, &QLineEdit::returnPressed,
	        this, &HomeView::connectToTypedAddress);
	connect(m_connect, &QPushButton::clicked,
	        this, &HomeView::connectToTypedAddress);

	QHBoxLayout* addressRow = new QHBoxLayout;
	addressRow->setContentsMargins(0, 0, 0, 0);
	addressRow->addWidget(m_address, 1);
	addressRow->addWidget(m_connect);

	QVBoxLayout* layout = new QVBoxLayout(page);
	layout->addWidget(m_greeting);
	layout->addWidget(m_identity);
	layout->addSpacing(6);
	layout->addWidget(new QLabel(tr("Connect to a hub by address:"), page));
	layout->addLayout(addressRow);
	layout->addStretch(1);

	return page;
}

QWidget* Qdc::HomeView::buildActivity()
{
	QWidget* page = new QWidget(this);

	/* Named, because a test looking for a list here would otherwise find one of the
	   browser's three first. */
	m_recentList->setObjectName(QStringLiteral("recentDownloads"));

	m_recentList->setSelectionMode(QAbstractItemView::NoSelection);

	muted(m_recentEmpty);
	m_recentEmpty->setWordWrap(true);
	m_recentEmpty->setText(tr("Nothing has finished downloading yet."));

	QVBoxLayout* layout = new QVBoxLayout(page);
	layout->addWidget(m_transferSummary);
	layout->addSpacing(4);
	layout->addWidget(m_recentEmpty);
	layout->addWidget(m_recentList);

	/*
	 * The slack at the foot rather than on the list, which fit_list() gives a fixed
	 * height so that a list of two things is two rows tall. Given the stretch it took
	 * the page's whole height while hidden and left the two lines floating apart.
	 */
	layout->addStretch(1);

	return page;
}

void Qdc::HomeView::refresh()
{
	refreshGreeting();
	refreshActivity();
}

Qdc::HubBrowser* Qdc::HomeView::hubs() const
{
	return m_hubs;
}

void Qdc::HomeView::refreshGreeting()
{
	if (!m_config)
	{
		m_greeting->setText(tr("QuickDC"));
		m_identity->setText(tr("The configuration is not available."));
		return;
	}

	namespace S = Qdc::Settings;

	/* Only a chosen one: the name the core falls back to is nobody's choice. */
	QString nickname = m_config->read(S::Nickname).trimmed();
	if (nickname == QString::fromUtf8(S::Nickname.fallback)) nickname.clear();

	m_greeting->setText(nickname.isEmpty()
		? tr("Welcome to QuickDC")
		: tr("Welcome back, %1").arg(nickname));

	const QString description = m_config->read(S::Description).trimmed();
	const bool active = m_config->read(S::ActiveMode);

	QStringList parts;
	if (!description.isEmpty()) parts << description;
	parts << (active ? tr("active mode") : tr("passive mode"));

	if (nickname.isEmpty())
	{
		parts.prepend(tr("Set a nickname under Settings before connecting"));
	}

	m_identity->setText(parts.join(QStringLiteral(" · ")));
}

void Qdc::HomeView::refreshActivity()
{
	quint64 down = 0;
	quint64 up = 0;
	int moving = 0;

	if (m_transfers)
	{
		const QVector<TransferInfo> active = m_transfers->activeTransfers();
		moving = static_cast<int>(active.size());

		for (const TransferInfo& transfer : active)
		{
			if (transfer.upload)
				up += transfer.bytesPerSecond;
			else
				down += transfer.bytesPerSecond;
		}
	}

	/* What is waiting, so that "in progress" and "queued" do not count the same
	   file twice; a file being fetched stays in the queue until it is complete. */
	const int queued = m_transfers
		? static_cast<int>(m_transfers->waitingEntries().size()) : 0;

	if (moving == 0 && queued == 0)
	{
		m_transferSummary->setText(tr("Nothing is being transferred."));
	}
	else
	{
		m_transferSummary->setText(
			tr("%n transfer(s) in progress", nullptr, moving)
			+ tr(", %n queued", nullptr, queued)
			+ QStringLiteral("   ↓ %1/s   ↑ %2/s")
				.arg(format_size(down), format_size(up)));
	}

	m_recentList->clear();

	const QList<CompletedDownload> recent = m_completed
		? m_completed->entries() : QList<CompletedDownload>();

	for (int n = 0; n < qMin(RECENT_SHOWN, static_cast<int>(recent.size())); ++n)
	{
		const CompletedDownload& entry = recent.at(n);

		/*
		 * The leaf is the name a stranger's share gave the file, so the row is labelled
		 * with it disarmed. The tooltip is the whole path and is not: a user reads one in
		 * order to go and find the file with it, and a path cut short is no use for that.
		 */
		QListWidgetItem* item = new QListWidgetItem(
			QStringLiteral("%1  —  %2")
				.arg(RichText::displayText(QFileInfo(entry.filename).fileName()),
				     format_size(entry.size)),
			m_recentList);

		item->setToolTip(RichText::literalHtml(entry.filename));
	}

	fit_list(m_recentList, RECENT_SHOWN);

	const bool any = !recent.isEmpty();
	m_recentList->setVisible(any);
	m_recentEmpty->setVisible(!any);
}

void Qdc::HomeView::connectToTypedAddress()
{
	const QString url = m_address->text().trimmed();
	if (url.isEmpty()) return;

	/*
	 * Cleared on the way out: the address has been handed over, and leaving it
	 * behind makes a second press look as though the first did nothing.
	 */
	m_address->clear();

	emit hubRequested(url);
}

int Qdc::HomeView::favouriteCount() const
{
	return m_hubs->favouriteCount();
}

int Qdc::HomeView::recentHubCount() const
{
	return m_hubs->recentCount();
}

int Qdc::HomeView::completedCount() const
{
	return m_recentList->count();
}

void Qdc::HomeView::showEvent(QShowEvent* event)
{
	View::showEvent(event);

	if (m_transfers) m_transfers->refresh();

	refresh();
	m_poll->start();
}

void Qdc::HomeView::hideEvent(QHideEvent* event)
{
	View::hideEvent(event);
	m_poll->stop();
}

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "hubbrowser.h"

#include "hublistmodel.h"

#include <core/favourites.h>
#include <core/hubdirectory.h>
#include <core/recenthubs.h>

#include <QDateTime>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>

namespace {

/** Which role a list row keeps its hub's address in. */
constexpr int AddressRole = Qt::UserRole + 1;

/**
 * How tall the browser is before the layout gives it anything else.
 *
 * A hub list is worth having several rows of, and this sits inside a page that
 * scrolls - so without a floor of its own it would be given the height of one row
 * and the page would scroll past it.
 */
constexpr int MINIMUM_HEIGHT = 320;

/** Padding above and below a row of the public list; see prepare(). */
constexpr int ROW_PADDING = 8;

/**
 * How wide the columns of the public list start out.
 *
 * Given rather than sized to their contents, because a hublist's contents are
 * whatever a hub operator typed: names of eighty characters, countries spelled with
 * decorative symbols. Sized to those, the fixed columns alone came to more than the
 * window and the description was pushed off the side of a table that scrolled
 * sideways. Every one of them can still be dragged.
 */
constexpr int NAME_WIDTH = 230;
constexpr int COUNTRY_WIDTH = 90;
constexpr int ADDRESS_WIDTH = 220;

/** When something happened, at the precision anyone reading a list cares about. */
QString format_when(const QDateTime& when)
{
	if (!when.isValid()) return QString();

	const QDateTime now = QDateTime::currentDateTime();
	const qint64 seconds = when.secsTo(now);

	if (seconds < 60) return QObject::tr("just now");
	if (seconds < 3600) return QObject::tr("%n minute(s) ago", nullptr,
	                                       static_cast<int>(seconds / 60));
	if (seconds < 86400) return QObject::tr("%n hour(s) ago", nullptr,
	                                        static_cast<int>(seconds / 3600));

	/* Past a day, the date says more than a count of them does. */
	return QLocale().toString(when, QLocale::ShortFormat);
}

/** What a row keeps a hub's address in; a row that explains an empty list has
    none. */
QString row_address(const QListWidgetItem* item)
{
	return item ? item->data(AddressRole).toString() : QString();
}

/** Secondary, and named so that the stylesheet says which colour that is. */
QLabel* muted(QLabel* label)
{
	label->setObjectName(QStringLiteral("mutedText"));
	return label;
}

void prepare(QTableView* table)
{
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setSelectionMode(QAbstractItemView::SingleSelection);
	table->setAlternatingRowColors(true);
	table->setShowGrid(false);
	table->setFrameShape(QFrame::NoFrame);

	/*
	 * One line per hub, elided rather than wrapped. A hublist description is a
	 * paragraph advertising the hub - a web address, a protocol, an invitation - so
	 * rows sized to their contents came out three lines tall and half a dozen hubs
	 * filled the list. The whole description is in the tooltip.
	 */
	table->setWordWrap(false);
	table->setTextElideMode(Qt::ElideRight);

	table->verticalHeader()->setVisible(false);
	table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
	table->verticalHeader()->setDefaultSectionSize(
		QFontMetrics(table->font()).height() + ROW_PADDING);

	QHeaderView* header = table->horizontalHeader();
	header->setHighlightSections(false);

	/* The description takes the slack: it is the one column whose value is a
	   sentence, and the only one worth giving a window's width to. */
	header->setSectionResizeMode(Qdc::HubListModel::DescriptionColumn,
	                             QHeaderView::Stretch);

	header->setSectionResizeMode(Qdc::HubListModel::UsersColumn,
	                             QHeaderView::ResizeToContents);

	for (const int column : {Qdc::HubListModel::NameColumn,
	                         Qdc::HubListModel::CountryColumn,
	                         Qdc::HubListModel::AddressColumn})
	{
		header->setSectionResizeMode(column, QHeaderView::Interactive);
	}

	table->setColumnWidth(Qdc::HubListModel::NameColumn, NAME_WIDTH);
	table->setColumnWidth(Qdc::HubListModel::CountryColumn, COUNTRY_WIDTH);
	table->setColumnWidth(Qdc::HubListModel::AddressColumn, ADDRESS_WIDTH);
}

}

Qdc::HubBrowser::HubBrowser(HubDirectory* directory, Favourites* favourites,
                            RecentHubs* recent, QWidget* parent)
	: QWidget(parent)
	, m_directory(directory)
	, m_favourites(favourites)
	, m_recent(recent)
	, m_tabs(new QTabWidget(this))
	, m_search(new QLineEdit(this))
	, m_refresh(new QPushButton(tr("Refresh"), this))
	, m_table(new QTableView(this))
	, m_model(new HubListModel(this))
	, m_filtered(new QSortFilterProxyModel(this))
	, m_status(new QLabel(this))
	, m_openPublic(new QPushButton(tr("Connect"), this))
	, m_bookmark(new QPushButton(tr("Bookmark"), this))

	/* Busiest first, which is the order somebody looking for a hub to join wants and
	   the one nothing else would put them in. */
	, m_sortColumn(HubListModel::UsersColumn)
	, m_favouriteList(new QListWidget(this))
	, m_openFavourite(new QPushButton(tr("Connect"), this))
	, m_unbookmark(new QPushButton(tr("Unbookmark"), this))
	, m_recentList(new QListWidget(this))
	, m_openRecent(new QPushButton(tr("Connect"), this))
	, m_forget(new QPushButton(tr("Remove"), this))
	, m_clearRecent(new QPushButton(tr("Clear list"), this))
{
	/*
	 * Named, every one of them. The stylesheet reaches a widget by name, and so does
	 * a test - which otherwise has to find "the first list" or "an enabled button
	 * whose text is Connect", and there are now three of each.
	 */
	m_search->setObjectName(QStringLiteral("hubSearch"));
	m_refresh->setObjectName(QStringLiteral("refreshHubLists"));
	m_table->setObjectName(QStringLiteral("publicHubs"));
	m_openPublic->setObjectName(QStringLiteral("connectPublicHub"));
	m_bookmark->setObjectName(QStringLiteral("bookmarkHub"));
	m_favouriteList->setObjectName(QStringLiteral("favouriteHubs"));
	m_openFavourite->setObjectName(QStringLiteral("connectFavouriteHub"));
	m_unbookmark->setObjectName(QStringLiteral("unbookmarkHub"));
	m_recentList->setObjectName(QStringLiteral("recentHubs"));
	m_openRecent->setObjectName(QStringLiteral("connectRecentHub"));
	m_forget->setObjectName(QStringLiteral("forgetRecentHub"));
	m_clearRecent->setObjectName(QStringLiteral("clearRecentHubs"));

	/*
	 * Visited first, because Home shows this strip and wants the hubs somebody has
	 * been on next to its welcome rather than three screens of strangers' hubs.
	 * Which tab is current is set below rather than left to the order.
	 */
	m_tabs->setDocumentMode(true);
	m_visitedPage = buildRecent();
	m_publicPage = buildPublic();
	m_favouritesPage = buildFavourites();

	m_tabs->addTab(m_visitedPage, tr("Visited"));
	m_tabs->addTab(m_publicPage, tr("Public hubs"));
	m_tabs->addTab(m_favouritesPage, tr("Favourites"));

	/*
	 * The public list is where this opens on its own, whatever order the tabs were
	 * added in. Home moves it off that as soon as it has added its welcome.
	 */
	showTab(PublicTab);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_tabs);

	setMinimumHeight(MINIMUM_HEIGHT);

	/* Switching to the public list is looking at it, so it is fetched then too. */
	connect(m_tabs, &QTabWidget::currentChanged, this, [this] { refreshIfPublic(); });

	if (m_directory)
	{
		connect(m_directory, &HubDirectory::changed, this, [this] {
			refreshPublic();
			refreshStatus();
		});

		connect(m_directory, &HubDirectory::fetchingChanged, this, [this](bool fetching) {
			m_refresh->setEnabled(!fetching);
			refreshStatus();
		});
	}

	if (m_favourites)
	{
		connect(m_favourites, &Favourites::changed, this, [this] {
			refreshFavourites();

			/* The public tab's button says which way it would go. */
			followPublicSelection();
		});
	}

	if (m_recent)
	{
		connect(m_recent, &RecentHubs::changed, this, &HubBrowser::refreshRecent);
	}

	refreshPublic();
	refreshStatus();
	refreshFavourites();
	refreshRecent();

	/* Said rather than left to the order the tabs were added in, since everything
	   above has had a chance to fill a list in. */
	showTab(PublicTab);
}

QWidget* Qdc::HubBrowser::buildPublic()
{
	QWidget* page = new QWidget(this);

	m_search->setPlaceholderText(tr("Search by name, description, country or "
	                                "address"));
	m_search->setClearButtonEnabled(true);

	m_filtered->setSourceModel(m_model);
	m_filtered->setFilterCaseSensitivity(Qt::CaseInsensitive);

	/* Every column, so that a country code or an address matches as readily as a
	   name does. */
	m_filtered->setFilterKeyColumn(-1);

	/* The numbers are offered as numbers under the edit role; see HubListModel. */
	m_filtered->setSortRole(Qt::EditRole);

	m_table->setModel(m_filtered);
	prepare(m_table);
	m_table->setSortingEnabled(true);
	m_table->sortByColumn(m_sortColumn, m_sortOrder);

	/* Follow whoever sorts the table, so that a list arriving afterwards is sorted
	   the way they left it; see the members. */
	connect(m_table->horizontalHeader(), &QHeaderView::sortIndicatorChanged,
	        this, [this](int column, Qt::SortOrder order) {
		if (m_replacingRows || column < 0) return;

		m_sortColumn = column;
		m_sortOrder = order;
	});

	muted(m_status);
	m_status->setWordWrap(true);

	connect(m_search, &QLineEdit::textChanged, this, &HubBrowser::setFilter);
	connect(m_refresh, &QPushButton::clicked, this, [this] {
		refreshPublicLists(true);
	});

	connect(m_table, &QTableView::activated, this, [this] {
		const QString address = selectedPublicAddress();
		if (!address.isEmpty()) emit hubRequested(address);
	});

	connect(m_openPublic, &QPushButton::clicked, this, [this] {
		const QString address = selectedPublicAddress();
		if (!address.isEmpty()) emit hubRequested(address);
	});

	connect(m_bookmark, &QPushButton::clicked, this, &HubBrowser::toggleBookmark);

	connect(m_table->selectionModel(), &QItemSelectionModel::currentChanged,
	        this, [this] { followPublicSelection(); });

	followPublicSelection();

	QHBoxLayout* top = new QHBoxLayout;
	top->setContentsMargins(0, 0, 0, 0);
	top->addWidget(m_search, 1);
	top->addWidget(m_refresh);

	QHBoxLayout* bottom = new QHBoxLayout;
	bottom->setContentsMargins(0, 0, 0, 0);
	bottom->addWidget(m_status, 1);
	bottom->addWidget(m_bookmark);
	bottom->addWidget(m_openPublic);

	QVBoxLayout* layout = new QVBoxLayout(page);
	layout->addLayout(top);
	layout->addWidget(m_table, 1);
	layout->addLayout(bottom);

	return page;
}

QWidget* Qdc::HubBrowser::buildFavourites()
{
	QWidget* page = new QWidget(this);

	m_favouriteList->setSelectionMode(QAbstractItemView::SingleSelection);

	/* So that the row explaining an empty list is readable rather than elided. */
	m_favouriteList->setWordWrap(true);

	connect(m_favouriteList, &QListWidget::activated, this, [this] {
		const QString address = selectedAddress(m_favouriteList);
		if (!address.isEmpty()) emit hubRequested(address);
	});

	connect(m_openFavourite, &QPushButton::clicked, this, [this] {
		const QString address = selectedAddress(m_favouriteList);
		if (!address.isEmpty()) emit hubRequested(address);
	});

	connect(m_unbookmark, &QPushButton::clicked, this, [this] {
		const QString address = selectedAddress(m_favouriteList);

		/* The store's own signal is what brings the list back up to date. */
		if (m_favourites && !address.isEmpty()) m_favourites->remove(address);
	});

	connect(m_favouriteList, &QListWidget::currentRowChanged,
	        this, [this] { followFavouriteSelection(); });

	followFavouriteSelection();

	QHBoxLayout* buttons = new QHBoxLayout;
	buttons->setContentsMargins(0, 0, 0, 0);
	buttons->addStretch(1);
	buttons->addWidget(m_unbookmark);
	buttons->addWidget(m_openFavourite);

	QVBoxLayout* layout = new QVBoxLayout(page);
	layout->addWidget(m_favouriteList, 1);
	layout->addLayout(buttons);

	return page;
}

QWidget* Qdc::HubBrowser::buildRecent()
{
	QWidget* page = new QWidget(this);

	m_recentList->setSelectionMode(QAbstractItemView::SingleSelection);
	m_recentList->setWordWrap(true);

	connect(m_recentList, &QListWidget::activated, this, [this] {
		const QString address = selectedAddress(m_recentList);
		if (!address.isEmpty()) emit hubRequested(address);
	});

	connect(m_openRecent, &QPushButton::clicked, this, [this] {
		const QString address = selectedAddress(m_recentList);
		if (!address.isEmpty()) emit hubRequested(address);
	});

	connect(m_forget, &QPushButton::clicked, this, [this] {
		const QString address = selectedAddress(m_recentList);
		if (m_recent && !address.isEmpty()) m_recent->remove(address);
	});

	connect(m_clearRecent, &QPushButton::clicked, this, [this] {
		if (m_recent) m_recent->clear();
	});

	connect(m_recentList, &QListWidget::currentRowChanged,
	        this, [this] { followRecentSelection(); });

	followRecentSelection();

	QHBoxLayout* buttons = new QHBoxLayout;
	buttons->setContentsMargins(0, 0, 0, 0);
	buttons->addWidget(m_clearRecent);
	buttons->addStretch(1);
	buttons->addWidget(m_forget);
	buttons->addWidget(m_openRecent);

	QVBoxLayout* layout = new QVBoxLayout(page);
	layout->addWidget(m_recentList, 1);
	layout->addLayout(buttons);

	return page;
}

void Qdc::HubBrowser::showTab(Tab tab)
{
	QWidget* page = m_publicPage;

	if (tab == FavouritesTab) page = m_favouritesPage;
	else if (tab == VisitedTab) page = m_visitedPage;

	const int index = m_tabs->indexOf(page);
	if (index >= 0) m_tabs->setCurrentIndex(index);
}

Qdc::HubBrowser::Tab Qdc::HubBrowser::currentTab() const
{
	QWidget* page = m_tabs->currentWidget();

	if (page == m_favouritesPage) return FavouritesTab;
	if (page == m_visitedPage) return VisitedTab;

	/* Including a page that is not one of ours; see the note on the declaration. */
	return PublicTab;
}

QTabWidget* Qdc::HubBrowser::tabs() const
{
	return m_tabs;
}

void Qdc::HubBrowser::refreshPublicLists(bool force)
{
	if (m_directory) m_directory->refresh(force);
}

void Qdc::HubBrowser::setFilter(const QString& text)
{
	if (m_search->text() != text) m_search->setText(text);

	m_filtered->setFilterFixedString(text.trimmed());

	/* The count in the status line is what is showing, not what was fetched. */
	refreshStatus();
}

QString Qdc::HubBrowser::filter() const
{
	return m_search->text();
}

int Qdc::HubBrowser::publicCount() const
{
	return m_filtered->rowCount();
}

int Qdc::HubBrowser::favouriteCount() const
{
	return hubRows(m_favouriteList);
}

int Qdc::HubBrowser::recentCount() const
{
	return hubRows(m_recentList);
}

void Qdc::HubBrowser::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);

	refreshIfPublic();

	/* The relative times in the recent list and the status line are wrong by
	   however long this has been out of sight. */
	refreshRecent();
	refreshStatus();
}

void Qdc::HubBrowser::refreshIfPublic()
{
	if (currentTab() != PublicTab) return;

	/*
	 * Every time, because what that costs is decided inside the directory: it
	 * fetches a source whose cache has gone stale and leaves the rest alone, so
	 * this is a request to be current rather than a request to download.
	 */
	refreshPublicLists(false);
}

void Qdc::HubBrowser::refreshPublic()
{
	const QString address = selectedPublicAddress();

	/*
	 * Sorted again after the rows are replaced, and the header ignored while they
	 * are: it puts its indicator back on the first column when the model resets, and
	 * the view sorts the table to match - so a list arriving left it in an order
	 * nobody asked for. See m_sortColumn.
	 */
	m_replacingRows = true;

	m_model->setHubs(m_directory ? m_directory->hubs() : QVector<HubListEntry>());
	m_table->sortByColumn(m_sortColumn, m_sortOrder);

	m_replacingRows = false;

	/*
	 * The selection is by row and the rows have all just been replaced, so it is
	 * put back by address - a list arriving while somebody was reading it should
	 * not move what they had picked out.
	 */
	if (!address.isEmpty())
	{
		for (int row = 0; row < m_filtered->rowCount(); ++row)
		{
			const QModelIndex index = m_filtered->index(row, HubListModel::NameColumn);
			if (index.data(HubListModel::AddressRole).toString() != address) continue;

			m_table->setCurrentIndex(index);
			break;
		}
	}

	followPublicSelection();
}

void Qdc::HubBrowser::refreshStatus()
{
	if (!m_directory)
	{
		m_status->setText(tr("There is no hub list."));
		m_status->setToolTip(QString());
		return;
	}

	const QList<HubListSource> sources = m_directory->sources();

	int failed = 0;
	QStringList detail;

	for (const HubListSource& source : sources)
	{
		if (!source.error.isEmpty())
		{
			failed++;
			detail.append(tr("%1 — %2").arg(source.url, source.error));
			continue;
		}

		if (source.fetching)
		{
			detail.append(tr("%1 — fetching").arg(source.url));
			continue;
		}

		detail.append(source.fetched.isValid()
			? tr("%1 — %n hub(s), %2", nullptr, source.hubs)
				.arg(source.url, format_when(source.fetched))
			: tr("%1 — not fetched yet").arg(source.url));
	}

	/* Which lists said what, for somebody wondering why a hub is missing. */
	m_status->setToolTip(detail.join(QStringLiteral("\n")));

	if (sources.isEmpty())
	{
		m_status->setText(tr("No hub lists are configured. Add them under "
		                     "[Hublists] in quickdc.conf."));
		return;
	}

	if (m_directory->isFetching())
	{
		m_status->setText(tr("Fetching the hub lists…"));
		return;
	}

	QStringList parts;

	const int shown = publicCount();
	const int total = m_directory->hubCount();

	if (total == 0)
	{
		parts.append(tr("No hubs yet"));
	}
	else if (shown == total)
	{
		parts.append(tr("%n hub(s)", nullptr, total));
	}
	else
	{
		/* What the search left, and what it left them out of. */
		parts.append(tr("%1 of %n hub(s)", nullptr, total).arg(shown));
	}

	parts.append(tr("%n list(s)", nullptr, static_cast<int>(sources.size())));

	const QDateTime updated = m_directory->lastUpdated();
	if (updated.isValid()) parts.append(tr("updated %1").arg(format_when(updated)));

	if (failed > 0) parts.append(tr("%n list(s) could not be fetched", nullptr, failed));

	m_status->setText(parts.join(QStringLiteral(" · ")));
}

void Qdc::HubBrowser::refreshFavourites()
{
	const QString selected = selectedAddress(m_favouriteList);

	m_favouriteList->clear();

	const QStringList urls = m_favourites ? m_favourites->urls() : QStringList();

	/*
	 * A favourite is stored as an address and nothing else, so a name for one can
	 * only come from what the hub called itself while it was last visited. Read
	 * once for the whole list rather than searched per row.
	 */
	QHash<QString, QString> names;
	if (m_recent)
	{
		for (const RecentHub& entry : m_recent->entries())
		{
			if (!entry.name.isEmpty()) names.insert(entry.url, entry.name);
		}
	}

	for (const QString& url : urls)
	{
		const QString name = names.value(url);

		QListWidgetItem* item = new QListWidgetItem(
			name.isEmpty() ? url : tr("%1  —  %2").arg(name, url), m_favouriteList);

		item->setData(AddressRole, url);
		item->setToolTip(url);

		if (url == selected) m_favouriteList->setCurrentItem(item);
	}

	if (urls.isEmpty())
	{
		explain(m_favouriteList, tr(
			"None yet. The star in a hub's header keeps it here, as does Bookmark on "
			"a hub in the public list."));
	}

	followFavouriteSelection();
}

void Qdc::HubBrowser::refreshRecent()
{
	const QString selected = selectedAddress(m_recentList);

	m_recentList->clear();

	const QList<RecentHub> entries = m_recent ? m_recent->entries()
	                                          : QList<RecentHub>();

	for (const RecentHub& entry : entries)
	{
		const QString label = entry.name.isEmpty()
			? entry.url : tr("%1  —  %2").arg(entry.name, entry.url);

		QListWidgetItem* item = new QListWidgetItem(
			tr("%1  ·  %2").arg(label, format_when(entry.visited)), m_recentList);

		item->setData(AddressRole, entry.url);
		item->setToolTip(entry.url);

		if (entry.url == selected) m_recentList->setCurrentItem(item);
	}

	if (entries.isEmpty())
		explain(m_recentList, tr("No hubs have been visited yet."));

	m_clearRecent->setEnabled(!entries.isEmpty() && m_recent);

	followRecentSelection();
}

void Qdc::HubBrowser::explain(QListWidget* list, const QString& why)
{
	QListWidgetItem* item = new QListWidgetItem(why, list);

	/* No flags at all: not selectable, not enabled, so it is read as the note it is
	   and the buttons stay disabled. */
	item->setFlags(Qt::NoItemFlags);
}

int Qdc::HubBrowser::hubRows(const QListWidget* list)
{
	int rows = 0;

	for (int n = 0; n < list->count(); ++n)
	{
		if (!row_address(list->item(n)).isEmpty()) rows++;
	}

	return rows;
}

QString Qdc::HubBrowser::selectedPublicAddress() const
{
	const QModelIndex index = m_table->currentIndex();
	if (!index.isValid()) return QString();

	return index.data(HubListModel::AddressRole).toString();
}

QString Qdc::HubBrowser::selectedAddress(const QListWidget* list)
{
	return row_address(list->currentItem());
}

void Qdc::HubBrowser::toggleBookmark()
{
	const QString address = selectedPublicAddress();
	if (!m_favourites || address.isEmpty()) return;

	m_favourites->toggle(address);
}

void Qdc::HubBrowser::followPublicSelection()
{
	const QString address = selectedPublicAddress();
	const bool chosen = !address.isEmpty();

	m_openPublic->setEnabled(chosen);
	m_bookmark->setEnabled(chosen && m_favourites);

	/*
	 * The button says which way it would go rather than being two buttons, one of
	 * which is always the wrong one to press.
	 */
	const bool kept = chosen && m_favourites && m_favourites->contains(address);
	m_bookmark->setText(kept ? tr("Unbookmark") : tr("Bookmark"));
}

void Qdc::HubBrowser::followFavouriteSelection()
{
	/* By address rather than by row, so that the row explaining an empty list does
	   not count as something to connect to. */
	const bool chosen = !selectedAddress(m_favouriteList).isEmpty();

	m_openFavourite->setEnabled(chosen);
	m_unbookmark->setEnabled(chosen && m_favourites);
}

void Qdc::HubBrowser::followRecentSelection()
{
	const bool chosen = !selectedAddress(m_recentList).isEmpty();

	m_openRecent->setEnabled(chosen);
	m_forget->setEnabled(chosen && m_recent);
}

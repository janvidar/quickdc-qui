/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "searchview.h"

#include "glyphs.h"
#include "searchmodel.h"
#include "theme.h"

#include <core/configbridge.h>
#include <core/filelistbridge.h>
#include <core/searchbridge.h>
#include <core/transferbridge.h>

#include <QComboBox>
#include <QCompleter>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTabBar>
#include <QToolButton>
#include <QSpinBox>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStringListModel>
#include <QTabWidget>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

const QString DOWNLOADS = QStringLiteral("Downloads");
const QString DEFAULT_DOWNLOAD_DIR = QStringLiteral("~/.quickdc/downloads");

/** A megabyte in the size filter is a mebibyte, as everywhere else here. */
constexpr quint64 MIB = 1024 * 1024;

/** How much of a query is kept on a tab before it is elided. */
constexpr int TAB_LABEL_MAX = 22;

/** The close cross on a tab, in logical pixels. */
constexpr int CLOSE_GLYPH = 11;

/**
 * The button that closes a search's tab.
 *
 * Built here rather than left to setTabsClosable(), which asks the style for its
 * standard close pixmap - and the Fusion style this application forces draws that as
 * a red cross that belongs to no theme here. The same painted glyph as the one on a
 * conversation header instead, taking its ink from the palette like everything else.
 */
QToolButton* tab_close_button(QWidget* parent)
{
	QToolButton* button = new QToolButton(parent);

	const qreal ratio = parent->devicePixelRatioF();

	QPixmap pixmap(QSize(CLOSE_GLYPH, CLOSE_GLYPH) * ratio);
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(Qt::transparent);

	QPainter painter(&pixmap);
	Qdc::paint_glyph(painter, Qdc::Glyph::Close,
	                 QRectF(0, 0, CLOSE_GLYPH, CLOSE_GLYPH),
	                 Qdc::theme().contentMuted);
	painter.end();

	button->setIcon(QIcon(pixmap));
	button->setIconSize(QSize(CLOSE_GLYPH, CLOSE_GLYPH));
	button->setAutoRaise(true);
	button->setToolTip(Qdc::SearchView::tr("Close this search"));

	return button;
}

/**
 * How wide the columns beside the name start out.
 *
 * Sized rather than fitted to their contents, because the contents arrive over the
 * following minute and a column that resizes itself as replies land moves the names
 * sideways under the pointer while somebody is reading them.
 *
 * Named for the column rather than ending in _WIDTH, because glibc defines
 * SIZE_WIDTH as a macro - and a macro replaces the name wherever it is written,
 * so the declaration itself stops parsing.
 */
constexpr int COLUMN_SIZE = 90;
constexpr int COLUMN_USERS = 60;
constexpr int COLUMN_SLOTS = 60;
constexpr int COLUMN_TYPE = 90;
constexpr int COLUMN_HUB = 200;

}

Qdc::SearchView::SearchView(SearchBridge* search, TransferBridge* transfers,
                            FilelistBridge* filelists, ConfigBridge* config,
                            QWidget* parent)
	: View(parent)
	, m_search(search)
	, m_transfers(transfers)
	, m_filelists(filelists)
	, m_config(config)
	, m_terms(new QLineEdit(this))
	, m_category(new QComboBox(this))
	, m_sizePolicy(new QComboBox(this))
	, m_size(new QSpinBox(this))
	, m_go(new QPushButton(tr("Search"), this))
	, m_pages(new QStackedWidget(this))
	, m_empty(new QLabel(this))
	, m_tabs(new QTabWidget(this))
	, m_history(new QStringListModel(this))
{
	m_empty->setObjectName(QStringLiteral("mutedText"));
	m_empty->setAlignment(Qt::AlignCenter);
	m_empty->setWordWrap(true);
	m_empty->setText(tr("Type what you are looking for.\n\n"
	                    "Every hub you are logged in to is asked at once, and answers "
	                    "arrive as whoever has the file gets round to replying."));

	m_tabs->setDocumentMode(true);
	m_tabs->setTabsClosable(true);

	m_pages->addWidget(m_empty);
	m_pages->addWidget(m_tabs);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(buildQueryBar());
	layout->addWidget(m_pages, 1);

	connect(m_tabs, &QTabWidget::tabCloseRequested, this, &SearchView::closeSearch);

	if (!m_search)
	{
		m_terms->setEnabled(false);
		m_category->setEnabled(false);
		m_sizePolicy->setEnabled(false);
		m_size->setEnabled(false);
		m_go->setEnabled(false);
		m_empty->setText(tr("Searching is not available: there is no core to ask."));
		return;
	}

	connect(m_search, &SearchBridge::searchSent,
	        this, [this](const QString& token, int hubsAsked) {
		if (Search* search = searchForToken(token))
		{
			search->hubsAsked = hubsAsked;
			refreshSummary(*search);
		}
	});
}

Qdc::SearchView::~SearchView()
{
	/*
	 * The widgets of each search go with this one, being children of it, but the
	 * record holding them together is not a QObject and has to be let go by hand.
	 */
	qDeleteAll(m_searches);
	m_searches.clear();
}

QWidget* Qdc::SearchView::buildQueryBar()
{
	QWidget* bar = new QWidget(this);

	m_terms->setPlaceholderText(tr("What are you looking for?"));
	m_terms->setClearButtonEnabled(true);

	/*
	 * Over the queries run this session rather than a stored list: what somebody
	 * looked for is a record of what they were after, and keeping it across restarts
	 * is a decision for whoever wants that feature, not a side effect of a combo box.
	 */
	QCompleter* completer = new QCompleter(m_history, m_terms);
	completer->setCaseSensitivity(Qt::CaseInsensitive);
	completer->setCompletionMode(QCompleter::PopupCompletion);
	m_terms->setCompleter(completer);

	m_category->addItem(tr("Anything"), int(SearchCategory::Any));
	m_category->addItem(tr("Audio"), int(SearchCategory::Audio));
	m_category->addItem(tr("Video"), int(SearchCategory::Video));
	m_category->addItem(tr("Pictures"), int(SearchCategory::Picture));
	m_category->addItem(tr("Documents"), int(SearchCategory::Document));
	m_category->addItem(tr("Archives"), int(SearchCategory::Archive));

	m_sizePolicy->addItem(tr("Any size"), int(SearchSize::Any));
	m_sizePolicy->addItem(tr("At least"), int(SearchSize::AtLeast));
	m_sizePolicy->addItem(tr("At most"), int(SearchSize::AtMost));

	m_size->setRange(1, 1024 * 1024);
	m_size->setSuffix(tr(" MiB"));
	m_size->setValue(1);
	m_size->setEnabled(false);

	connect(m_sizePolicy, &QComboBox::currentIndexChanged, this, [this] {
		m_size->setEnabled(m_sizePolicy->currentData().toInt() != int(SearchSize::Any));
	});

	m_go->setObjectName(QStringLiteral("primaryButton"));

	connect(m_go, &QPushButton::clicked, this, &SearchView::startSearch);
	connect(m_terms, &QLineEdit::returnPressed, this, &SearchView::startSearch);

	QHBoxLayout* layout = new QHBoxLayout(bar);
	layout->setContentsMargins(16, 12, 16, 12);
	layout->setSpacing(8);
	layout->addWidget(m_terms, 1);
	layout->addWidget(m_category);
	layout->addWidget(m_sizePolicy);
	layout->addWidget(m_size);
	layout->addWidget(m_go);

	return bar;
}

QString Qdc::SearchView::title() const
{
	return tr("Search");
}

Qdc::Glyph Qdc::SearchView::glyph() const
{
	return Glyph::Search;
}

void Qdc::SearchView::focusQuery()
{
	m_terms->setFocus();
	m_terms->selectAll();
}

void Qdc::SearchView::startSearch()
{
	if (!m_search) return;

	const QString terms = m_terms->text().trimmed();
	if (terms.isEmpty()) return;

	SearchQuery query;
	query.terms = terms;
	query.category = SearchCategory(m_category->currentData().toInt());
	query.sizePolicy = SearchSize(m_sizePolicy->currentData().toInt());

	if (query.sizePolicy != SearchSize::Any)
		query.size = static_cast<quint64>(m_size->value()) * MIB;

	const QString token = m_search->start(query);

	if (token.isEmpty())
	{
		QMessageBox::information(this, tr("Search"),
			tr("The search could not be sent: the QuickDC core is not running."));
		return;
	}

	Search* search = new Search;
	search->token = token;
	search->terms = terms;

	/*
	 * The words a name must carry, for placing a reply that arrives without a
	 * token. An exclusion is not among them - a reply is being recognised here,
	 * not filtered.
	 */
	const QStringList words = terms.split(QRegularExpression(QStringLiteral("\\s+")),
	                                      Qt::SkipEmptyParts);

	for (const QString& word : words)
	{
		if (!word.startsWith(QLatin1Char('-')))
			search->words.append(word.toLower());
	}

	search->model = new SearchModel(this);

	search->tree = new QTreeView;
	search->tree->setModel(search->model);
	search->tree->setRootIsDecorated(true);
	search->tree->setAllColumnsShowFocus(true);
	search->tree->setSelectionBehavior(QAbstractItemView::SelectRows);
	search->tree->setContextMenuPolicy(Qt::CustomContextMenu);
	search->tree->setEditTriggers(QAbstractItemView::NoEditTriggers);

	QHeaderView* header = search->tree->header();
	header->setSectionResizeMode(SearchModel::NameColumn, QHeaderView::Stretch);
	header->setSectionResizeMode(SearchModel::SizeColumn, QHeaderView::Interactive);
	header->setSectionResizeMode(SearchModel::UsersColumn, QHeaderView::Interactive);
	header->setSectionResizeMode(SearchModel::SlotsColumn, QHeaderView::Interactive);
	header->setSectionResizeMode(SearchModel::TypeColumn, QHeaderView::Interactive);
	header->setSectionResizeMode(SearchModel::HubColumn, QHeaderView::Interactive);

	search->tree->setColumnWidth(SearchModel::SizeColumn, COLUMN_SIZE);
	search->tree->setColumnWidth(SearchModel::UsersColumn, COLUMN_USERS);
	search->tree->setColumnWidth(SearchModel::SlotsColumn, COLUMN_SLOTS);
	search->tree->setColumnWidth(SearchModel::TypeColumn, COLUMN_TYPE);
	search->tree->setColumnWidth(SearchModel::HubColumn, COLUMN_HUB);

	connect(search->tree, &QTreeView::customContextMenuRequested,
	        this, &SearchView::showResultMenu);

	connect(search->tree, &QTreeView::doubleClicked,
	        this, [this, search](const QModelIndex& index) {
		/* A file with more than one source expands rather than downloading. */
		if (!index.parent().isValid() && search->model->rowCount(index) > 1)
		{
			search->tree->setExpanded(index, !search->tree->isExpanded(index));
			return;
		}

		const SearchHit hit = search->model->hitAt(index);

		/* A folder has nothing to queue, so opening it means browsing whoever has it. */
		if (hit.directory)
			browse(hit);
		else
			download(hit, false);
	});

	search->summary = new QLabel;
	search->summary->setObjectName(QStringLiteral("mutedText"));
	search->summary->setContentsMargins(8, 4, 8, 6);

	search->page = new QWidget;

	QVBoxLayout* page = new QVBoxLayout(search->page);
	page->setContentsMargins(0, 0, 0, 0);
	page->setSpacing(0);
	page->addWidget(search->tree, 1);
	page->addWidget(search->summary);

	refreshSummary(*search);

	m_searches.append(search);

	QString label = terms;
	if (label.size() > TAB_LABEL_MAX)
		label = label.left(TAB_LABEL_MAX - 1) + QStringLiteral("…");

	const int tab = m_tabs->addTab(search->page, label);
	m_tabs->setTabToolTip(tab, terms);

	QToolButton* close = tab_close_button(m_tabs->tabBar());
	connect(close, &QToolButton::clicked, this, [this, page = search->page] {
		/* By page rather than by index: a tab to the left may have gone by then. */
		closeSearch(m_tabs->indexOf(page));
	});

	m_tabs->tabBar()->setTabButton(tab, QTabBar::RightSide, close);
	m_tabs->setCurrentIndex(tab);

	m_pages->setCurrentWidget(m_tabs);

	/* Newest first, and once however often the same thing is looked for. */
	QStringList history = m_history->stringList();
	history.removeAll(terms);
	history.prepend(terms);
	m_history->setStringList(history);
}

void Qdc::SearchView::closeSearch(int index)
{
	QWidget* page = m_tabs->widget(index);
	if (!page) return;

	for (qsizetype n = 0; n < m_searches.size(); ++n)
	{
		if (m_searches.at(n)->page != page) continue;

		Search* search = m_searches.takeAt(n);

		/*
		 * The model is a child of this view rather than of the page, so it does not
		 * go with the widgets and is deleted here. Replies for this search go on
		 * arriving and are dropped once the token matches nothing.
		 */
		delete search->model;
		delete search;
		break;
	}

	m_tabs->removeTab(index);
	delete page;

	if (m_tabs->count() == 0) m_pages->setCurrentWidget(m_empty);
}

void Qdc::SearchView::addHit(const SearchHit& hit)
{
	addHits(QVector<SearchHit>{hit});
}

void Qdc::SearchView::addHits(const QVector<SearchHit>& hits)
{
	/*
	 * Sorted first and added afterwards, so that each search hears once about
	 * everything in the batch that was for it. Which search a reply answers is
	 * usually its token; on NMDC there is none and it has to be matched by name,
	 * which is the expensive half and is why the answer is remembered per name
	 * rather than worked out again for every reply carrying it.
	 */
	QHash<Search*, QVector<SearchHit>> sorted;
	QHash<QString, Search*> matched;

	for (const SearchHit& hit : hits)
	{
		Search* search = nullptr;

		if (!hit.token.isEmpty())
		{
			search = searchForToken(hit.token);
		}
		else
		{
			const auto seen = matched.constFind(hit.name);

			if (seen != matched.constEnd())
			{
				search = seen.value();
			}
			else
			{
				search = searchMatching(hit.name);
				matched.insert(hit.name, search);
			}
		}

		if (search) sorted[search].append(hit);
	}

	for (auto one = sorted.constBegin(); one != sorted.constEnd(); ++one)
	{
		one.key()->model->add(one.value());
		refreshSummary(*one.key());
	}
}

Qdc::SearchView::Search* Qdc::SearchView::searchForToken(const QString& token)
{
	for (Search* search : m_searches)
	{
		if (search->token == token) return search;
	}

	return nullptr;
}

Qdc::SearchView::Search* Qdc::SearchView::searchMatching(const QString& filename)
{
	const QString name = filename.toLower();

	for (auto n = m_searches.crbegin(); n != m_searches.crend(); ++n)
	{
		Search* search = *n;

		bool matches = !search->words.isEmpty();

		for (const QString& word : search->words)
		{
			if (!name.contains(word))
			{
				matches = false;
				break;
			}
		}

		if (matches) return search;
	}

	return nullptr;
}

Qdc::SearchView::Search* Qdc::SearchView::currentSearch()
{
	QWidget* page = m_tabs->currentWidget();
	if (!page) return nullptr;

	for (Search* search : m_searches)
	{
		if (search->page == page) return search;
	}

	return nullptr;
}

void Qdc::SearchView::refreshSummary(Search& search)
{
	const int files = search.model->fileCount();

	QString asked;

	/*
	 * Every plural written out rather than left to tr()'s %n, for the reason given
	 * in ConnectionBar::reason(): with no translation loaded the source string is
	 * returned as it stands, so "1 file(s)" is what would be read - and this line is
	 * under the results the whole time a search is open.
	 */
	if (search.hubsAsked < 0)
		asked = tr("Asking the hubs…");
	else if (search.hubsAsked == 0)
		asked = tr("No hub was asked — nothing is connected.");
	else if (search.hubsAsked == 1)
		asked = tr("Asked 1 hub.");
	else
		asked = tr("Asked %1 hubs.").arg(search.hubsAsked);

	if (files == 0)
	{
		search.summary->setText(search.hubsAsked == 0
			? asked
			: tr("%1 No answers yet.").arg(asked));
		return;
	}

	const int peers = search.model->peerCount();

	const QString found = files == 1 ? tr("1 file") : tr("%1 files").arg(files);
	const QString from = peers == 1 ? tr("1 peer") : tr("%1 peers").arg(peers);

	search.summary->setText(tr("%1 %2 from %3.").arg(asked, found, from));
}

void Qdc::SearchView::showResultMenu(const QPoint& where)
{
	Search* search = currentSearch();
	if (!search) return;

	const QModelIndex index = search->tree->indexAt(where);
	if (!index.isValid()) return;

	const SearchHit hit = search->model->hitAt(index);
	if (hit.name.isEmpty()) return;

	QMenu menu(this);

	QAction* get = menu.addAction(tr("Download"));
	QAction* getTo = menu.addAction(tr("Download to…"));
	menu.addSeparator();
	QAction* files = menu.addAction(tr("Browse %1's files")
		.arg(hit.user.nick.isEmpty() ? tr("this user") : hit.user.nick));

	/*
	 * A folder cannot be queued: the queue holds files. Left in the menu and
	 * disabled with the reason on it, rather than absent, so that the answer to
	 * "why can I not download this" is where the question is asked.
	 */
	get->setEnabled(m_transfers != nullptr && !hit.directory);
	getTo->setEnabled(m_transfers != nullptr && !hit.directory);

	if (hit.directory)
	{
		const QString why = tr("A folder cannot be downloaded. Browse the user's "
		                       "files and take what you want from it.");
		get->setToolTip(why);
		getTo->setToolTip(why);
	}

	files->setEnabled(m_filelists != nullptr && !hit.user.sourceId.isEmpty());

	QAction* chosen = menu.exec(search->tree->viewport()->mapToGlobal(where));

	if (chosen == get)        download(hit, false);
	else if (chosen == getTo) download(hit, true);
	else if (chosen == files) browse(hit);
}

void Qdc::SearchView::download(const SearchHit& hit, bool chooseFolder)
{
	if (!m_transfers || hit.name.isEmpty()) return;

	/* The name out of the peer's path; their folders are not ours to reproduce. */
	const QString name = QFileInfo(hit.name).fileName();

	if (!chooseFolder)
	{
		m_transfers->enqueue(downloadDirectory() + QLatin1Char('/') + name, hit);
		return;
	}

	/*
	 * A turn later, as elsewhere: the chooser is modal and opening it from inside
	 * the menu's own event leaves the menu on screen behind it.
	 */
	QTimer::singleShot(0, this, [this, hit, name] {
		const QString folder = QFileDialog::getExistingDirectory(this,
			tr("Download to"), downloadDirectory(),
			QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);

		if (folder.isEmpty()) return;

		m_transfers->enqueue(folder + QLatin1Char('/') + name, hit);
	});
}

void Qdc::SearchView::browse(const SearchHit& hit)
{
	if (!m_filelists || hit.hubUrl.isEmpty() || hit.user.sourceId.isEmpty()) return;

	m_filelists->request(hit.hubUrl, hit.user.sourceId);

	/*
	 * Said here because the list opens somewhere else entirely - in the hub's own
	 * conversation, minutes later if the peer has to be retried - and without a word
	 * the menu item looks as though it did nothing.
	 */
	QMessageBox::information(this, tr("Browse files"),
		tr("Asked %1 for their file list. It opens beside their conversation on %2 "
		   "once it arrives, which can take a while.")
			.arg(hit.user.nick.isEmpty() ? tr("the user") : hit.user.nick, hit.hubUrl));
}

QString Qdc::SearchView::downloadDirectory() const
{
	if (!m_config) return DEFAULT_DOWNLOAD_DIR;

	/*
	 * Read now rather than kept: this is one blocking trip to the core thread per
	 * download somebody asked for by hand, where HubsView caches the same setting
	 * because it queues a file for every picture that arrives in chat.
	 */
	const QString directory = m_config->getString(ConfigStore::Application, DOWNLOADS,
	                                              QStringLiteral("Directory")).trimmed();

	return directory.isEmpty() ? DEFAULT_DOWNLOAD_DIR : directory;
}

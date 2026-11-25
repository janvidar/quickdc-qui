/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_SEARCHVIEW_H
#define HAVE_QUICKDC_QUI_SEARCHVIEW_H

#include "view.h"

#include <core/coretypes.h>

#include <QPointer>
#include <QList>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QModelIndex;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QStringListModel;
class QTabWidget;
class QTreeView;

namespace Qdc {

class ConfigBridge;
class FilelistBridge;
class SearchBridge;
class SearchModel;
class TransferBridge;

/**
 * Looking for a file across every hub at once.
 *
 * One tab per search, because a search on this network is not a request that
 * finishes: replies arrive from whoever happens to be online and willing, they keep
 * arriving for as long as they keep arriving, and nothing says when there will be no
 * more. Two searches can therefore be usefully open at the same time, and a tab is
 * what keeps their answers apart.
 *
 * What keeps them apart is the core: it numbers each search, decides which one a
 * reply answers, and drops one that answers none. Each tab keeps the number its
 * search was given, and closing it tells the core to forget the search.
 *
 * Replies are not asked for here. They arrive on whichever hub they came in on and
 * are handed to addHit() by the window, which is the only thing that can see both
 * this and the hubs.
 */
class SearchView final : public View {
	Q_OBJECT

public:
	/**
	 * @param search where a query goes. May be null, in which case the form is
	 *        shown disabled.
	 * @param transfers used to queue a result; may be null.
	 * @param filelists used to browse the peer that answered; may be null.
	 * @param config read for the download folder; may be null.
	 */
	SearchView(SearchBridge* search, TransferBridge* transfers,
	           FilelistBridge* filelists, ConfigBridge* config,
	           QWidget* parent = nullptr);

	~SearchView() override;

	QString title() const override;
	Glyph glyph() const override;

public slots:
	/** File one reply under the search the core says it answers, if its tab is open. */
	void addHit(const Qdc::SearchHit& hit);

	/**
	 * The replies that arrived together, filed under the searches they answer.
	 *
	 * One insertion and one summary per search per batch, rather than per reply:
	 * the model transaction and the count under the results are what a burst of
	 * hundreds of replies used to cost the window, once each.
	 */
	void addHits(const QVector<Qdc::SearchHit>& hits);

	/** Put the cursor in the query field. */
	void focusQuery();

private:
	/** One search: its tab, its answers, and the numbers that identify it. */
	struct Search {
		quint64 request = 0;

		/** The core's number for it, once searchSent() has said; 0 until then. */
		quint64 search = 0;

		QString terms;

		QWidget* page = nullptr;
		QTreeView* tree = nullptr;
		SearchModel* model = nullptr;
		QLabel* summary = nullptr;

		/** What searchSent() reported, or -1 until it has. */
		int hubsAsked = -1;
	};

	QWidget* buildQueryBar();

	/** Read the form, ask the bridge, and open a tab for the answers. */
	void startSearch();

	void closeSearch(int index);

	/** Update the line under a tab's table: hubs asked, files, peers. */
	void refreshSummary(Search& search);

	Search* searchNumbered(quint64 search);

	Search* currentSearch();

	void showResultMenu(const QPoint& where);
	void download(const Qdc::SearchHit& hit, bool chooseFolder);
	void browse(const Qdc::SearchHit& hit);

	/** Where a download goes, as the configuration has it. */
	QString downloadDirectory() const;

	QPointer<SearchBridge> m_search;
	TransferBridge* m_transfers;
	FilelistBridge* m_filelists;
	ConfigBridge* m_config;

	QLineEdit* m_terms;
	QComboBox* m_category;
	QComboBox* m_sizePolicy;
	QSpinBox* m_size;
	QPushButton* m_go;

	/** The query bar over either the tabs or the nothing-yet page. */
	QStackedWidget* m_pages;
	QLabel* m_empty;
	QTabWidget* m_tabs;

	/**
	 * Every query run this session, newest first, offered as completions.
	 *
	 * A model rather than a plain list because a QCompleter takes its list once, at
	 * construction, and this one grows with every search.
	 */
	QStringListModel* m_history;

	QList<Search*> m_searches;
};

}

#endif // HAVE_QUICKDC_QUI_SEARCHVIEW_H

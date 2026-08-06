/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_SEARCHVIEW_H
#define HAVE_QUICKDC_QUI_SEARCHVIEW_H

#include "view.h"

#include <core/coretypes.h>

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
 * What keeps them apart underneath is the token. It goes out with the request, comes
 * back on every ADC reply, and each tab keeps the one it was opened with. NMDC has
 * nowhere to carry a token, so a reply without one is placed by what it contains
 * instead; see routing in addHit().
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
	/**
	 * File one reply under the search it answers.
	 *
	 * Dropped, and deliberately, when it belongs to no open tab: a search whose tab
	 * has been closed is one nobody is waiting for, and peers go on answering it for
	 * a while afterwards.
	 */
	void addHit(const Qdc::SearchHit& hit);

	/**
	 * The replies that arrived together, sorted into the searches they answer.
	 *
	 * One insertion and one summary per search per batch, rather than per reply:
	 * the model transaction and the count under the results are what a burst of
	 * hundreds of replies used to cost the window, once each.
	 */
	void addHits(const QVector<Qdc::SearchHit>& hits);

	/** Put the cursor in the query field. */
	void focusQuery();

private:
	/** One search: its tab, its answers, and the token that identifies it. */
	struct Search {
		QString token;

		/** As typed, for the tab's label and for placing an untokened reply. */
		QString terms;

		/** The words of 'terms' that a name has to carry, lower cased. */
		QStringList words;

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

	Search* searchForToken(const QString& token);

	/**
	 * The tab an untokened reply belongs to, newest first, or null.
	 *
	 * Newest first because a reply arriving now is far more likely to answer the
	 * search just started than one from ten minutes ago, and a name matching both
	 * is genuinely ambiguous - there is nothing in the reply to settle it with.
	 */
	Search* searchMatching(const QString& filename);

	Search* currentSearch();

	void showResultMenu(const QPoint& where);
	void download(const Qdc::SearchHit& hit, bool chooseFolder);
	void browse(const Qdc::SearchHit& hit);

	/** Where a download goes, as the configuration has it. */
	QString downloadDirectory() const;

	SearchBridge* m_search;
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

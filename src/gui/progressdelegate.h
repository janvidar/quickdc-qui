/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_PROGRESSDELEGATE_H
#define HAVE_QUICKDC_QUI_PROGRESSDELEGATE_H

#include <QStyledItemDelegate>

namespace Qdc {

/**
 * A progress bar in a table cell.
 *
 * Drawn rather than assembled from a QProgressBar per row: a view creates and
 * destroys editors as rows scroll, and a widget per row would be both slower and
 * unable to follow the row's selected state.
 *
 * The fraction comes from a role the model supplies, so the same delegate serves
 * the queue and the active transfers without knowing about either.
 */
class ProgressDelegate final : public QStyledItemDelegate {
	Q_OBJECT

public:
	/** @param fractionRole a role returning a double between 0 and 1. */
	ProgressDelegate(int fractionRole, QObject* parent = nullptr);

	void paint(QPainter* painter, const QStyleOptionViewItem& option,
	           const QModelIndex& index) const override;

	QSize sizeHint(const QStyleOptionViewItem& option,
	               const QModelIndex& index) const override;

private:
	int m_fractionRole;
};

}

#endif // HAVE_QUICKDC_QUI_PROGRESSDELEGATE_H

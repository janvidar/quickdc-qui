/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_CHATFEED_H
#define HAVE_QUICKDC_QUI_CHATFEED_H

#include <QHash>
#include <QListView>
#include <QStyledItemDelegate>
#include <QUrl>

#include "richtext.h"

#include <functional>

namespace Qdc {

class ChatModel;

/**
 * Lays out one chat line: a timestamp, who said it, and what they said.
 *
 * Heights vary, because the text wraps. That makes the width the delegate is
 * given part of its own answer, so the computed height is cached against the
 * width it was computed for and thrown away when the viewport changes size -
 * without the cache, QListView measuring every row on each layout pass would
 * re-wrap the entire backlog.
 */
class ChatDelegate final : public QStyledItemDelegate {
	Q_OBJECT

public:
	explicit ChatDelegate(QObject* parent = nullptr);

	void paint(QPainter* painter, const QStyleOptionViewItem& option,
	           const QModelIndex& index) const override;

	QSize sizeHint(const QStyleOptionViewItem& option,
	               const QModelIndex& index) const override;

	/** Called by the feed when the viewport width changes, or when rows move. */
	void invalidateLayout();

	/**
	 * Who to ask whether a picture named in a message is on this machine.
	 *
	 * Without one, media renders as a chip to click. The feed does not know what has
	 * been downloaded and must not guess, so this is supplied from above.
	 */
	void setMediaResolver(std::function<RichText::LocalMedia(const QString& tth)> media);

	/**
	 * The link under 'point', or empty if there is none there.
	 *
	 * 'point' is in the coordinates the row was drawn in, so the caller passes what it
	 * got from the mouse and the offset the delegate painted at is applied here.
	 */
	QString anchorAt(const QModelIndex& index, const QStyleOptionViewItem& option,
	                 const QPoint& point) const;

private:
	/**
	 * The rich text for one row, already wrapped to 'width'.
	 *
	 * Built once and reused by both paint() and sizeHint(), because those two
	 * have to agree exactly or rows overlap.
	 */
	class Layout;

	const Layout& layoutFor(const QModelIndex& index, int width,
	                        const QStyleOptionViewItem& option) const;

	/** Keyed by row. Cleared whenever the width it was built for changes. */
	mutable QHash<int, Layout*> m_cache;
	mutable int m_cachedWidth = -1;

	std::function<RichText::LocalMedia(const QString& tth)> m_media;
};

/**
 * The message list.
 *
 * Follows the tail while the view is already at the bottom, and leaves the
 * position alone when it is not - so a new message does not yank the view away
 * from someone reading back through the history.
 */
class ChatFeed final : public QListView {
	Q_OBJECT

public:
	explicit ChatFeed(QWidget* parent = nullptr);

	void setModel(QAbstractItemModel* model) override;

	/** Whether the view is close enough to the bottom to be following it. */
	bool isAtBottom() const;

	/** Passed to the delegate; see ChatDelegate::setMediaResolver(). */
	void setMediaResolver(std::function<RichText::LocalMedia(const QString& tth)> media);

	/** The link under a point in the viewport, or empty. */
	QString anchorAt(const QPoint& point) const;

signals:
	/**
	 * A link in a message was clicked.
	 *
	 * Only ever a scheme RichText::isLinkable() allows. Where it goes is not decided
	 * here: a hub address is opened inside QuickDC and everything else is handed to the
	 * desktop, and only the window above knows how to do the first of those.
	 */
	void linkActivated(const QUrl& url);

protected:
	void resizeEvent(QResizeEvent* event) override;

	void mouseMoveEvent(QMouseEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	void onRowsInserted();

	ChatDelegate* m_delegate;

	/**
	 * Whether the view was at the bottom before the rows arrived. Sampled at
	 * insertion time, because by the time the layout has run the scrollbar has
	 * already moved and the question can no longer be asked.
	 */
	bool m_wasAtBottom = true;

	/**
	 * The link the press landed on.
	 *
	 * A link follows on release over the same one it was pressed on, so that a drag that
	 * happens to start on a link selects text instead of navigating.
	 */
	QString m_pressedAnchor;
};

}

#endif // HAVE_QUICKDC_QUI_CHATFEED_H

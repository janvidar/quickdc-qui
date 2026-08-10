/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_CHATFEED_H
#define HAVE_QUICKDC_QUI_CHATFEED_H

#include <QHash>
#include <QListView>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QUrl>

#include "richtext.h"

#include <core/coretypes.h>

#include <functional>

namespace Qdc {

/**
 * A place in the feed: which row, and how many characters into that row's text.
 *
 * A selection cannot be a pair of character offsets alone, because the feed is a row
 * per message and each row lays its own document out - so a position needs to say
 * which document it is in. Offsets count into that document's plain text, prefix
 * included: somebody who drags from the timestamp means to copy the timestamp.
 */
struct FeedPos {
	int row = -1;
	int offset = 0;

	bool isValid() const { return row >= 0; }
};

bool operator==(const FeedPos& left, const FeedPos& right);
bool operator<(const FeedPos& left, const FeedPos& right);

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

	/**
	 * Which character of this row's text is under 'point'.
	 *
	 * The nearest one rather than only a direct hit, because this answers a drag:
	 * somebody sweeping across a message passes above and below its line as often as
	 * along it, and a position of "nowhere" in the middle of a sweep would make the
	 * selection flicker.
	 */
	int characterAt(const QModelIndex& index, const QStyleOptionViewItem& option,
	                const QPoint& point) const;

	/** The row's text as it is shown, which is what a selection of it copies. */
	QString plainText(const QModelIndex& index,
	                  const QStyleOptionViewItem& option) const;

	/**
	 * The range to draw highlighted, as a pair of positions in the feed.
	 *
	 * Held here rather than asked for per row, because painting is where it is needed
	 * and the delegate is handed one row at a time with nothing to ask.
	 */
	void setSelection(const FeedPos& from, const FeedPos& to);

signals:
	/**
	 * A row has to be drawn again: a picture in it has moved on a frame.
	 *
	 * One row rather than the whole feed, because an animation repaints several times a
	 * second and a backlog of them would otherwise redraw everything each time.
	 */
	void rowNeedsRepaint(int row) const;

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

	/** What to highlight; see setSelection(). Ordered, so from is never after to. */
	FeedPos m_from;
	FeedPos m_to;

	/**
	 * Stops animating what nobody is looking at.
	 *
	 * Every row is measured, so every animation in the backlog exists whether or not it
	 * is on screen. Painting is the only sign that a row is visible, so a row that has
	 * not been painted lately has its animations paused until it is.
	 */
	QTimer* m_idle;
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

	/**
	 * Draw the rows again, because what is on this machine has changed.
	 *
	 * A picture nobody had is a chip; once the file arrives the same message becomes the
	 * picture, and only something outside knows when that happened.
	 */
	void refreshMedia();

	/** The link under a point in the viewport, or empty. */
	QString anchorAt(const QPoint& point) const;

	/**
	 * The text somebody has swept out, rows joined by newlines, or empty.
	 *
	 * What is shown rather than what arrived: markdown has been rendered by the time
	 * it is on screen, and a magnet is a chip naming a file, so copying gives the
	 * words that were being read rather than the markup behind them.
	 */
	QString selectedText() const;

	bool hasSelectedText() const;

public slots:
	/** Put the swept text on the clipboard. Does nothing when there is none. */
	void copySelection();

	/** Every row, end to end. */
	void selectAllText();

	void clearSelection();

signals:
	/**
	 * A link in a message was clicked.
	 *
	 * Only ever a scheme RichText::isLinkable() allows. Where it goes is not decided
	 * here: a hub address is opened inside QuickDC and everything else is handed to the
	 * desktop, and only the window above knows how to do the first of those.
	 */
	void linkActivated(const QUrl& url);

	/**
	 * Somebody's name in the feed was right-clicked.
	 *
	 * Carries the record the message itself held, which is who they were when they
	 * said it. Whoever handles this decides what is still true of them - the name
	 * may belong to somebody who has since left, and a menu is still worth having
	 * for one that does.
	 */
	void userMenuRequested(const Qdc::UserInfo& speaker, const QPoint& globalPos);

protected:
	void resizeEvent(QResizeEvent* event) override;

	/**
	 * A row's colours are laid into its document when it is measured, so a scheme
	 * chosen while the window is open has to throw those documents away.
	 */
	void changeEvent(QEvent* event) override;

	void contextMenuEvent(QContextMenuEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	/** Draw one row again, because a picture in it moved on a frame. */
	void repaintRow(int row);

	/**
	 * Where in the feed a point in the viewport is.
	 *
	 * Clamped to the first and last rows rather than answering "nowhere" above or
	 * below them, so a sweep that runs off the top or the bottom selects as far as it
	 * went instead of stopping where the pointer left the rows.
	 */
	FeedPos positionAt(const QPoint& point) const;

	/** How many characters a row holds. */
	int lengthOf(int row) const;

	/** Hand the ordered pair to the delegate and repaint what it covers. */
	void applySelection();

	void onRowsInserted();

	ChatDelegate* m_delegate;

	/**
	 * Where a sweep began and where it has reached. Either may be the earlier of the
	 * two, because a selection can be dragged upwards.
	 */
	FeedPos m_anchor;
	FeedPos m_cursor;

	/** Whether the button is still down on a sweep that started in this view. */
	bool m_sweeping = false;

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

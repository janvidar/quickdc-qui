/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "chatfeed.h"

#include "chatmodel.h"
#include "richtext.h"
#include "theme.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QImageReader>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStyle>
#include <QTextDocument>
#include <QTextOption>
#include <QtMath>

namespace {

constexpr int MARGIN_X = 12;
constexpr int MARGIN_Y = 4;

/** How near the bottom still counts as following the tail, in pixels. */
constexpr int TAIL_SLACK = 24;

/**
 * A stable colour for a nickname.
 *
 * Hashed rather than assigned, so the same person is the same colour in every
 * conversation and across restarts. The hue is free, but the saturation and
 * lightness are pinned to a band that stays legible on both a light and a dark
 * background - which is why this does not simply take a random QColor.
 */
QColor nick_colour(const QString& nick, bool darkBackground)
{
	const uint hash = qHash(nick, 0x9e3779b9);
	const int hue = static_cast<int>(hash % 360);

	return darkBackground ? QColor::fromHsl(hue, 160, 168)
	                      : QColor::fromHsl(hue, 150, 92);
}

/** The markup colours, taken from the theme rather than mixed here. */
Qdc::RichText::RenderColours markup_colours()
{
	const Qdc::Theme& current = Qdc::theme();

	Qdc::RichText::RenderColours colours;
	colours.code = current.contentText;
	colours.codeBack = current.contentAlt;
	colours.quote = current.contentMuted;
	colours.quoteBar = current.border;
	colours.link = current.accent;

	colours.chip = current.contentText;
	colours.chipBack = current.contentAlt;

	return colours;
}

}

/**
 * One row's wrapped rich text, plus what it was wrapped to.
 *
 * A QTextDocument rather than a hand-rolled QTextLayout: the line has several
 * runs with different colours and weights, and a document is the cheapest way to
 * get wrapping, bidi and font fallback all behaving correctly.
 */
class Qdc::ChatDelegate::Layout {

public:
	Layout(const ChatMessage& message, int width, const QFont& font,
	       const QPalette& palette, bool darkBackground,
	       const std::function<RichText::LocalMedia(const QString&)>& media)
	{
		document.setDefaultFont(font);
		document.setDocumentMargin(0.0);

		QTextOption wrapping = document.defaultTextOption();
		wrapping.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
		document.setDefaultTextOption(wrapping);

		const QColor stamp = palette.color(QPalette::PlaceholderText);
		const QString time = message.received.toString(QStringLiteral("HH:mm"));

		/*
		 * Where the message text starts, measured rather than guessed: the
		 * timestamp is in the body font and the name is bold, so the two have to be
		 * measured with the fonts they are drawn in.
		 */
		QFont speakerFont = font;
		speakerFont.setBold(!message.action);
		speakerFont.setItalic(message.action);

		RichText::RenderOptions options;
		options.colours = markup_colours();

		options.prefix = QStringLiteral("<span style=\"color:%1\">%2</span> ")
			.arg(stamp.name(), time.toHtmlEscaped());

		qreal indent = QFontMetricsF(font).horizontalAdvance(time + QLatin1Char(' '));

		if (message.fromHub.isEmpty())
		{
			const QColor ink = nick_colour(message.from.nick, darkBackground);
			const QString nick = message.from.nick.toHtmlEscaped();

			/*
			 * A "/me" line puts the name inside the sentence rather than before it, so
			 * it is set in the same italic as the words that follow it.
			 */
			options.prefix += message.action
				? QStringLiteral("<span style=\"color:%1; font-style:italic\">%2</span> ")
					.arg(ink.name(), nick)
				: QStringLiteral("<b style=\"color:%1\">%2</b> ").arg(ink.name(), nick);

			indent += QFontMetricsF(speakerFont)
				.horizontalAdvance(message.from.nick + QLatin1Char(' '));

			options.italic = message.action;
		}
		else
		{
			/*
			 * A line the hub generated rather than a person: muted and italic, and with
			 * no name in front of it, so it does not read as somebody speaking.
			 */
			options.body = stamp;
			options.italic = true;
		}

		options.indent = static_cast<int>(qRound(indent));

		/*
		 * A picture gets the text column, and no more height than a message can carry
		 * without pushing everything else off the screen.
		 */
		options.mediaWidth = qMax(0, width - options.indent);

		/*
		 * Resolved here, and the image handed to the document rather than named to it:
		 * a document left to fetch its own resources decides for itself what to open,
		 * and only files this client already has may be drawn.
		 */
		if (media)
		{
			options.media = [this, &media](const QString& tth) {
				const RichText::LocalMedia here = media(tth);
				if (here.isValid()) pictures.append(here);

				return here;
			};
		}

		/*
		 * Only a message that said it is markdown is read as markdown. Without the flag
		 * the text is literal, so somebody's asterisks stay asterisks.
		 */
		const QString html = message.richText
			? RichText::renderMarkdown(message.text, options)
			: RichText::renderPlain(message.text, options);

		for (const RichText::LocalMedia& picture : pictures)
		{
			/*
			 * Read at the size it will be drawn at, so a photograph from a camera is
			 * not held in memory at full resolution for every row on screen.
			 */
			QImageReader reader(picture.path);
			reader.setAutoTransform(true);

			QSize scaled = picture.size;
			const int room = options.mediaWidth;

			if (room > 0 && scaled.width() > room)
				scaled = QSize(room, scaled.height() * room / scaled.width());

			if (options.mediaHeight > 0 && scaled.height() > options.mediaHeight)
			{
				scaled = QSize(scaled.width() * options.mediaHeight / scaled.height(),
				               options.mediaHeight);
			}

			reader.setScaledSize(scaled);

			const QImage image = reader.read();
			if (image.isNull()) continue;

			document.addResource(QTextDocument::ImageResource,
			                     QUrl(picture.path), image);
		}

		document.setHtml(html);
		document.setTextWidth(width);
	}

	QTextDocument document;

	/** The pictures this row draws, kept so they can be loaded before the html is set. */
	QList<RichText::LocalMedia> pictures;
};

Qdc::ChatDelegate::ChatDelegate(QObject* parent)
	: QStyledItemDelegate(parent)
{
}

void Qdc::ChatDelegate::setMediaResolver(
	std::function<RichText::LocalMedia(const QString& tth)> media)
{
	m_media = std::move(media);
	invalidateLayout();
}

void Qdc::ChatDelegate::invalidateLayout()
{
	qDeleteAll(m_cache);
	m_cache.clear();
	m_cachedWidth = -1;
}

const Qdc::ChatDelegate::Layout& Qdc::ChatDelegate::layoutFor(
	const QModelIndex& index, int width, const QStyleOptionViewItem& option) const
{
	if (width != m_cachedWidth)
	{
		qDeleteAll(m_cache);
		m_cache.clear();
		m_cachedWidth = width;
	}

	if (Layout* cached = m_cache.value(index.row(), nullptr)) return *cached;

	const ChatMessage message =
		index.data(ChatModel::MessageRole).value<ChatMessage>();

	/*
	 * Which way round the palette is, decided from the window colour rather than
	 * a setting: the nick colours have to stay legible on whichever it is, and
	 * nothing else here knows the theme.
	 */
	const QColor background = option.palette.color(QPalette::Base);
	const bool dark = background.lightness() < 128;

	Layout* layout = new Layout(message, width, option.font, option.palette, dark, m_media);
	m_cache.insert(index.row(), layout);

	return *layout;
}

void Qdc::ChatDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const
{
	QStyleOptionViewItem adjusted = option;
	initStyleOption(&adjusted, index);

	/* The style draws selection and hover; the text is ours. */
	adjusted.text.clear();

	QStyle* style = adjusted.widget ? adjusted.widget->style() : nullptr;
	if (style)
	{
		style->drawControl(QStyle::CE_ItemViewItem, &adjusted, painter,
		                   adjusted.widget);
	}

	const int width = option.rect.width() - 2 * MARGIN_X;
	if (width <= 0) return;

	const Layout& layout = layoutFor(index, width, option);

	painter->save();
	painter->translate(option.rect.left() + MARGIN_X, option.rect.top() + MARGIN_Y);

	QAbstractTextDocumentLayout::PaintContext context;
	context.palette = option.palette;

	if (option.state & QStyle::State_Selected)
	{
		context.palette.setColor(QPalette::Text,
			option.palette.color(QPalette::HighlightedText));
	}

	layout.document.documentLayout()->draw(painter, context);
	painter->restore();
}

QSize Qdc::ChatDelegate::sizeHint(const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const
{
	const int width = option.rect.width() - 2 * MARGIN_X;

	/*
	 * QListView asks before it has a width to offer, on the very first layout.
	 * A single line's worth is enough to get the pass started; the real height
	 * follows once the viewport is sized and invalidateLayout() has run.
	 */
	if (width <= 0)
	{
		return QSize(0, QFontMetrics(option.font).height() + 2 * MARGIN_Y);
	}

	const Layout& layout = layoutFor(index, width, option);

	return QSize(option.rect.width(),
	             qCeil(layout.document.size().height()) + 2 * MARGIN_Y);
}

QString Qdc::ChatDelegate::anchorAt(const QModelIndex& index,
                                   const QStyleOptionViewItem& option,
                                   const QPoint& point) const
{
	const int width = option.rect.width() - 2 * MARGIN_X;
	if (width <= 0) return QString();

	const Layout& layout = layoutFor(index, width, option);

	/* Into the document's own coordinates, which start where paint() translated to. */
	const QPointF inside = QPointF(point) - QPointF(option.rect.left() + MARGIN_X,
	                                                option.rect.top() + MARGIN_Y);

	if (inside.x() < 0.0 || inside.y() < 0.0) return QString();

	return layout.document.documentLayout()->anchorAt(inside);
}

Qdc::ChatFeed::ChatFeed(QWidget* parent)
	: QListView(parent)
	, m_delegate(new ChatDelegate(this))
{
	setItemDelegate(m_delegate);

	/* Heights differ per row, so the view must measure rather than assume. */
	setUniformItemSizes(false);

	setSelectionMode(QAbstractItemView::ExtendedSelection);
	setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setFrameShape(QFrame::NoFrame);
	setWordWrap(true);
	setAccessibleName(tr("Messages"));

	/* So that a link can show a hand before it is pressed. */
	viewport()->setMouseTracking(true);
}

void Qdc::ChatFeed::setModel(QAbstractItemModel* model)
{
	if (QAbstractItemModel* previous = QListView::model())
		previous->disconnect(this);

	QListView::setModel(model);
	if (!model) return;

	/*
	 * Sampled before the insertion, while the scrollbar still reflects where the
	 * view was, and acted on after - by which time the new rows have been laid
	 * out and there is something to scroll to.
	 */
	connect(model, &QAbstractItemModel::rowsAboutToBeInserted, this, [this] {
		m_wasAtBottom = isAtBottom();
	});

	connect(model, &QAbstractItemModel::rowsInserted,
	        this, &ChatFeed::onRowsInserted);

	/*
	 * The delegate caches a laid out row against its row number, and trimming the
	 * backlog renumbers every row after the one that went - so without this the feed
	 * would go on measuring and painting each row with the layout of another.
	 */
	connect(model, &QAbstractItemModel::rowsRemoved, this, [this] {
		m_delegate->invalidateLayout();
		scheduleDelayedItemsLayout();
	});

	connect(model, &QAbstractItemModel::modelReset, this, [this] {
		m_delegate->invalidateLayout();
		scheduleDelayedItemsLayout();
	});
}

void Qdc::ChatFeed::setMediaResolver(
	std::function<RichText::LocalMedia(const QString& tth)> media)
{
	m_delegate->setMediaResolver(std::move(media));
	scheduleDelayedItemsLayout();
}

void Qdc::ChatFeed::refreshMedia()
{
	const bool wasAtBottom = isAtBottom();

	m_delegate->invalidateLayout();
	scheduleDelayedItemsLayout();

	/* A row that grows to fit a picture must not push the tail out of sight. */
	if (wasAtBottom)
	{
		QMetaObject::invokeMethod(this, &QListView::scrollToBottom,
		                          Qt::QueuedConnection);
	}
}

bool Qdc::ChatFeed::isAtBottom() const
{
	const QScrollBar* bar = verticalScrollBar();

	/*
	 * True while there is nothing to scroll, so an empty feed starts out
	 * following the tail rather than having to be scrolled to it once.
	 */
	if (!bar || bar->maximum() == 0) return true;

	return bar->value() >= bar->maximum() - TAIL_SLACK;
}

void Qdc::ChatFeed::onRowsInserted()
{
	if (!m_wasAtBottom) return;

	/*
	 * Deferred, because the rows have been inserted but the view has not yet
	 * measured them, so the scrollbar's maximum is still the old one.
	 */
	QMetaObject::invokeMethod(this, &QListView::scrollToBottom,
	                          Qt::QueuedConnection);
}

void Qdc::ChatFeed::resizeEvent(QResizeEvent* event)
{
	const bool wasAtBottom = isAtBottom();
	const bool widthChanged = event->oldSize().width() != event->size().width();

	QListView::resizeEvent(event);

	if (widthChanged)
	{
		/*
		 * Every row has to re-wrap, so the cached heights are all wrong and the
		 * view has to measure again.
		 */
		m_delegate->invalidateLayout();
		scheduleDelayedItemsLayout();
	}

	if (wasAtBottom)
	{
		QMetaObject::invokeMethod(this, &QListView::scrollToBottom,
		                          Qt::QueuedConnection);
	}
}

QString Qdc::ChatFeed::anchorAt(const QPoint& point) const
{
	const QModelIndex index = indexAt(point);
	if (!index.isValid()) return QString();

	QStyleOptionViewItem option;
	initViewItemOption(&option);
	option.rect = visualRect(index);

	return m_delegate->anchorAt(index, option, point);
}

void Qdc::ChatFeed::mouseMoveEvent(QMouseEvent* event)
{
	QListView::mouseMoveEvent(event);

	/* Not while dragging out a selection, where the pointer is doing something else. */
	const QString anchor = event->buttons() == Qt::NoButton
		? anchorAt(event->position().toPoint()) : QString();

	if (anchor.isEmpty())
		viewport()->unsetCursor();
	else
		viewport()->setCursor(Qt::PointingHandCursor);
}

void Qdc::ChatFeed::mousePressEvent(QMouseEvent* event)
{
	m_pressedAnchor = event->button() == Qt::LeftButton
		? anchorAt(event->position().toPoint()) : QString();

	QListView::mousePressEvent(event);
}

void Qdc::ChatFeed::mouseReleaseEvent(QMouseEvent* event)
{
	const QString anchor = m_pressedAnchor;
	m_pressedAnchor.clear();

	QListView::mouseReleaseEvent(event);

	if (anchor.isEmpty() || event->button() != Qt::LeftButton) return;

	/* Released somewhere else, so this was a drag and not a click on the link. */
	if (anchorAt(event->position().toPoint()) != anchor) return;

	const QUrl url(anchor, QUrl::StrictMode);

	/*
	 * Checked again on the way out even though nothing unlinkable was ever rendered as
	 * an anchor: this is the last point before a stranger's text reaches the desktop.
	 */
	if (!RichText::isLinkable(url)) return;

	emit linkActivated(url);
}

void Qdc::ChatFeed::leaveEvent(QEvent* event)
{
	viewport()->unsetCursor();
	QListView::leaveEvent(event);
}

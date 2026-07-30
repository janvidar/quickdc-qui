/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "chatfeed.h"

#include "chatmodel.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStyle>
#include <QTextDocument>
#include <QTextOption>
#include <QtMath>

namespace {

constexpr int MARGIN_X = 8;
constexpr int MARGIN_Y = 3;

/** How near the bottom still counts as following the tail, in pixels. */
constexpr int TAIL_SLACK = 24;

/**
 * Escape for the document, and turn real line breaks into <br>.
 *
 * A newline is whitespace in HTML and would otherwise collapse into a space, so a
 * message someone sent as several lines would arrive as one. Both \r\n and a bare
 * \r are normalised first, since either may come off the wire.
 */
QString escape(const QString& text)
{
	QString escaped = text.toHtmlEscaped();

	escaped.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
	escaped.replace(QLatin1Char('\r'), QLatin1Char('\n'));
	escaped.replace(QLatin1Char('\n'), QStringLiteral("<br>"));

	return escaped;
}

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
	       const QPalette& palette, bool darkBackground)
	{
		document.setDefaultFont(font);
		document.setDocumentMargin(0.0);

		QTextOption options = document.defaultTextOption();
		options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
		document.setDefaultTextOption(options);

		const QColor stamp = palette.color(QPalette::PlaceholderText);
		const QString time = message.received.toString(QStringLiteral("HH:mm"));

		/*
		 * Where the message text starts, measured rather than guessed: the
		 * timestamp is in the body font and the name is bold, so the two have to be
		 * measured with the fonts they are drawn in.
		 */
		QFont bold = font;
		bold.setBold(true);

		const QString speaker = message.fromHub.isEmpty() ? message.from.nick
		                                                  : message.fromHub;

		const qreal indent = QFontMetricsF(font).horizontalAdvance(time + QLatin1Char(' '))
			+ QFontMetricsF(bold).horizontalAdvance(speaker + QLatin1Char(' '));

		/*
		 * A hanging indent: the block is pushed right to the text column and its
		 * first line pulled back out again, so every line after the first - whether
		 * it came from a newline in the message or from wrapping - lines up under
		 * the text rather than under the timestamp.
		 */
		const QString style =
			QStringLiteral("margin-left:%1px; text-indent:-%1px;")
				.arg(static_cast<int>(qRound(indent)));

		QString html = QStringLiteral("<div style=\"%1\">").arg(style);

		html += QStringLiteral("<span style=\"color:%1\">%2</span> ")
			.arg(stamp.name(), escape(time));

		if (!message.fromHub.isEmpty())
		{
			/*
			 * A line the hub generated rather than a person. Italic and unnamed
			 * by colour, so it does not read as someone speaking.
			 */
			html += QStringLiteral("<i style=\"color:%1\">%2</i>")
				.arg(stamp.name(), escape(message.text));
		}
		else if (message.action)
		{
			/* "/me" style: the nick belongs inside the sentence, not before it. */
			html += QStringLiteral("<i><span style=\"color:%1\">%2</span> %3</i>")
				.arg(nick_colour(message.from.nick, darkBackground).name(),
				     escape(message.from.nick), escape(message.text));
		}
		else
		{
			html += QStringLiteral("<b style=\"color:%1\">%2</b> %3")
				.arg(nick_colour(message.from.nick, darkBackground).name(),
				     escape(message.from.nick), escape(message.text));
		}

		html += QStringLiteral("</div>");

		document.setHtml(html);
		document.setTextWidth(width);
	}

	QTextDocument document;
};

Qdc::ChatDelegate::ChatDelegate(QObject* parent)
	: QStyledItemDelegate(parent)
{
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

	Layout* layout = new Layout(message, width, option.font, option.palette, dark);
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

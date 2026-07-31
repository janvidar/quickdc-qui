/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "chatpane.h"

#include "chatfeed.h"
#include "chatmodel.h"
#include "composer.h"
#include "hubheader.h"

#include <core/hubbridge.h>

#include <QFrame>
#include <QShowEvent>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

Qdc::ChatPane::ChatPane(HubBridge* hub, QWidget* parent)
	: QWidget(parent)
	, m_hub(hub)
	, m_header(new HubHeader(this))
	, m_tabs(new QTabWidget(this))
	, m_model(new ChatModel(this))
	, m_feed(new ChatFeed(this))
	, m_composer(new Composer(this))
	, m_messages(new QSplitter(Qt::Horizontal, this))
{
	m_feed->setModel(m_model);
	m_feed->setObjectName(QStringLiteral("chatFeed"));

	QWidget* conversation = new QWidget(m_messages);
	conversation->setObjectName(QStringLiteral("chatSurface"));
	conversation->setAutoFillBackground(true);

	QFrame* divider = new QFrame(conversation);
	divider->setFrameShape(QFrame::HLine);
	divider->setFrameShadow(QFrame::Plain);

	m_composer->setObjectName(QStringLiteral("composerSurface"));

	QVBoxLayout* stacked = new QVBoxLayout(conversation);
	stacked->setContentsMargins(0, 0, 0, 0);
	stacked->setSpacing(0);
	stacked->addWidget(m_feed, 1);
	stacked->addWidget(divider);
	stacked->addWidget(m_composer);

	m_messages->addWidget(conversation);
	m_messages->setStretchFactor(0, 1);
	m_messages->setChildrenCollapsible(false);

	m_tabs->setDocumentMode(true);
	m_tabs->addTab(m_messages, tr("Messages"));

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(m_header);
	layout->addWidget(m_tabs, 1);

	connect(m_composer, &Composer::sendRequested, this, &ChatPane::sendMessage);
	connect(m_feed, &ChatFeed::linkActivated, this, &ChatPane::linkActivated);
	connect(m_composer, &Composer::attachRequested, this, &ChatPane::attachRequested);
	connect(m_header, &HubHeader::closeRequested, this, &ChatPane::closeRequested);

	/*
	 * Formatting is offered only where the hub has agreed to carry the flag that says a
	 * message is markdown - which it does by answering the RTF0 extension. Until then,
	 * and on a hub that never answers, the controls are disabled rather than sending
	 * markers the far end would show literally.
	 */
	if (m_hub)
	{
		connect(m_hub, &HubBridge::richTextSupportChanged,
		        m_composer, &Composer::setRichTextEnabled);
	}

	m_composer->setRichTextEnabled(m_hub && m_hub->supportsRichText());
}

Qdc::HubBridge* Qdc::ChatPane::hub() const
{
	return m_hub;
}

Qdc::HubHeader* Qdc::ChatPane::header() const
{
	return m_header;
}

void Qdc::ChatPane::setMediaResolver(
	std::function<RichText::LocalMedia(const QString& tth)> media)
{
	m_feed->setMediaResolver(std::move(media));
}

void Qdc::ChatPane::insertAttachment(const RichText::Attachment& file)
{
	m_composer->insertAttachment(file);
}

QTabWidget* Qdc::ChatPane::tabs() const
{
	return m_tabs;
}

Qdc::ChatModel* Qdc::ChatPane::chatModel() const
{
	return m_model;
}

Qdc::ChatFeed* Qdc::ChatPane::feed() const
{
	return m_feed;
}

Qdc::Composer* Qdc::ChatPane::composer() const
{
	return m_composer;
}

void Qdc::ChatPane::addSidebar(QWidget* sidebar, int width)
{
	if (!sidebar) return;

	m_messages->addWidget(sidebar);
	m_messages->setStretchFactor(m_messages->count() - 1, 0);

	/*
	 * The conversation takes whatever is left. Given as a pair rather than by
	 * stretch alone, because a splitter distributes by size hint until it is told
	 * otherwise and the feed's hint is not meaningful.
	 */
	m_messages->setSizes({m_messages->width() - width, width});
}

int Qdc::ChatPane::unreadCount() const
{
	return m_unread;
}

void Qdc::ChatPane::clearUnread()
{
	if (m_unread == 0) return;

	m_unread = 0;
	emit unreadChanged(0);
}

void Qdc::ChatPane::post(const ChatMessage& message)
{
	m_model->append(message);

	/*
	 * isVisible() is false for a pane that is not the current one in a stack,
	 * which is exactly the question being asked - and also false for the whole
	 * window being hidden, which is the right answer too.
	 */
	if (isVisible()) return;

	++m_unread;
	emit unreadChanged(m_unread);
}

void Qdc::ChatPane::postNotice(const QString& text)
{
	/*
	 * Carried as a hub message rather than as a user's line, because that is what
	 * the feed already renders as narration - and because attributing it to a
	 * nickname would be a lie.
	 */
	ChatMessage notice;
	notice.hubUrl = m_hub ? m_hub->url() : QString();
	notice.fromHub = tr("QuickDC");
	notice.text = text;
	notice.received = QDateTime::currentDateTime();

	post(notice);
}

void Qdc::ChatPane::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);

	clearUnread();

	/* Typing is what someone almost always wants on arriving at a conversation. */
	m_composer->focusInput();
}

/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_CHATMODEL_H
#define HAVE_QUICKDC_QUI_CHATMODEL_H

#include <core/coretypes.h>

#include <QAbstractListModel>
#include <QList>

namespace Qdc {

/**
 * The lines in one conversation.
 *
 * A model rather than a growing block of rich text, which is what makes the feed
 * virtualized: the view asks for the rows it is about to paint and no others, so
 * the cost of showing a conversation does not grow with its length.
 *
 * Bounded, because a session that runs for days would otherwise grow without
 * limit. The oldest lines are dropped, which is also why this is not a plain
 * append-only list.
 */
class ChatModel final : public QAbstractListModel {
	Q_OBJECT

public:
	enum Role {
		/** The whole ChatMessage, for the delegate to lay out. */
		MessageRole = Qt::UserRole + 1
	};

	explicit ChatModel(QObject* parent = nullptr);

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role) const override;

	void append(const ChatMessage& message);
	void clear();

	/**
	 * How many lines to keep. Older ones are dropped from the front as new ones
	 * arrive.
	 *
	 * The default is a few thousand, which is enough that scrolling back feels
	 * unbounded but small enough that the view's own layout pass stays cheap -
	 * QListView measures every row to know how far it can scroll, so this is the
	 * number that pass is proportional to.
	 */
	void setCapacity(int capacity);
	int capacity() const;

	const ChatMessage& at(int row) const;

private:
	/** Drop from the front until the capacity is met. */
	void trim();

	QList<ChatMessage> m_messages;
	int m_capacity = 4000;
};

}

#endif // HAVE_QUICKDC_QUI_CHATMODEL_H

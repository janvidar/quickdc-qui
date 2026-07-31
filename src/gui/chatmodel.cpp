/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "chatmodel.h"

#include "richtext.h"

Qdc::ChatModel::ChatModel(QObject* parent)
	: QAbstractListModel(parent)
{
}

int Qdc::ChatModel::rowCount(const QModelIndex& parent) const
{
	/* A list has no children, so anything below the root has no rows. */
	return parent.isValid() ? 0 : static_cast<int>(m_messages.size());
}

QVariant Qdc::ChatModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= m_messages.size())
		return QVariant();

	const ChatMessage& message = m_messages.at(index.row());

	switch (role)
	{
		case MessageRole:
			return QVariant::fromValue(message);

		case Qt::DisplayRole:
		{
			/*
			 * Not what the delegate paints; this is for accessibility and for
			 * anything that copies rows as text. Markup comes off, so what is read
			 * aloud or pasted elsewhere is prose rather than asterisks.
			 */
			const QString text = message.richText
				? RichText::toPlainText(message.text) : message.text;

			return message.fromHub.isEmpty()
				? QStringLiteral("%1: %2").arg(message.from.nick, text)
				: QStringLiteral("%1: %2").arg(message.fromHub, text);
		}

		default:
			return QVariant();
	}
}

void Qdc::ChatModel::append(const ChatMessage& message)
{
	const int row = static_cast<int>(m_messages.size());

	beginInsertRows(QModelIndex(), row, row);
	m_messages.append(message);
	endInsertRows();

	trim();
}

void Qdc::ChatModel::clear()
{
	if (m_messages.isEmpty()) return;

	beginResetModel();
	m_messages.clear();
	endResetModel();
}

void Qdc::ChatModel::setCapacity(int capacity)
{
	m_capacity = qMax(1, capacity);
	trim();
}

int Qdc::ChatModel::capacity() const
{
	return m_capacity;
}

const Qdc::ChatMessage& Qdc::ChatModel::at(int row) const
{
	/*
	 * Callers come from the delegate, which is only ever asked about rows the
	 * view believes exist. A static empty message rather than an assertion, so a
	 * stale index paints nothing instead of taking the process down.
	 */
	static const ChatMessage empty;

	if (row < 0 || row >= m_messages.size()) return empty;
	return m_messages.at(row);
}

void Qdc::ChatModel::trim()
{
	const int excess = static_cast<int>(m_messages.size()) - m_capacity;
	if (excess <= 0) return;

	/* One removal for the whole excess, so the view relayouts once. */
	beginRemoveRows(QModelIndex(), 0, excess - 1);
	m_messages.remove(0, excess);
	endRemoveRows();
}

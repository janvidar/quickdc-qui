/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "usermodel.h"

#include "glyphs.h"
#include "richtext.h"

#include <QIcon>
#include <QLocale>

Qdc::UserModel::UserModel(QObject* parent)
	: QAbstractListModel(parent)
{
}

int Qdc::UserModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(m_users.size());
}

QVariant Qdc::UserModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= m_users.size())
		return QVariant();

	const UserInfo& user = m_users.at(index.row());

	switch (role)
	{
		case UserRole:
			return QVariant::fromValue(user);

		case Qt::DisplayRole:
			/*
			 * A nickname is whatever the person chose, and this is a dense list read at
			 * a glance - so it is disarmed on the way to the view and nowhere earlier.
			 * UserRole still carries the real record, which is what everything that
			 * acts on a row goes by.
			 */
			return RichText::displayText(user.nick);

		case Qt::DecorationRole:
			/*
			 * A figure rather than a dot, so the column reads as people rather than
			 * as strings, and hollow for somebody who says they are away.
			 *
			 * Away is what the user claims, in the AW field of an ADC INF or the
			 * status byte of an NMDC $MyINFO. Nothing else is known about anybody's
			 * attention, so there are two states and not three: a hub that says
			 * nothing leaves a user drawn as present.
			 *
			 * The colour says whether they are an operator, which is the other thing
			 * about a user worth seeing without reading the list.
			 */
			return presence_icon(user.away, user.op);

		case Qt::ToolTipRole:
		{
			/*
			 * Every line the user chose is disarmed one at a time, before the join
			 * rather than after it: the newlines between them are ours, and a pass over
			 * the joined text would take them out again. The escaping goes the other way
			 * round - once, over the whole of it, at the end - because a tooltip is drawn
			 * as markup, and the shape of this one is to be the only markup in it.
			 */
			QStringList lines;
			lines << RichText::displayText(user.nick);

			if (!user.description.isEmpty())
				lines << RichText::displayText(user.description);

			if (user.sharedBytes > 0)
			{
				lines << tr("Sharing %1")
					.arg(QLocale().formattedDataSize(
						static_cast<qint64>(user.sharedBytes)));
			}

			if (!user.userAgent.isEmpty())
				lines << RichText::displayText(user.userAgent);
			if (user.op) lines << tr("Operator");

			/* Said as well as drawn: a hollow figure is not self-explanatory. */
			if (user.away) lines << tr("Away");

			/*
			 * A keyprint means they published a certificate for peer connections.
			 * Worth showing in full rather than as a flag, since the whole point
			 * of one is being able to compare it.
			 */
			if (!user.keyprint.isEmpty())
				lines << RichText::displayText(user.keyprint);

			return RichText::literalHtml(lines.join(QLatin1Char('\n')));
		}

		default:
			return QVariant();
	}
}

bool Qdc::UserModel::precedes(const UserInfo& left, const UserInfo& right)
{
	if (left.op != right.op) return left.op;

	const int order = QString::compare(left.nick, right.nick, Qt::CaseInsensitive);
	if (order != 0) return order < 0;

	/*
	 * Two people with the same nickname differing only in case are possible on
	 * NMDC, so the id breaks the tie and keeps the order total - without this the
	 * insertion point would depend on which was compared first.
	 */
	return left.id < right.id;
}

int Qdc::UserModel::placeFor(const UserInfo& user) const
{
	int low = 0;
	int high = static_cast<int>(m_users.size());

	while (low < high)
	{
		const int middle = low + (high - low) / 2;

		if (precedes(m_users.at(middle), user))
			low = middle + 1;
		else
			high = middle;
	}

	return low;
}

void Qdc::UserModel::reindex(int from)
{
	for (int n = qMax(0, from); n < m_users.size(); ++n)
		m_rows.insert(m_users.at(n).id, n);
}

void Qdc::UserModel::upsert(const UserInfo& user)
{
	if (user.id.isEmpty()) return;

	/*
	 * This is the real record, so it is no longer standing in for anything.
	 */
	m_placeholders.remove(user.id);

	/*
	 * On ADC the id is a session id and the placeholder was keyed on the nickname,
	 * so the stand-in has to be evicted by name before this row is added - or the
	 * user is listed twice, once as a bare nickname and once for real.
	 */
	if (user.id != user.nick && m_placeholders.contains(user.nick))
		remove(user.nick);

	const auto existing = m_rows.constFind(user.id);
	if (existing != m_rows.constEnd())
	{
		const int row = existing.value();

		/*
		 * Removed and reinserted rather than updated in place, because the
		 * details that changed may include the nickname or operator flag, and
		 * those decide where the row belongs.
		 */
		const bool moves = precedes(user, m_users.at(row))
			|| (row + 1 < m_users.size() && precedes(m_users.at(row + 1), user));

		if (!moves)
		{
			m_users[row] = user;
			const QModelIndex changed = index(row);
			emit dataChanged(changed, changed);
			return;
		}

		remove(user.id);
	}

	const int row = placeFor(user);

	beginInsertRows(QModelIndex(), row, row);
	m_users.insert(row, user);
	endInsertRows();

	reindex(row);
	emit countChanged(count());
}

void Qdc::UserModel::noteNickname(const QString& nick)
{
	if (nick.isEmpty() || m_rows.contains(nick)) return;

	/*
	 * A join carries only a nickname, and on NMDC that doubles as the id. On ADC
	 * the details follow in an update keyed by the real id, which will add a
	 * second row - so this row is replaced when that happens only if the ids
	 * agree. Showing the name in the meantime is better than an empty list.
	 */
	UserInfo user;
	user.id = nick;
	user.nick = nick;
	user.sourceId = nick;

	upsert(user);

	/*
	 * Marked after the insert, because upsert() clears the mark for the id it is
	 * given - which is this one.
	 */
	m_placeholders.insert(nick);
}

void Qdc::UserModel::remove(const QString& id)
{
	const auto existing = m_rows.constFind(id);
	if (existing == m_rows.constEnd()) return;

	const int row = existing.value();

	beginRemoveRows(QModelIndex(), row, row);
	m_users.removeAt(row);
	endRemoveRows();

	m_rows.remove(id);
	m_placeholders.remove(id);
	reindex(row);

	emit countChanged(count());
}

void Qdc::UserModel::clear()
{
	if (m_users.isEmpty()) return;

	beginResetModel();
	m_users.clear();
	m_rows.clear();
	m_placeholders.clear();
	endResetModel();

	emit countChanged(0);
}

int Qdc::UserModel::count() const
{
	return static_cast<int>(m_users.size());
}

Qdc::UserInfo Qdc::UserModel::at(int row) const
{
	if (row < 0 || row >= m_users.size()) return UserInfo();
	return m_users.at(row);
}

Qdc::UserInfo Qdc::UserModel::byId(const QString& id) const
{
	const auto found = m_rows.constFind(id);
	if (found == m_rows.constEnd()) return UserInfo();

	return at(found.value());
}

Qdc::UserInfo Qdc::UserModel::resolve(const UserInfo& remembered) const
{
	if (!remembered.sourceId.isEmpty())
	{
		for (const UserInfo& user : m_users)
			if (user.sourceId == remembered.sourceId) return user;
	}

	if (!remembered.id.isEmpty())
	{
		const UserInfo found = byId(remembered.id);
		if (!found.id.isEmpty()) return found;
	}

	if (!remembered.nick.isEmpty())
	{
		for (const UserInfo& user : m_users)
			if (user.nick == remembered.nick) return user;
	}

	return UserInfo();
}

QStringList Qdc::UserModel::nicknames() const
{
	QStringList names;
	names.reserve(static_cast<qsizetype>(m_users.size()));

	for (const UserInfo& user : m_users)
	{
		if (!user.nick.isEmpty()) names.append(user.nick);
	}

	return names;
}

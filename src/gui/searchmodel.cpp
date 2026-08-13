/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "searchmodel.h"

#include "richtext.h"

#include <core/searchbridge.h>

#include <QFileInfo>
#include <QLocale>
#include <QSet>

#include <algorithm>

namespace {

QString format_size(quint64 bytes)
{
	return QLocale().formattedDataSize(static_cast<qint64>(bytes));
}

/**
 * The last part of a reply's path, which is what a row is labelled with.
 *
 * The separator has to come off first. A folder is named with one on the end, and
 * QFileInfo reads that as a path ending in no name at all and answers with nothing -
 * so taken straight, every folder in a result set is a blank row.
 */
QString display_name(const QString& path)
{
	QString name = path;

	while (name.endsWith(QLatin1Char('/')))
		name.chop(1);

	return QFileInfo(name).fileName();
}

QString category_name(Qdc::SearchCategory category)
{
	switch (category)
	{
		case Qdc::SearchCategory::Audio:    return Qdc::SearchModel::tr("Audio");
		case Qdc::SearchCategory::Video:    return Qdc::SearchModel::tr("Video");
		case Qdc::SearchCategory::Picture:  return Qdc::SearchModel::tr("Picture");
		case Qdc::SearchCategory::Document: return Qdc::SearchModel::tr("Document");
		case Qdc::SearchCategory::Archive:  return Qdc::SearchModel::tr("Archive");
		case Qdc::SearchCategory::Any:      break;
	}

	return QString();
}

}

Qdc::SearchModel::SearchModel(QObject* parent)
	: QAbstractItemModel(parent)
{
}

QString Qdc::SearchModel::peerKey(const SearchHit& hit)
{
	/* By hub as well as by id: the same nick on two hubs is two peers. */
	return hit.hubUrl + QLatin1Char('\n') + hit.user.sourceId;
}

int Qdc::SearchModel::place(const SearchHit& hit, bool& isNewGroup)
{
	++m_hits;
	m_peers.insert(peerKey(hit));

	const int existing = hit.tth.isEmpty() ? -1 : m_byRoot.value(hit.tth, -1);

	if (existing >= 0)
	{
		Group& group = m_groups[existing];
		group.hits.append(hit);

		/*
		 * A later reply may know a size an earlier one did not, and may name the
		 * file more plainly than it was named before.
		 */
		if (!group.size && hit.size) group.size = hit.size;

		const QString name = display_name(hit.name);

		if (!name.isEmpty() && (group.name.isEmpty() || name.size() < group.name.size()))
		{
			group.name = name;
			group.category = SearchBridge::categoryOf(group.name);
		}

		isNewGroup = false;
		return existing;
	}

	Group group;
	group.tth  = hit.tth;
	group.name = display_name(hit.name);
	group.size = hit.size;
	group.directory = hit.directory;
	group.category = SearchBridge::categoryOf(group.name);
	group.hits.append(hit);

	const int row = static_cast<int>(m_groups.size());
	m_groups.append(group);

	if (!hit.tth.isEmpty()) m_byRoot.insert(hit.tth, row);

	isNewGroup = true;
	return row;
}

void Qdc::SearchModel::add(const SearchHit& hit)
{
	add(QVector<SearchHit>{hit});
}

void Qdc::SearchModel::add(const QVector<SearchHit>& hits)
{
	if (hits.isEmpty()) return;

	const int firstNew = static_cast<int>(m_groups.size());

	/*
	 * Two passes rather than one, because the rows a batch adds have to be announced
	 * as one range: a beginInsertRows per reply is a relayout of the view per reply,
	 * and that is what a burst of them costs when it is not held back.
	 */
	QList<int> touched;

	for (const SearchHit& hit : hits)
	{
		bool fresh = false;
		const int row = place(hit, fresh);

		/* Only the ones that already existed; a new group is announced by its insert. */
		if (!fresh && row < firstNew) touched.append(row);
	}

	const int added = static_cast<int>(m_groups.size()) - firstNew;

	if (added > 0)
	{
		/*
		 * The groups are already in place, so this only tells the view about them.
		 * Safe because nothing between the two calls asks this model anything.
		 */
		beginInsertRows(QModelIndex(), firstNew, firstNew + added - 1);
		endInsertRows();
	}

	if (touched.isEmpty()) return;

	std::sort(touched.begin(), touched.end());
	touched.erase(std::unique(touched.begin(), touched.end()), touched.end());

	/*
	 * A run at a time. The peer count and the slot total have moved on every row a
	 * reply landed in, and a batch that fills a contiguous stretch of them says so
	 * once rather than a row at a time.
	 */
	int start = touched.first();
	int previous = start;

	const auto report = [this](int from, int to) {
		emit dataChanged(index(from, 0), index(to, ColumnCount - 1));

		for (int row = from; row <= to; ++row)
		{
			const QModelIndex parent = index(row, 0);
			const int peers = static_cast<int>(m_groups.at(row).hits.size());

			if (peers > 0)
				emit dataChanged(index(0, 0, parent), index(peers - 1, ColumnCount - 1, parent));
		}
	};

	for (int n = 1; n < touched.size(); ++n)
	{
		if (touched.at(n) == previous + 1)
		{
			previous = touched.at(n);
			continue;
		}

		report(start, previous);
		start = previous = touched.at(n);
	}

	report(start, previous);
}

Qdc::SearchHit Qdc::SearchModel::hitAt(const QModelIndex& index) const
{
	if (!index.isValid()) return SearchHit();

	if (isPeer(index))
	{
		const int parent = groupOf(index);
		if (parent < 0 || parent >= m_groups.size()) return SearchHit();

		const Group& group = m_groups.at(parent);
		if (index.row() < 0 || index.row() >= group.hits.size()) return SearchHit();

		return group.hits.at(index.row());
	}

	if (index.row() < 0 || index.row() >= m_groups.size()) return SearchHit();

	const Group& group = m_groups.at(index.row());
	if (group.hits.isEmpty()) return SearchHit();

	/* The one most likely to answer straight away. */
	const SearchHit* best = &group.hits.first();

	for (const SearchHit& hit : group.hits)
	{
		if (hit.freeSlots > best->freeSlots) best = &hit;
	}

	return *best;
}

int Qdc::SearchModel::fileCount() const
{
	return static_cast<int>(m_groups.size());
}

int Qdc::SearchModel::hitCount() const
{
	return m_hits;
}

int Qdc::SearchModel::peerCount() const
{
	return static_cast<int>(m_peers.size());
}

int Qdc::SearchModel::rowCount(const QModelIndex& parent) const
{
	if (!parent.isValid()) return static_cast<int>(m_groups.size());

	/* A peer has nothing under it, and only the first column carries children. */
	if (isPeer(parent) || parent.column() != 0) return 0;

	if (parent.row() < 0 || parent.row() >= m_groups.size()) return 0;

	return static_cast<int>(m_groups.at(parent.row()).hits.size());
}

int Qdc::SearchModel::columnCount(const QModelIndex&) const
{
	return ColumnCount;
}

QModelIndex Qdc::SearchModel::index(int row, int column, const QModelIndex& parent) const
{
	if (!hasIndex(row, column, parent)) return QModelIndex();

	if (!parent.isValid()) return createIndex(row, column, quintptr(0));

	return createIndex(row, column, quintptr(parent.row() + 1));
}

QModelIndex Qdc::SearchModel::parent(const QModelIndex& child) const
{
	if (!child.isValid() || !isPeer(child)) return QModelIndex();

	return createIndex(groupOf(child), 0, quintptr(0));
}

QVariant Qdc::SearchModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid()) return QVariant();

	if (isPeer(index))
	{
		const int parent = groupOf(index);
		if (parent < 0 || parent >= m_groups.size()) return QVariant();

		const Group& group = m_groups.at(parent);
		if (index.row() < 0 || index.row() >= group.hits.size()) return QVariant();

		return peerData(group.hits.at(index.row()), index.column(), role);
	}

	if (index.row() < 0 || index.row() >= m_groups.size()) return QVariant();

	return fileData(m_groups.at(index.row()), index.column(), role);
}

QVariant Qdc::SearchModel::fileData(const Group& group, int column, int role) const
{
	if (role == Qt::ToolTipRole)
	{
		/*
		 * The name is the one the peer answering the search chose, so it is disarmed
		 * before it is put in front of a sentence of ours - which is the confusion RTF0
		 * asks to be prevented. The hash beside it is our own reading of base32.
		 *
		 * The sentence and the label are ours and the name is not, and a tooltip is drawn
		 * as markup, so the finished thing is escaped: what stays structure is the newline
		 * between the two lines, and nothing the peer sent can add to it.
		 */
		const QString name = RichText::displayText(group.name);

		return RichText::literalHtml(group.tth.isEmpty()
			? tr("%1\nNo hash, so this file cannot be matched against any other.")
				.arg(name)
			: tr("%1\nTTH: %2").arg(name, group.tth));
	}

	/* The figures right, the words left - and the hub is words, as it is below. */
	if (role == Qt::TextAlignmentRole
	    && (column == SizeColumn || column == UsersColumn || column == SlotsColumn))
	{
		return QVariant(Qt::AlignRight | Qt::AlignVCenter);
	}

	if (role != Qt::DisplayRole) return QVariant();

	switch (column)
	{
		case NameColumn:
			/*
			 * Disarmed here and not in place(), so that what is matched by hash, sorted
			 * and asked for is still the name the peer sent.
			 */
			return RichText::displayText(group.name);

		case SizeColumn:
			return group.size ? format_size(group.size) : QString();

		case UsersColumn:
			return static_cast<int>(group.hits.size());

		case SlotsColumn:
		{
			/*
			 * Free of advertised, added up over the peers offering it, which is a
			 * fair reading of how likely the file is to start moving now.
			 */
			quint32 free = 0;
			quint32 total = 0;

			for (const SearchHit& hit : group.hits)
			{
				free += hit.freeSlots;
				total += hit.user.slotCount;
			}

			return QStringLiteral("%1/%2").arg(free).arg(total);
		}

		case TypeColumn:
			return group.directory ? tr("Folder") : category_name(group.category);

		case HubColumn:
		{
			QSet<QString> hubs;
			for (const SearchHit& hit : group.hits) hubs.insert(hit.hubUrl);

			if (hubs.size() == 1) return *hubs.constBegin();

			return hubs.size() == 2 ? tr("2 hubs")
			                        : tr("%1 hubs").arg(hubs.size());
		}

		default:
			break;
	}

	return QVariant();
}

QVariant Qdc::SearchModel::peerData(const SearchHit& hit, int column, int role) const
{
	if (role == Qt::ToolTipRole)
	{
		/* The path is the peer's own; the file row shows only the name from it. */
		return RichText::literalHtml(RichText::displayText(hit.name));
	}

	if (role == Qt::TextAlignmentRole && column == SlotsColumn)
		return QVariant(Qt::AlignRight | Qt::AlignVCenter);

	if (role != Qt::DisplayRole) return QVariant();

	switch (column)
	{
		case NameColumn:
			return hit.user.nick.isEmpty()
				? tr("somebody") : RichText::displayText(hit.user.nick);

		case SlotsColumn:
			return QStringLiteral("%1/%2").arg(hit.freeSlots).arg(hit.user.slotCount);

		case HubColumn:
			return hit.hubUrl;

		default:
			break;
	}

	/*
	 * Size, count and type are the file's rather than the peer's, and repeating them
	 * on every row under it says nothing.
	 */
	return QVariant();
}

QVariant Qdc::SearchModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return QAbstractItemModel::headerData(section, orientation, role);

	switch (section)
	{
		case NameColumn:  return tr("Name");
		case SizeColumn:  return tr("Size");
		case UsersColumn: return tr("Users");
		case SlotsColumn: return tr("Slots");
		case TypeColumn:  return tr("Type");
		case HubColumn:   return tr("Hub");

		default:          break;
	}

	return QVariant();
}

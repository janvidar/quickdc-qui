/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "boardmodel.h"

#include "richtext.h"

#include <QDateTime>
#include <QFont>
#include <QLocale>
#include <QPalette>
#include <QSet>

#include <algorithm>

Qdc::BoardModel::BoardModel(QObject* parent)
	: QAbstractItemModel(parent)
{
}

void Qdc::BoardModel::setBoard(const QString& board)
{
	m_board = board;
}

QString Qdc::BoardModel::board() const
{
	return m_board;
}

bool Qdc::BoardModel::isPlausible(quint64 seconds)
{
	if (!seconds) return false;

	const quint64 now = static_cast<quint64>(QDateTime::currentSecsSinceEpoch());

	return seconds <= now + FutureTolerance;
}

QString Qdc::BoardModel::describeTimestamp(quint64 seconds)
{
	/*
	 * Not corrected, not clamped and not shown: a hub that stamps a post a century
	 * ahead has pinned it to the head of the board for good, and the number is what
	 * every cursor derived from it is made of. Saying the time is unknown is the whole
	 * of what a client may do about it.
	 */
	if (!isPlausible(seconds)) return tr("unknown time");

	return QLocale().toString(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(seconds)),
	                          QLocale::ShortFormat);
}

bool Qdc::BoardModel::listable(const QString& tth) const
{
	if (tth.isEmpty()) return false;

	const auto found = m_entries.constFind(tth);
	if (found == m_entries.constEnd() || found->removed) return false;

	return !m_failed.contains(tth);
}

void Qdc::BoardModel::setVerificationFailed(const QString& tth)
{
	if (tth.isEmpty() || m_failed.contains(tth)) return;

	m_failed.insert(tth);

	/*
	 * A rebuild, because a post leaving takes its replies with it wherever they were
	 * drawn: they name a parent that is no longer on the board and become orphans of
	 * the thread, which is the same arrangement as a parent that never arrived.
	 */
	beginResetModel();
	rebuild();
	endResetModel();
}

const Qdc::BoardEntry* Qdc::BoardModel::live(const QString& tth) const
{
	if (!listable(tth)) return nullptr;

	return &m_entries.constFind(tth).value();
}

bool Qdc::BoardModel::holds(const QString& tth) const
{
	return !tth.isEmpty() && m_entries.contains(tth);
}

bool Qdc::BoardModel::isWithdrawn(const QString& tth) const
{
	const auto found = m_entries.constFind(tth);

	return found != m_entries.constEnd() && found->removed;
}

Qdc::BoardEntry Qdc::BoardModel::entryOf(const QString& tth) const
{
	return m_entries.value(tth);
}

Qdc::PostState Qdc::BoardModel::stateOf(const QString& tth) const
{
	/*
	 * The tombstone wins over anything cached about the document. What was verified
	 * stays verified - that is a property of bytes and a hash - but the post's place on
	 * this board is gone, and that is what a reader is being told about here.
	 */
	if (isWithdrawn(tth)) return PostState::Withdrawn;

	return m_states.value(tth, PostState::Announced);
}

void Qdc::BoardModel::setState(const QString& tth, PostState state)
{
	if (tth.isEmpty()) return;

	if (m_states.value(tth, PostState::Announced) == state) return;

	m_states.insert(tth, state);

	const QModelIndex row = indexOf(tth);
	if (!row.isValid()) return;

	emit dataChanged(row, row.sibling(row.row(), ColumnCount - 1));
}

void Qdc::BoardModel::add(const BoardEntry& entry)
{
	add(QVector<BoardEntry>{entry});
}

void Qdc::BoardModel::add(const QVector<BoardEntry>& entries)
{
	bool changed = false;

	for (const BoardEntry& entry : entries)
	{
		/* The root is the identity, so an entry without one names nothing. */
		if (entry.tth.isEmpty()) continue;

		/*
		 * One hub's assertions about one board. An entry naming another board is not a
		 * conflict to resolve but a different board, and holding both here would
		 * produce one that exists nowhere.
		 */
		if (!m_board.isEmpty() && !entry.board.isEmpty() && entry.board != m_board)
			continue;

		const auto held = m_entries.constFind(entry.tth);

		if (held != m_entries.constEnd() && entry.timestamp <= held->timestamp)
		{
			/*
			 * Resuming replays the whole second the cursor sits in, so the entries of
			 * that second arrive twice; a copy of one already held carries an equal
			 * timestamp and is dropped. A withdrawal is the same rule read the other
			 * way: a tombstone is stamped with the time of the withdrawal, so it is
			 * greater and takes effect.
			 */
			continue;
		}

		m_entries.insert(entry.tth, entry);
		changed = true;
	}

	if (!changed) return;

	beginResetModel();
	rebuild();
	endResetModel();
}

void Qdc::BoardModel::clear()
{
	beginResetModel();

	m_entries.clear();
	m_states.clear();
	m_failed.clear();
	m_nodes.clear();
	m_nodeByTth.clear();
	m_roots.clear();
	m_missing.clear();

	endResetModel();
}

bool Qdc::BoardModel::precedes(const QString& left, const QString& right) const
{
	const quint64 first = m_entries.value(left).timestamp;
	const quint64 second = m_entries.value(right).timestamp;

	if (first != second) return first < second;

	/*
	 * Timestamps are not unique and a busy board collides constantly, so the tie has to
	 * be broken on something. The root is the only candidate that survives a restart:
	 * it is the post's identity, it is immutable, it is the same on every hub that
	 * indexes the document, and it does not depend on the order the entries happened to
	 * arrive in. Insertion order would read correctly for one session and differently
	 * for the next, because a live feed and a replay out of a cache deliver the same
	 * second in different orders - which is the thread rearranging itself between
	 * sessions that the specification asks to be prevented.
	 */
	return left < right;
}

int Qdc::BoardModel::attach(const QString& tth, int parentNode, int depth, bool flattened,
                            bool missingParent, bool withdrawnParent)
{
	Node node;
	node.tth = tth;
	node.parent = parentNode;
	node.depth = depth;
	node.flattened = flattened;
	node.missingParent = missingParent;
	node.withdrawnParent = withdrawnParent;
	node.thread = parentNode < 0 ? tth : m_nodes.at(parentNode).thread;

	const int index = static_cast<int>(m_nodes.size());
	m_nodes.append(node);
	m_nodeByTth.insert(tth, index);

	if (parentNode < 0)
		m_roots.append(index);
	else
		m_nodes[parentNode].children.append(index);

	return index;
}

void Qdc::BoardModel::rebuild()
{
	m_nodes.clear();
	m_nodeByTth.clear();
	m_roots.clear();
	m_missing.clear();

	/* Replies under the parent they name, where that parent is one we hold and it lives. */
	QHash<QString, QStringList> children;

	/* Orphans hung on the thread the hub named, for want of anything better. */
	QHash<QString, QStringList> adopted;

	QStringList tops;
	QSet<QString> missing;

	for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it)
	{
		const BoardEntry& entry = it.value();
		if (!listable(entry.tth)) continue;

		if (entry.parent.isEmpty())
		{
			tops.append(entry.tth);
			continue;
		}

		/*
		 * The parent link is preferred over the hub's thread field wherever it can be
		 * followed. The thread root is the hub's convenience and is never verified,
		 * while the parent is inside the document's hash - so where the two disagree,
		 * this is where the disagreement is settled.
		 */
		if (live(entry.parent))
		{
			children[entry.parent].append(entry.tth);
			continue;
		}

		if (!m_entries.contains(entry.parent)) missing.insert(entry.parent);

		/*
		 * An orphan is ordinary rather than broken: the parent may have been withdrawn,
		 * may be on a board this session cannot subscribe to, or may be from another
		 * hub entirely. It goes at the top level of the thread the hub named, if that
		 * thread is one we hold, and stands on its own otherwise.
		 */
		if (live(entry.thread) && entry.thread != entry.tth)
			adopted[entry.thread].append(entry.tth);
		else
			tops.append(entry.tth);
	}

	m_missing = missing.values();
	std::sort(m_missing.begin(), m_missing.end());

	/** One row waiting to be made, and where it wanted to sit. */
	struct Pending {
		QString tth;
		int parent;
		int depth;
		bool flattened;
	};

	/*
	 * A work list rather than recursion. The nesting a row is *drawn* at is bounded, but
	 * the thread it came from is not, and a recursive descent of a chain of ten thousand
	 * replies is a stack a remote hub chose the depth of.
	 */
	QList<Pending> queue;
	queue.reserve(tops.size());

	for (const QString& top : tops) queue.append(Pending{top, -1, 0, false});

	while (true)
	{
		if (queue.isEmpty())
		{
			/*
			 * Nothing is dropped, whatever the hub sent. Two entries naming each other as
			 * parent are on no thread and so are reached from none; so is an orphan whose
			 * thread field names a post that is itself a reply, since only a thread root
			 * adopts. Rather than trusting the parent links to describe a tree - they are
			 * the hub's copies until the documents arrive, and a cycle is one lie away -
			 * the descent runs out and then anything still undrawn is drawn at the top
			 * level, marked as having a parent that is not here. A post left off the board
			 * because its own hub lied about it would be the one fault with no remedy.
			 */
			for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it)
			{
				if (!listable(it.key()) || m_nodeByTth.contains(it.key())) continue;

				queue.append(Pending{it.key(), -1, 0, false});
			}

			if (queue.isEmpty()) break;
		}

		const Pending pending = queue.takeFirst();

		/*
		 * A row is made once. The sweep can offer a post that the descent has meanwhile
		 * reached from its parent, and drawing the same post twice would put two of it on
		 * the board.
		 */
		if (m_nodeByTth.contains(pending.tth)) continue;

		int host = pending.parent;
		int depth = pending.depth;
		bool flattened = pending.flattened;

		if (depth > MaximumDepth)
		{
			/*
			 * The bound has been reached, so this reply is drawn beside its parent
			 * instead of under it and every reply below it likewise. The thread carries
			 * on as a flat run at the deepest level rather than disappearing, and the
			 * row says that its nesting was cut; the alternative is an indent that
			 * walks the text off the edge of the view.
			 */
			host = m_nodes.at(pending.parent).parent;
			depth = MaximumDepth;
			flattened = true;
		}

		const BoardEntry entry = m_entries.value(pending.tth);

		/*
		 * Marked by where the row actually sits rather than by why it sits there: if
		 * what it is drawn under is not the post it names as its parent, that parent is
		 * not here - withdrawn, never held, or named by links that could not be
		 * followed - and the reader is told which of the two it can be told apart.
		 */
		const bool detached = !entry.parent.isEmpty()
			&& (host < 0 || m_nodes.at(host).tth != entry.parent);

		const bool withdrawnParent = detached && isWithdrawn(entry.parent);

		const int node = attach(pending.tth, host, depth, flattened,
		                        detached && !withdrawnParent, withdrawnParent);

		for (const QString& child : children.value(pending.tth))
			queue.append(Pending{child, node, depth + 1, flattened});

		/* Only a thread root adopts: an orphan sits at the top level of its thread. */
		if (host < 0)
		{
			for (const QString& orphan : adopted.value(pending.tth))
				queue.append(Pending{orphan, node, 1, false});
		}
	}

	order();
}

void Qdc::BoardModel::order()
{
	const auto sort = [this](QList<int>& rows) {
		std::sort(rows.begin(), rows.end(), [this](int left, int right) {
			return precedes(m_nodes.at(left).tth, m_nodes.at(right).tth);
		});

		for (int row = 0; row < rows.size(); ++row) m_nodes[rows.at(row)].row = row;
	};

	sort(m_roots);

	for (Node& node : m_nodes) sort(node.children);
}

QStringList Qdc::BoardModel::missingParents() const
{
	return m_missing;
}

int Qdc::BoardModel::postCount() const
{
	return static_cast<int>(m_nodes.size());
}

int Qdc::BoardModel::threadCount() const
{
	return static_cast<int>(m_roots.size());
}

QString Qdc::BoardModel::threadOf(const QString& tth) const
{
	const int node = m_nodeByTth.value(tth, -1);

	return node < 0 ? QString() : m_nodes.at(node).thread;
}

const Qdc::BoardModel::Node* Qdc::BoardModel::nodeAt(const QModelIndex& index) const
{
	if (!index.isValid() || !isNode(index)) return nullptr;

	const int node = nodeOf(index);
	if (node < 0 || node >= m_nodes.size()) return nullptr;

	return &m_nodes.at(node);
}

Qdc::BoardEntry Qdc::BoardModel::entryAt(const QModelIndex& index) const
{
	const Node* node = nodeAt(index);

	return node ? m_entries.value(node->tth) : BoardEntry();
}

QString Qdc::BoardModel::tthAt(const QModelIndex& index) const
{
	const Node* node = nodeAt(index);

	return node ? node->tth : QString();
}

QModelIndex Qdc::BoardModel::indexOf(const QString& tth) const
{
	const int node = m_nodeByTth.value(tth, -1);
	if (node < 0) return QModelIndex();

	return createIndex(m_nodes.at(node).row, 0, quintptr(node + 1));
}

int Qdc::BoardModel::rowCount(const QModelIndex& parent) const
{
	if (!parent.isValid()) return static_cast<int>(m_roots.size());

	/* Only the first column carries children, as in every tree Qt draws. */
	if (parent.column() != 0) return 0;

	const Node* node = nodeAt(parent);

	return node ? static_cast<int>(node->children.size()) : 0;
}

int Qdc::BoardModel::columnCount(const QModelIndex&) const
{
	return ColumnCount;
}

QModelIndex Qdc::BoardModel::index(int row, int column, const QModelIndex& parent) const
{
	if (!hasIndex(row, column, parent)) return QModelIndex();

	if (!parent.isValid())
		return createIndex(row, column, quintptr(m_roots.at(row) + 1));

	const Node* node = nodeAt(parent);
	if (!node) return QModelIndex();

	return createIndex(row, column, quintptr(node->children.at(row) + 1));
}

QModelIndex Qdc::BoardModel::parent(const QModelIndex& child) const
{
	const Node* node = nodeAt(child);
	if (!node || node->parent < 0) return QModelIndex();

	const Node& above = m_nodes.at(node->parent);

	return createIndex(above.row, 0, quintptr(node->parent + 1));
}

QString Qdc::BoardModel::documentSentence(PostState state) const
{
	switch (state)
	{
		case PostState::Announced:
			return tr("The hub has announced this post. The document has not been "
			          "fetched, so nothing above has been checked against it.");

		case PostState::Fetching:
			return tr("The document is being fetched.");

		case PostState::Verified:
			return tr("The document was fetched and hashed to the root this entry "
			          "names.");

		case PostState::Disputed:
			return tr("The document was fetched and verified, but its own author "
			          "disagrees with the hub's.");

		case PostState::Unavailable:
			return tr("Nobody holding the document could be reached. The entry is kept "
			          "and can be tried again.");

		case PostState::Withdrawn:
			return tr("The post has been taken out of the hub's index.");
	}

	return QString();
}

QVariant Qdc::BoardModel::display(const BoardEntry& entry, int column) const
{
	switch (column)
	{
		case SubjectColumn:
		{
			/*
			 * Reduced here, at the draw site, and never where the value is kept: what
			 * is keyed, compared and sent back to the hub is still the subject the hub
			 * sent. The branch is on the reduced value rather than on the raw one, so a
			 * subject of nothing but invisible characters falls back to our own wording
			 * instead of leaving the row blank.
			 */
			const QString subject = RichText::displayText(entry.subject);

			return subject.isEmpty() ? tr("(no subject)") : subject;
		}

		case AuthorColumn:
		{
			/*
			 * The nickname is what a reader recognises and is the weakest thing here:
			 * it is not in the document at all, is unique on no hub and at no time, and
			 * is never what a post is attributed to or an ignore list matched against -
			 * those belong to the CID, which the tooltip gives in full. Where the hub
			 * sent no nickname the CID is drawn instead, because a row has to say
			 * something and a session identifier at least identifies a session.
			 */
			const QString nick = RichText::displayText(entry.nick);
			if (!nick.isEmpty()) return nick;

			const QString cid = RichText::displayText(entry.cid);

			return cid.isEmpty() ? tr("unknown author") : cid;
		}

		case WhenColumn:
			return describeTimestamp(entry.timestamp);

		default:
			break;
	}

	return QVariant();
}

QString Qdc::BoardModel::tooltip(const Node& node, const BoardEntry& entry) const
{
	QStringList lines;

	/*
	 * Every remote-chosen field of it disarmed one at a time, before any of them is put
	 * beside a sentence of ours - which is what stops one of them being mistaken for
	 * part of the sentence. The join happens after, and literalHtml() once over the
	 * finished thing is the caller's business, because a tooltip is drawn as markup.
	 */
	const QString subject = RichText::displayText(entry.subject);
	lines.append(subject.isEmpty() ? tr("(no subject)") : subject);

	const QString nick = RichText::displayText(entry.nick);

	if (!nick.isEmpty())
	{
		lines.append(tr("The hub says the session was called %1 at the time.").arg(nick));
	}

	const QString cid = RichText::displayText(entry.cid);

	if (!cid.isEmpty())
	{
		/*
		 * What the CID means and no more: this hub says it accepted the post from a
		 * session that had authenticated as this. It is the hash of a secret rather
		 * than a public key, nothing can be signed with it, and it is never presented
		 * as a verified identity.
		 */
		lines.append(tr("Submitted, the hub says, by the session %1. That is not proof "
		                "of authorship.").arg(cid));
	}

	lines.append(tr("Accepted by the hub: %1").arg(describeTimestamp(entry.timestamp)));

	lines.append(documentSentence(stateOf(node.tth)));

	if (node.missingParent)
	{
		lines.append(tr("This reply names a parent that is not held here, which is "
		                "ordinary: it may be gone from the index, on a board this "
		                "session cannot read, or from another hub."));
	}

	if (node.withdrawnParent)
		lines.append(tr("The post this replies to is no longer on the board."));

	if (node.flattened)
	{
		lines.append(tr("The thread runs deeper than this list nests, so this reply is "
		                "drawn beside its parent rather than under it."));
	}

	lines.append(tr("TTH: %1").arg(RichText::displayText(entry.tth)));

	return lines.join(QLatin1Char('\n'));
}

QVariant Qdc::BoardModel::data(const QModelIndex& index, int role) const
{
	const Node* node = nodeAt(index);
	if (!node) return QVariant();

	const BoardEntry entry = m_entries.value(node->tth);

	switch (role)
	{
		case TthRole:               return node->tth;
		case EntryRole:             return QVariant::fromValue(entry);
		case StateRole:             return static_cast<int>(stateOf(node->tth));
		case ThreadRole:            return node->thread;
		case ParentTthRole:         return entry.parent;
		case DepthRole:             return node->depth;
		case MissingParentRole:     return node->missingParent;
		case WithdrawnParentRole:   return node->withdrawnParent;
		case FlattenedRole:         return node->flattened;

		/* Escaped once, over the finished text, because Qt draws a tooltip as markup. */
		case Qt::ToolTipRole:       return RichText::literalHtml(tooltip(*node, entry));

		case Qt::ForegroundRole:
		{
			/*
			 * A post nobody could be found for is dimmed rather than labelled. The
			 * entry is the hub's and stands; what is missing is the document, which
			 * may be on a machine that is merely switched off today - so the row
			 * stays where it is, says what it always said, and is drawn quieter than
			 * the posts that can be read.
			 */
			if (stateOf(node->tth) != PostState::Unavailable) return QVariant();

			return QPalette().color(QPalette::PlaceholderText);
		}

		case Qt::FontRole:
		{
			/*
			 * A reply drawn somewhere other than under the post it names is slanted,
			 * which is a mark on the row rather than a word in it: the subject beside
			 * it is a stranger's text, and one of our own words placed there could be
			 * read as part of it. What is odd about the row is in the tooltip.
			 */
			if (!node->missingParent && !node->withdrawnParent) return QVariant();

			QFont slanted;
			slanted.setItalic(true);

			return slanted;
		}

		case Qt::DisplayRole:       return display(entry, index.column());

		default:                    break;
	}

	return QVariant();
}

QVariant Qdc::BoardModel::headerData(int section, Qt::Orientation orientation,
                                     int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return QAbstractItemModel::headerData(section, orientation, role);

	switch (section)
	{
		case SubjectColumn:  return tr("Subject");
		case AuthorColumn:   return tr("Author");
		case WhenColumn:     return tr("Accepted");

		default:             break;
	}

	return QVariant();
}

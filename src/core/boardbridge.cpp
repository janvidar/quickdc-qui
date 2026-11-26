/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#include "boardbridge.h"

#include "corechannel.h"
#include "corethread.h"
#include "hubbridge.h"

#include <string>
#include <vector>

namespace {

namespace Ev = QuickDC::Api::Events;

QString from_std(const std::string& text)
{
	return QString::fromUtf8(text.c_str(), static_cast<qsizetype>(text.size()));
}

std::string to_std(const QString& text)
{
	const QByteArray utf8 = text.toUtf8();
	return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

QVector<QString> from_std(const std::vector<std::string>& list)
{
	QVector<QString> out;
	out.reserve(static_cast<qsizetype>(list.size()));
	for (const std::string& text : list) out.append(from_std(text));
	return out;
}

Qdc::BoardInfo from_core(const QuickDC::Board& board)
{
	Qdc::BoardInfo info;
	info.name         = from_std(board.name);
	info.title        = from_std(board.title);
	info.description  = from_std(board.description);
	info.permissions  = static_cast<quint32>(board.permissions);
	info.maxSize      = board.maxSize;
	info.newest       = board.newest;
	info.oldest       = board.oldest;
	info.posts        = board.posts;
	info.removed      = board.removed;
	return info;
}

Qdc::BoardEntry from_core(const QuickDC::BoardEntry& entry)
{
	Qdc::BoardEntry copied;
	copied.tth        = from_std(entry.tth);
	copied.size       = entry.size;
	copied.board      = from_std(entry.board);
	copied.cid        = from_std(entry.cid);
	copied.nick       = from_std(entry.nick);
	copied.parent     = from_std(entry.parent);
	copied.thread     = from_std(entry.thread);
	copied.subject    = from_std(entry.subject);
	copied.timestamp  = entry.timestamp;
	copied.removed    = entry.removed;
	return copied;
}

QVector<Qdc::BoardEntry> from_core(const std::vector<QuickDC::BoardEntry>& entries)
{
	QVector<Qdc::BoardEntry> out;
	out.reserve(static_cast<qsizetype>(entries.size()));
	for (const QuickDC::BoardEntry& entry : entries) out.append(from_core(entry));
	return out;
}

Qdc::BoardBridge::View from_core(QuickDC::Api::BoardView view)
{
	switch (view)
	{
		case QuickDC::Api::BoardView::Complete: return Qdc::BoardBridge::View::Complete;
		case QuickDC::Api::BoardView::Gap:      return Qdc::BoardBridge::View::Gap;
		case QuickDC::Api::BoardView::Unknown:  break;
	}
	return Qdc::BoardBridge::View::Unknown;
}

QString words(Ev::PostStatus::Why why, quint64 size)
{
	using W = Ev::PostStatus::Why;
	switch (why)
	{
		case W::None:
		case W::Evicted:
			return QString();
		case W::NotAHash:
			return Qdc::BoardBridge::tr("The hub named this post with something that is not a hash, so "
			                            "there is nothing to fetch.");
		case W::Withdrawn:
			return Qdc::BoardBridge::tr("This post has been withdrawn.");
		case W::TooLarge:
			return Qdc::BoardBridge::tr("This post is said to be %1 bytes, which is more than this client "
			                            "will fetch for one post.").arg(size);
		case W::Unsized:
			return Qdc::BoardBridge::tr("Nothing says how large this post is, and no limit is set on what "
			                            "may be fetched for one.");
		case W::Allowance:
			return Qdc::BoardBridge::tr("This board has been asked for as much as it is allowed for the "
			                            "moment. Try again shortly.");
		case W::NobodyAnswered:
			return Qdc::BoardBridge::tr("Nobody who has this post could be reached. It may be on a client "
			                            "that is offline; try again later.");
		case W::Oversent:
			return Qdc::BoardBridge::tr("The peer sending this post sent more than the post was said to be, "
			                            "so the transfer was abandoned.");
		case W::Mismatch:
			return Qdc::BoardBridge::tr("What arrived is not the post that was asked for.");
		case W::NoStore:
			return Qdc::BoardBridge::tr("There is nowhere to keep posts.");
		case W::Unreadable:
			return Qdc::BoardBridge::tr("The document could not be read.");
		case W::OverCap:
			return Qdc::BoardBridge::tr("The document is larger than this client will read for one post.");
		case W::WrongHash:
			return Qdc::BoardBridge::tr("What arrived is not the post that hash names, so it was thrown away.");
		case W::NotAPost:
			return Qdc::BoardBridge::tr("The document is not a post: its header does not parse.");
		case W::KnownBad:
			return Qdc::BoardBridge::tr("The document behind this post is not the post it claims to be, and "
			                            "every copy of it is these same bytes.");
	}
	return QString();
}

QString words(const Ev::PostRefused& refused)
{
	using W = Ev::PostRefused::Why;
	switch (refused.why)
	{
		case W::NoIdentity:
			return Qdc::BoardBridge::tr("This post cannot be written yet: a post says who wrote it, and this "
			                            "installation has no client ID until it has connected to a hub once.");
		case W::NoSubject:
			return Qdc::BoardBridge::tr("A post that starts a thread needs a subject.");
		case W::TooLarge:
			return Qdc::BoardBridge::tr("This post is %1 bytes and this board takes at most %2, so it would be "
			                            "refused. It has to be shorter.").arg(refused.size).arg(refused.limit);
		case W::NotWritten:
			return Qdc::BoardBridge::tr("This post cannot be kept (%1), and a post nobody can send is not "
			                            "published.").arg(from_std(refused.reason));
		case W::NotOffered:
			return Qdc::BoardBridge::tr("This post cannot be offered: %1").arg(from_std(refused.reason));
		case W::NoHub:
			return Qdc::BoardBridge::tr("This post cannot be sent: the hub is not connected, or has no boards.");
	}
	return QString();
}

}

Qdc::BoardBridge::BoardBridge(CoreThread* thread, QObject* parent)
	: QObject(parent)
	, m_thread(thread)
{
	if (m_thread) connect(m_thread->channel(), &CoreChannel::received, this, &BoardBridge::take);
}

Qdc::BoardBridge::~BoardBridge() = default;

QString Qdc::BoardBridge::hubKey(const QString& url)
{
	return HubBridge::settingsUrl(url);
}

QString Qdc::BoardBridge::boardKey(const QString& hubUrl, const QString& board)
{
	return hubUrl + QLatin1Char('\n') + board;
}

void Qdc::BoardBridge::take(const QuickDC::Api::Event& event)
{
	if (const auto* e = event.as<Ev::BoardEntries>())
	{
		const QString hub = from_std(e->hubUrl);
		const QString board = from_std(e->board);
		noteRead(hub, board, e->unread, from_std(e->read), e->whole);

		if (e->whole) emit entriesLoaded(hub, board, from_core(e->entries));
		else emit entriesStored(hub, board, from_core(e->entries));
	}
	else if (const auto* e = event.as<Ev::BoardRefused>())
	{
		emit boardRefused(from_std(e->hubUrl), e->code, from_std(e->message),
		                  from_std(e->board), from_std(e->tth));
	}
	else if (const auto* e = event.as<Ev::BoardRead>())
	{
		noteRead(from_std(e->hubUrl), from_std(e->board), e->unread, from_std(e->read), e->whole);
	}
	else if (const auto* e = event.as<Ev::BoardState>())
	{
		const QString hub = from_std(e->hubUrl);
		if (e->gone)
		{
			emit boardGone(hub, from_std(e->board));
			return;
		}

		BoardInfo info = from_core(e->descriptor);
		info.name = from_std(e->board);
		emit boardStateReady(hub, info, e->described, e->cursor, from_core(e->view), e->left);
	}
	else if (const auto* e = event.as<Ev::PostStatus>())
	{
		using S = Ev::PostStatus::State;
		const QString tth = from_std(e->tth);

		switch (e->state)
		{
			case S::Announced:
				setState(tth, PostState::Announced);
				break;
			case S::Fetching:
				setState(tth, PostState::Fetching);
				break;
			case S::Withdrawn:
				setState(tth, PostState::Withdrawn);
				break;
			case S::Unavailable:
				setState(tth, PostState::Unavailable);
				emit postUnavailable(tth, words(e->why, e->size));
				break;
			case S::Unreadable:
				setState(tth, PostState::Unavailable);
				emit postUnreadable(tth, words(e->why, e->size));
				break;
			case S::Verified:
			case S::Disputed:
			{
				BoardPost post;
				post.tth = tth;
				post.cid = from_std(e->post.cid);
				post.parent = from_std(e->post.parent);
				post.subject = from_std(e->post.subject);
				post.body = from_std(e->post.body);
				post.dated = e->post.dated;
				post.date = e->post.date;
				post.richText = e->post.richText;
				post.state = e->state == S::Verified ? PostState::Verified : PostState::Disputed;

				m_attributed.insert(tth, from_std(e->attributedTo));
				setState(tth, post.state);
				emit postReady(tth, post);
				break;
			}
		}
	}
	else if (const auto* e = event.as<Ev::PostRefused>())
	{
		if (m_writing.remove(e->request)) emit postRefused(words(*e));
	}
	else if (const auto* e = event.as<Ev::PostSubmitted>())
	{
		m_writing.remove(e->request);
	}
}

Qdc::PostState Qdc::BoardBridge::postState(const QString& tth) const
{
	return m_state.value(tth, PostState::Announced);
}

QString Qdc::BoardBridge::attribution(const QString& tth) const
{
	return m_attributed.value(tth);
}

bool Qdc::BoardBridge::isRead(const QString& hubUrl, const QString& board, const QString& tth) const
{
	const auto at = m_read.constFind(boardKey(hubUrl, board));
	return at != m_read.constEnd() && at->read.contains(tth);
}

int Qdc::BoardBridge::unread(const QString& hubUrl, const QString& board) const
{
	const auto at = m_read.constFind(boardKey(hubUrl, board));
	return at == m_read.constEnd() ? 0 : at->unread;
}

void Qdc::BoardBridge::noteRead(const QString& hubUrl, const QString& board, int unread,
                                const QVector<QString>& read, bool whole)
{
	if (board.isEmpty()) return;

	Read& held = m_read[boardKey(hubUrl, board)];

	/* One that passed over a few posts says nothing of the rest. */
	QSet<QString> marked = whole ? QSet<QString>() : held.read;
	for (const QString& tth : read) marked.insert(tth);

	if (held.unread == unread && held.read == marked) return;

	held.unread = unread;
	held.read = std::move(marked);
	emit readStateChanged(hubUrl, board);
}

void Qdc::BoardBridge::setState(const QString& tth, PostState state)
{
	if (tth.isEmpty()) return;

	const auto at = m_state.constFind(tth);
	if (at != m_state.constEnd() && *at == state) return;

	m_state.insert(tth, state);
	emit postStateChanged(tth, state);
}

void Qdc::BoardBridge::loadEntries(const QString& hubUrl, const QString& board)
{
	if (m_thread && !board.isEmpty())
		m_thread->submit(QuickDC::Api::Commands::LoadBoard{to_std(hubUrl), to_std(board)});
}

void Qdc::BoardBridge::read(const QString& hubUrl, const QString& board, const QString& tth, bool fetch)
{
	if (m_thread && !board.isEmpty() && !tth.isEmpty())
		m_thread->submit(QuickDC::Api::Commands::ReadPost{to_std(hubUrl), to_std(board), to_std(tth), fetch});
}

void Qdc::BoardBridge::markAllRead(const QString& hubUrl, const QString& board)
{
	if (m_thread && !board.isEmpty())
		m_thread->submit(QuickDC::Api::Commands::MarkRead{to_std(hubUrl), to_std(board), std::string()});
}

void Qdc::BoardBridge::setPolicy(const QString& hubUrl, Policy policy, std::optional<bool> choice)
{
	using Command = QuickDC::Api::Commands::SetBoardPolicy;
	if (!m_thread) return;

	m_thread->submit(Command{to_std(hubUrl),
		policy == Policy::Serve ? Command::Policy::Serve : Command::Policy::FetchAhead,
		!choice ? Command::Choice::Follow : *choice ? Command::Choice::Always : Command::Choice::Never});
}

void Qdc::BoardBridge::subscribe(const QString& hubUrl, const QString& board, bool fromStart)
{
	if (m_thread && !board.isEmpty())
		m_thread->submit(QuickDC::Api::Commands::SubscribeBoard{to_std(hubUrl), to_std(board), fromStart});
}

void Qdc::BoardBridge::leave(const QString& hubUrl, const QString& board)
{
	if (m_thread && !board.isEmpty())
		m_thread->submit(QuickDC::Api::Commands::UnsubscribeBoard{to_std(hubUrl), to_std(board)});
}

void Qdc::BoardBridge::requestEntry(const QString& hubUrl, const QString& board, const QString& tth)
{
	if (m_thread && !board.isEmpty() && !tth.isEmpty())
		m_thread->submit(QuickDC::Api::Commands::RequestBoardEntry{to_std(hubUrl), to_std(board), to_std(tth)});
}

void Qdc::BoardBridge::withdraw(const QString& hubUrl, const QString& board, const QString& tth)
{
	if (m_thread && !board.isEmpty() && !tth.isEmpty())
		m_thread->submit(QuickDC::Api::Commands::WithdrawPost{to_std(hubUrl), to_std(board), to_std(tth)});
}

void Qdc::BoardBridge::write(const QString& hubUrl, const QString& board, const QString& parent,
                             const QString& subject, const QString& body, bool richText)
{
	static quint64 requests = 0;

	const quint64 request = ++requests;
	m_writing.insert(request);
	if (m_thread && !board.isEmpty()
	    && m_thread->submit(QuickDC::Api::Commands::WritePost{request, to_std(hubUrl), to_std(board),
	                                                          to_std(parent), to_std(subject), to_std(body), richText}))
		return;

	m_writing.remove(request);
	emit postRefused(words(Ev::PostRefused{request, Ev::PostRefused::Why::NoHub, 0, 0, std::string()}));
}

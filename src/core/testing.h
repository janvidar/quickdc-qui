/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_TESTING_H
#define HAVE_QUICKDC_QUI_TESTING_H

/*
 * Reaching past the boundary, for tests that look at what the core did.
 *
 * Nothing in the program may use these: a view that waits on the core freezes
 * for as long as the core's turn takes, and one that holds a core object holds
 * something the core may free at any moment. tests/nothing-blocks-the-views.sh
 * fails the build for a source under src/ that names this namespace.
 */

#include <QList>
#include <QStringList>
#include <QVector>

#include <functional>

namespace QuickDC { class Core; }

namespace Qdc {

class BoardBridge;
class Completed;
class CoreThread;
class Favourites;
class HubDirectory;
class RecentHubs;
struct BoardEntry;
struct BoardInfo;
struct BoardPost;
struct CompletedDownload;
struct HubListEntry;
struct HubListSource;
struct RecentHub;

namespace Testing {

/** Run 'job' on the core thread, and wait for it to have run. */
bool runOnCore(CoreThread* thread, std::function<void()> job, int timeout = 30000);

/** Run 'job' on the core thread, some time after this returns. */
void post(CoreThread* thread, std::function<void()> job);

/** The core, from inside a job; null anywhere else. */
QuickDC::Core* core(CoreThread* thread);

/*
 * Have a bridge hold a list as though the core had announced it, for a test of
 * what is drawn from one that needs no core behind it.
 */
void announce(Favourites& kept, const QStringList& urls);
void announce(RecentHubs& recent, const QList<RecentHub>& hubs);
void announce(Completed& completed, const QList<CompletedDownload>& downloads);
void announce(HubDirectory& directory, const QVector<HubListEntry>& hubs,
              const QList<HubListSource>& sources);

/* What the core says of the boards on the hub filed under 'hubKey'; see BoardBridge::hubKey(). */
void announceBoard(BoardBridge& boards, const QString& hubKey, const BoardInfo& board, bool left = false);
void announceGone(BoardBridge& boards, const QString& hubKey, const QString& board);
void announceEntries(BoardBridge& boards, const QString& hubKey, const QString& board,
                     const QVector<BoardEntry>& entries, int unread, bool whole = false);
void announceRead(BoardBridge& boards, const QString& hubKey, const QString& board, int unread,
                  const QStringList& read, bool whole);
void announcePost(BoardBridge& boards, const BoardPost& post, const QString& attributedTo);
void announceUnreadable(BoardBridge& boards, const QString& tth);

}
}

#endif // HAVE_QUICKDC_QUI_TESTING_H

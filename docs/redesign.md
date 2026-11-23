# Core redesign: boundary, reactive state, lifetime

Tracks the rework of three things found in a review of quickdc and
quickdc-qui (November 2026):

1. **The core is a library with a message-passing boundary** - one owned
   instance, no process globals, commands in and value-snapshot events out.
2. **State is reactive** - settings are typed and announce their changes, and
   whatever is derived from them (port mapping, advertised endpoint, what a hub
   was told) is recomputed and re-applied when an input changes, never read once
   at start-up or login.
4. **Ownership and lifetime are a rule, not a patch** - the core owns its
   objects; front ends hold handles and subscriptions; callbacks never cross a
   thread, only messages do.

Work happens on `redesign` branches in a clone beside the working checkout
(`~/source/quickdc-qui-redesign`), in the superproject and in the `quickdc`
submodule (and `samurai` if it has to move). The working checkout and its
uncommitted quickdc changes are not touched.

## Baseline (before any change)

Superproject `7ae37d1`, quickdc `875bf4f`, samurai `6798e91`, libdrop-cpp
`846316b`, exotic `858e5cf`. Debug, Ninja, all suites on: 1236 ctest entries.

- quickdc core suite: all pass.
- quickdc-qui suite (12): all pass.
- samurai `autotest`: one case fails under `ctest -j32`, passes run alone -
  load-sensitive, not ours.
- libdrop-cpp `droptest`: aborts every run, `std::vector<char>::operator[]`
  bounds assertion (`_GLIBCXX_ASSERTIONS`). Pre-existing; out of scope, noted.
- ASan/UBSan core build (`build-asan`, `-DQUICKDC_SANITIZE=ON`): 78 core tests
  fail at baseline, every one a LeakSanitizer report (adchub, bbs_hub,
  dcclientsession, users, zupload - leaks in hub command handling and in test
  helpers); no other sanitizer errors. The GUI suites do not link with the
  sanitizer on, because the superproject does not add the flags to its own
  targets.

"Green" below means: everything that passed at baseline still passes.

## Rules the end state must satisfy

- R1. No `Core::getInstance()`, no `quickdc_core`. Anything that needs the core
  is handed it by whoever owns it.
- R2. A front end never holds a pointer to a core object. It sends commands and
  receives events; both are plain values.
- R3. A subscription is an object. Destroying it unsubscribes, from any state,
  including from inside the dispatch it is subscribed to.
- R4. No closure is invoked on a thread other than the one it was created for.
  What crosses a thread is a value.
- R5. Every setting is declared once: group, key, type, default. Reading one
  never writes the file. Changing one notifies.
- R6. Derived state has one function that computes it from its inputs, and one
  place that applies the difference. Nothing caches an input "until restart".

## Plan

Order is chosen so that each step lands green and the later ones build on the
earlier ones.

### Phase A - foundations (lifetime and explicit ownership)

- [x] A1. `QuickDC::Subscription` and a dispatch-safe listener list in the core
      (R3). Tested on its own: removal during dispatch, destruction of the list
      before the token, move semantics.
- [x] A2. Convert the core's listener registries (`HubListener`,
      `QueueListener`, `TransferListener`, `AttachmentListener`,
      `DropCheckListener`, `PortForwardListener`, `HubListListener`,
      `DCServerListener`, `PrivateTransferListener`, `JobListener`) to return
      subscriptions. Old add/remove kept as thin wrappers until callers move.
      Done for the true multi-listener registries (transfer, queue) and for
      DropCheck's observer slot, which displaced a second observer. The rest
      are owner-is-listener pointers (Hub, PortForward, HubList, HashJob,
      Transfer) whose listener owns the object, and Share's per-request
      attachment waiters, which Phase B replaces with events.
- [x] A3. Explicit core context (R1): replace `Core::getInstance()` and the
      `quickdc_core` global with a `Core&` handed down from construction -
      managers get it from `Core`, sessions from their manager. ~150 sites.
      `Connection` (base of every peer and hub session and of the probe)
      holds `Core&`; `Hub` holds `Core&` and the manager it registered with;
      `DCServer`, `HubManager` take `Core&`; `QueueManager` takes a nullable
      `Core*` (tests run it alone); the local hub takes its `Preferences&` and
      counts uptime itself; `ADC::CID` is `Core::identity`, built from that
      core's config; `Hash::Manager` reports idle through a callback the share
      manager registers instead of reaching into `core->shares`; share files
      find their manager through their directory; the drop free functions
      take `Core&`; the ADC datagram handler is passed the core.
- [x] A4. File statics in `api/core.cpp` (`mapping_asked_for`,
      `measurement_owed`, the port-forward and drop-check listeners) become
      members of an object owned by `Core` (`Core::Automation`).

### Phase B - the boundary (commands in, events out)

- [x] B1. The core owns its loop and a thread-safe inbox: the job queue and
      notifier move out of `Qdc::CoreThread` into the core (`Core::post`,
      `Core::runUntilStopped`, `Core::stop`). `CoreThread` becomes a host.
      Landed as `QuickDC::Inbox` (open/post/drain/close, tested alone) and
      `Core::post`, `Core::requestStop`, `Core::runUntilStopped(running)`;
      `CoreThread` keeps its state machine and blocking call but owns no
      queue, notifier or loop of its own.
- [~] B2. Value types for what crosses the boundary (`QuickDC::Api`): user,
      hub state, chat line, queue item, transfer, search result, board entry,
      reachability verdict, setting value. No pointers into the core.
      Grown per bridge in `api/protocol.h`; hublist entries so far
      (`api/hublistentry.h`, so the protocol drags in no HTTP headers).
- [x] B3. `Api::Event` (a variant of those) and an `EventSink` the host supplies
      - the core calls it on its own thread with a value; the host marshals.
      `Core::subscribe(EventSink*)` (core thread), fed by `Api::Service`.
- [x] B4. `Api::Command` set and `Core::submit()`, replacing the GUI's lambdas
      that reach into `core->queue`, `core->hubs` and the rest. Mechanism
      in place (`Core::submit`, `Api::Service::execute`); commands are added
      with the bridge that needs them.
- [~] B5. Migrate bridges one at a time, smallest first: hublist, drop,
      media, config, filelist, search, transfer, hub, board. Each one: stop
      touching core objects, stop implementing core listener interfaces, consume
      events. GUI suite green after each.
      - [x] hublist: `FetchHubList` / `HubListFetched` / `HubListFailed`;
            no Relay, no core object, no listener in the bridge.
      - [x] drop: `MeasureReachability`, `LookupPublicAddress`,
            `ReplayReachability`, and the transitional `ApplyPortForwarding`
            and `RepublishClientInfo` (to go in C2/C4); events
            `ReachabilityCheckStarted/Measured/CheckFailed` - failures carry
            a reason code, the GUI words them.
      - [x] media: `AttachFile` / `AttachmentReady` / `AttachmentFailed`;
            the service is the share manager's attachment listener.
      - [ ] config - deferred to after C1: it mirrors the whole preference
            store and seeds it with a blocking call; what replaces it is the
            typed, change-announcing settings C1 builds, and today a value the
            core writes itself never reaches that mirror.
      - [x] filelist: `RequestFilelist` / `FilelistReady` (decoded tree as
            `Api::RemoteDirectory`) / `FilelistFailed`. The queue now announces
            a file list request ending (`QueueListener::EventFilelistFinished`),
            so the bridge's one-second poll of the pending set is gone.
      - [x] search: `Search` / `SearchSent`. Results still arrive through
            the hub bridge's listener and move with it.
      - [x] transfer: `Enqueue`, `EnqueueFromSearch`, `EnqueueForReading`,
            `AddFallbackSource`, `SetPriority`, `RemoveFromQueue`,
            `SnapshotTransfers`; events `Transfer{Started,Finished,Stopped,
            Error}`, `QueueChanged`, `DownloadCompleted`, `TransfersSnapshot`.
            The service is the queue's and the transfer manager's listener.
      - [x] hub: the session is the core's (`Api::Service::HubSession`),
            opened under a front-end-chosen `HubHandle` (two openings of one
            address are two sessions). 14 commands, 21 events; history read
            from the chat archive by command. Redirect/peer policy is handed
            over in advance (`SetHubPolicy`) instead of being atomics the core
            thread read out of a box the bridge shared.
      - [ ] board
- [ ] B6. Remove `CoreThread::core()`, `runOnCoreBlocking`, `Relay` (R2, R4).

### Phase C - reactive state

- [ ] C1. Settings schema (R5): one declaration table, typed accessors, defaults
      in one place, reads that never write, change notification as an event.
      Versioned migrations replace one-off fix-ups (the withdrawn hublist, the
      connection-mode settlement).
- [ ] C2. Network plan (R6): connection mode + port + bind address + forwarding
      + drop server -> one computed plan; one reconciler applies the difference
      (listen sockets, gateway mapping, measurement, INF re-announce to every
      hub). Replaces the `mapping_asked_for` / `measurement_owed` state machine.
- [ ] C3. Listen port and bind address change without restart.
- [ ] C4. Front end binds settings pages to setting/plan events; remove the
      "tell the hubs again" control and the settings-path republish calls.

## Decisions

- D1. Work in a separate clone, on `redesign` branches, so the working
  checkout (with uncommitted diagnostic work in quickdc) is untouched.
- D2. samurai's own singletons (`SocketMonitor`, `MessageHandler`,
  `TimerManager`, `OSBase`) are left for now. They are the platform layer's,
  and R1 is about the core. Revisit after Phase B, since they are what still
  stops two cores existing in one process.
- D4. The GUI-isolation rule stays: `qdcgui` cannot see a core header
  (enforced by the link). So `Api::Event` and `Api::Command` are structs
  around their variants (forward-declarable), and the event signal lives on
  `Qdc::CoreChannel`, whose header only bridge sources include.
  `CoreThread` subscribes one sink and re-emits through the channel; Qt's
  queued connection is what carries the value across, and its automatic
  disconnect is what ends a destroyed bridge's interest - which is R3/R4
  without a Relay.
- D5. A hublist fetch now belongs to the core's service, not the bridge
  that asked: a bridge destroyed mid-fetch lets it finish and hears
  nothing. Previously it aborted the download.
- D6. A request whose answer is broadcast carries an id the answer echoes
  (`SnapshotTransfers.request`), because every sink hears every event: a
  bridge took a snapshot a destroyed bridge had asked for, queued ahead of
  its own commands, and read the queue as it was before them. Found by
  `tst_search`.
- D3. Keep the protocol in-process and C++-typed (a `std::variant`), not
  serialised. Serialising for a remote front end is easy later from value
  types and costs nothing to defer.

## Deferred / found along the way

- Fixed in passing (A3): the CID singleton outlived the core that read it, so
  a second core in a process announced the first one's identity; a share
  file in a subdirectory announced its hash through the global core rather
  than its own manager, which was wrong for any manager not installed as
  `core->shares`; `~HubManager` deleted the hubs still registered with it
  and each one then unregistered from the manager being destroyed.
- Front-end hub sessions that outlive a stopped core are deleted by
  `~HubManager`, not leaked; the GUI's `SessionBox` pointer dangles after
  that but nothing reaches it (jobs posted after stop are dropped). B1
  makes this explicit.
- `Preferences::setGroup()` is a cursor shared by every reader - C1.
- The 78 baseline LeakSanitizer reports (see Baseline).
- Every sink is told every event, and Qt queues a copy per connected
  bridge: a hub's user list at login is thousands of `UserUpdated` events,
  each copied to ~10 receivers that mostly ignore it. Fine so far; if it
  shows, route by handle in `CoreChannel` (one signal per hub) before
  anything cleverer.
- Search tokens are numbered per SearchBridge from 1, so two bridges would
  hand out the same token and take each other's SearchSent (and results).
  Pre-existing; the app builds one. Move numbering into the core when the
  hub bridge's results move.

- libdrop-cpp `droptest` bounds assertion (baseline).
- samurai `autotest` load sensitivity under `-j32` (baseline).

## Log

- 2026-11-23: clone created, baseline built and run, plan written.
- 2026-11-23: A1+A2 landed (quickdc `39a5c4e`). Full suite green.
- 2026-11-23: A3+A4 landed. No `Core::getInstance`, `quickdc_core`,
  `CID::getInstance` or `global_cid` left anywhere. Full suite green (1247);
  ASan core failures identical to baseline, no new sanitizer errors.
- 2026-11-23: B1 landed. Full suite green (1254, 7 new inbox cases).
- 2026-11-23: B3/B4 mechanism + hublist bridge migrated (B5). 1258 + 2 new
  GUI cases, green.
- 2026-11-23: drop and media bridges migrated. Green.
- 2026-11-23: filelist bridge migrated, polling replaced by an event. Green.
- 2026-11-23: search bridge migrated. Green (1260).
- 2026-11-23: transfer bridge migrated; snapshot requests correlated (D6). Green.
- 2026-11-23: hub bridge migrated; GUI holds no core hub object. Green (1260).

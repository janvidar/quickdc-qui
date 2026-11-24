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
  helpers); no other sanitizer errors. The GUI suites did not link with the
  sanitizer on, because qdcbridge links the core PRIVATE and so passed none
  of samurai's flags on - fixed on this branch (see Log).

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
- [x] B5. Migrate bridges one at a time, smallest first: hublist, drop,
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
      - [x] config: one settings copy in `CoreChannel`, seeded from a
            snapshot the core takes as it starts, kept current by
            `SettingChanged`; writes go ahead locally and are acknowledged by
            `SettingsWritten`. No blocking seed; core-written values now reach
            the views.
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
      - [x] board: the index and post store are the core's
            (`Api::Service::BoardStore`, `api/service_boards.cpp`), one per
            bridge under a `BoardStoreHandle`; 13 commands, each answered by
            an event echoing its request number. The bridge keeps its
            completion code and runs it on its own thread when the matching
            answer arrives (`BoardBridge::ask`). Checking that a document is
            the post it was asked for (tiger tree root, parse, author) moved
            into the core with it, and its refusals became codes.
- [x] B6. Remove `CoreThread::core()`, `runOnCoreBlocking`, `Relay` (R2, R4).
      `Relay` and `call()` are gone; `core()`, `post()` and the blocking call
      are private, reachable only through `Qdc::Testing` (src/core/testing.h)
      for white-box tests, and the lint in tests/nothing-blocks-the-views.sh
      now fails any source under src/ that names it.

### Phase C - reactive state

- [x] C1. Settings schema (R5): one declaration table, typed accessors, defaults
      in one place, reads that never write, change notification as an event.
      Versioned migrations replace one-off fix-ups (the withdrawn hublist, the
      connection-mode settlement).
      Done: every change to a `Preferences` is told to its subscribers with a
      revision (C1a), which is what the config migration stands on. C1b:
      `config/settings.h` declares every key the core reads (group, type,
      default) and every core read of the configuration goes through it,
      peeking - none writes a default back. A saved empty bind address means
      the default. The GUI reads the same declarations, re-exported by the
      bridge as `Qdc::Settings` (src/core/uisettings.h), with the interface's
      own settings declared there once (2026-11-24, see Log).
      Migrations: `config/migrations.{h,cpp}` - numbered steps, each run once,
      `[Settings] Version` recording how far an installation got; run in the
      core's constructor before anything reads either store. Step 1 settles
      the connection mode (was a one-off call), step 2 unpins hub identities
      left by closed sessions (nickname or password equal to the global one,
      or the product-name fallback), step 3 resets a reachability server the
      page can no longer change. The withdrawn-hublist filter stays in
      the GUI: it is idempotent on every load, not a one-time step.
- [x] C2. Network plan (R6): connection mode + port + bind address + forwarding
      + drop server -> one computed plan; one reconciler applies the difference
      (listen sockets, gateway mapping, measurement, INF re-announce to every
      hub). Replaces the `mapping_asked_for` / `measurement_owed` state machine.
- [x] C3. Listen port and bind address change without restart.
- [x] C4. Front end binds settings pages to setting/plan events; remove the
      "tell the hubs again" control and the settings-path republish calls.
      Landed together: `Core::Automation` subscribes to the configuration,
      notes which of [Network], [User] and [Local hub] moved, and once per
      loop turn computes the plan (`planFrom`, peeks only) and applies the
      difference from the last one applied - reopening the peer port
      (`DCServer::listen(Endpoint)`), starting/stopping/moving the gateway
      mapping, measuring again or looking the address up, telling the hubs
      (throttled), and waking the local hub. `ApplyPortForwarding`,
      `RepublishClientInfo`, `AnnounceSettingsChanged`, the bridge methods
      behind them and the settings page's hand-ordered sequence are gone;
      the page no longer says a port change waits for a restart.
      Not yet: the check-map-recheck state machine (`mapping_asked_for`,
      `measurement_owed`) still drives the measurement loop itself; folding
      it into the plan is the next step if it earns it.

### Phase E - samurai's event loop (D2)

Goal: nothing in samurai, libdrop or the core reaches for process-wide state, so
two cores - or two test environments - can live in one process without sharing a
poll, a timer heap or a message queue.

- [x] E1. `Samurai::EventLoop` owns the socket monitor, the timers, the posted
      messages, the DNS resolver pool and the bandwidth counters; the
      `getInstance()`s and `setSocketMonitor()` go. `OS` stays: it only caches
      facts about the host.
- [x] E2. Everything that registers with a loop is given one: every socket
      constructor (an accepted socket takes its server's), `Timer`,
      `MessageListener` and posting, `InetAddress::lookup()` and the resolvers,
      and the components built on them (HTTP client and `fetch()`, UPnP SSDP,
      gateway and port mapper, SOCKS resolver).
- [x] E3. A loop destroyed first detaches what still points at it - registered
      sockets, timers, listeners - so a late destructor does nothing rather than
      touch freed memory. Otherwise the rule is plain: a loop outlives what is
      made on it.
- [x] E4. samurai's and libdrop's tests make a loop per case (or per file)
      instead of priming singletons.
- [x] E5. The core owns one `EventLoop` and makes everything through it; its
      tests and helpers likewise. A test runs two cores in one process, each
      with its own sockets and timers.

### Phase F - a headless front end, and a GUI with no core policy

Goal: everything worth testing - hubs, chat, search, file lists, peer transfers,
boards, settings - can be driven and checked from a plain shell, by a script, with
no display; and the GUI is a renderer of what the core says, holding no decision
about core behaviour. The same `Api` serves both, so whatever the CLI gains the GUI
can use, and nothing the GUI needs is out of the CLI's reach.

The CLI (`qdc`, in the quickdc repository beside the core, not under test/):
- Runs each core on a thread of its own via `Core::runUntilStopped()`, sends
  `Api::Command`s with `submit()`, receives `Api::Event`s through a subscription
  handed to its own thread - the GUI's shape, without Qt. It may include the
  protocol header and nothing else from the core, enforced by the link as the GUI's
  isolation is.
- Several cores in one process (`--core a --core b`, each its own home directory);
  a line addressed `@a` goes to that core. Two cores meeting on one's local hub over
  loopback is how a peer transfer is tested without the internet.
- Scripted first: one command per line from a file or stdin, mapping one-to-one
  onto `Api::Commands` (`@a open adc://127.0.0.1:N`, `@b search foo`, ...);
  `wait <Event> [field=value ...] [timeout=10s]` blocks until a matching event or
  fails; `expect` checks state; events go out as JSON lines; the exit status is
  the verdict. An interactive mode is the same parser reading a terminal.
- Scenarios are ctest tests: a script plus the files it shares and expects.

- [x] F1. The CLI: core threads, the event pump, the script language, JSON event
      output, `wait`/`expect`, exit status, `--home`; hubs, chat and settings.
- [x] F2. Offline scenarios on loopback: join a local hub, chat, private message,
      search, file list, download (active, passive, TLS), as ctest tests.
- [ ] F3. Core policy out of the GUI, each step covered by a CLI scenario before
      the GUI is switched to it: search results matched to their search in the
      core; picture auto-fetch policy and limits; the board store, fetching ahead
      and withdrawn posts; the public hub directory (merge, cache); favourites,
      recent hubs and completed downloads as core state; share folder management;
      the connection-mode derivation (one copy, the core's).
- [ ] F4. The old `quickdc_cli` retired: what it does that the `Api` lacks (hub
      operator actions, local hub administration, the connection list, the local
      TTH search) added as commands and events, or dropped.
- [ ] F5. A project skill for driving it from the AI loop: build, run a scenario,
      read the output.

## Decisions

- D1. Work in a separate clone, on `redesign` branches, so the working
  checkout (with uncommitted diagnostic work in quickdc) is untouched.
- D2. samurai's own singletons (`SocketMonitor`, `MessageHandler`,
  `TimerManager`, `OSBase`) were left through Phase B. Taken up 2026-11-24
  (Phase E below).
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
- ~~The 78 baseline LeakSanitizer reports~~ - gone 2026-11-24 (see Log).
- ~~Every sink is told every event, and Qt queues a copy per connected
  bridge~~ - done 2026-11-24: an event with a hub goes to that hub's
  `HubRoute` only (see Log).
- ~~Search tokens are numbered per SearchBridge from 1~~ - already settled
  in B5: `SearchBridge::start()` numbers from one process-wide counter, and
  a result carries its token back to the search that asked. Nothing to move.

- ~~libdrop-cpp `droptest` bounds assertion~~ and ~~samurai `autotest` load
  sensitivity~~ - both fixed 2026-11-24, both samurai bugs (see Log); neither
  suite needs leaving out of a run any more.

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
- 2026-11-23: board bridge migrated. Every bridge but config is on the boundary. Green (1260).
- 2026-11-23: config bridge migrated (B5 complete), B6 done, C1a done. Green (1264).
- 2026-11-23: C2, C3, C4 landed. Green (1269, 5 new network plan cases).
- 2026-11-23: sanitizers reach the GUI (qdcbridge re-exports samurai's
  sanitizer flags PUBLIC; tests/lsan.supp for Qt's D-Bus meta-objects and
  fontconfig's cache). Whole suite under ASan/UBSan: only the 78 baseline
  leak cases fail; every GUI suite passes; no non-leak report anywhere.
- 2026-11-23: fixed a C3 regression before it shipped: the server no longer
  writes Port=1411 at start-up (the plan peeks), and the hub sessions read Port
  with a default of 0 that their read wrote back - so a fresh configuration
  would have stored Port=0 and the reconciler rebound the peer port to it.
  Every hub-session Port read now peeks with 1411. This is exactly what C1b
  (one declared default per setting, reads that never write) removes for
  good; 79 call sites remain to convert (inventory: adchubsession 24,
  dc/hubsession 14, adc/server/hub 14, sharemanager 6, the rest small).

## Review findings (adversarial review of the whole diff, 2026-11-23)

- Port=0 at first login once nothing stored the port - fixed (C1b removes the
  class: one default, reads that never write).
- Address defaulted "" in the plan and "0.0.0.0" in the sessions, so the
  first login looked like an advertised change - fixed by the one default.
- A second fetch of a busy hublist failed the first for everyone - fixed.
- Save request numbers and search tokens counted per bridge while answers
  are broadcast - fixed, numbered across bridges.
- SetPriority makes every TransferBridge refresh - accepted, harmless.
- Checked and found correct: lifetimes (Core/Service/HubManager order,
  CloseHub, ListenerList), Inbox/CoreThread threading, settings mirror
  ordering and pending logic, line-for-line equivalence of every migrated
  job.

## Next

- samurai's own singletons (D2). The last of the global state; worth it only
  to run more than one core in a process, or to test with several.
- 2026-11-23: smoke test of the real app (driver in the session scratchpad):
  port change without restart, local hub enabled live, hublists, a public
  ADCS hub, mode switches, config file holding only what was saved - all
  as intended. Found (pre-existing): a hub session read nickname, description
  and e-mail once, so a rename never reached a joined hub; and closing a
  session wrote the name it used back as that hub's own, outranking the
  setting there for good. Fixed: `Hub::refreshIdentity()`, run from the
  reconcile step on any [User] change or per-hub identity change in the
  session store - description and e-mail go out with the next INF, a
  nickname change reconnects; sessions no longer write the nickname back.
  Verified against adcs://adcs.uhub.org:1511: joined as qdc-smoke-a,
  renamed -> rejoined as qdc-smoke-b, description updated in place, no
  per-hub nickname left in session.conf. 1273 tests green.
  Still pinned the same way: the password (~HubSession writes it back,
  so a global password is copied into every hub's group) - harmless today
  but the same shape; worth the same treatment.
- 2026-11-23: settings migrations; pinned identities unpinned once on upgrade; sessions no longer write back the password either. Green (1278).
- 2026-11-23: the check-map-recheck state folded: `network/mappingpolicy.{h,cpp}`
  decides whether to hold a gateway mapping from the plan's wish and the
  evidence (useless only when a check *started* through it; cleared on a
  configuration change; a refused one re-asked while still needed). Every
  start/stop goes through `MappingPolicy::apply()`, via the plan
  (`AutomationState::applyMapping`) - the reconciler and the drop listener no
  longer each drive the gateway. Behaviour change: a mapping made at start-up
  (automatic mode) is now let go on the same evidence as one the loop asked
  for; the ownership distinction existed only for front-end-made mappings,
  which no longer exist. 9 new cases; 1287 green; ASan unchanged.
- 2026-11-24: smoke run on the maintainer's own setup found four things, all
  fixed and verified there:
  - Queued sources (quickdc `7a4c39e`, `c24e568`): a source that could not be
    connected to was asked again on every scheduler pass, several times a
    second, for ever - it was claimed after the ask and a failed ask recorded
    no failure. A source's user is now looked up on whichever hub they are on
    (a CID is the same on every hub), one who is away is left for the retry
    interval without counting against the source, and a search reply relayed
    by a hub no longer records a source with an empty CID.
  - Migration step 3 (`51659f9`): a reachability server stored before the
    page stopped offering one (here `drop.extatic.org`, which no longer
    resolves) outranked the default for good, so every check failed. Put
    back to `drop.quickdc.org` once; an empty one stays empty (that is how a
    client opts out of asking a third party).
  - "Occupied" on a retry (`6a089fb`, pre-existing): the probe answered a
    DROP challenge from whatever had arrived when the connection became
    readable. Split across segments, half the nonce went back - or none,
    when the prefix itself was cut and the line was dropped as unknown - and
    the server read a wide open port as taken by something else. A challenge
    is now read off the socket and answered from the whole line.
    `challengeProgress()`; three cases. Six rechecks in a row against the
    real server: all open; with the old probe, one of six occupied.
  - IPv6 on a host with no IPv6 route (`722b5b4`, `a57802d`): the check ran
    after every IPv4 one and again on every refresh (a check that never
    completes is never fresh), and logged an ERROR each time. Skipped where
    no interface that is up holds a global address (`hostHasGlobalIPv6()`:
    link-local and the unique-local one a VPN overlay hands out do not
    count); the error line, a duplicate of the debug one, is gone.
  Pushed to gitea; the redesign clone is deleted. 1300 green.
- 2026-11-24: passwords no longer pinned per hub. The migration unpinned the
  copies already made, but two paths still made new ones: NMDC wrote an empty
  password into the hub's group when the hub refused one - and an empty value
  per hub outranks the global password for good - and typing the global
  password at a hub's prompt kept a copy of it there. `Settings::writeForHub()`
  keeps a per-hub value only where it differs from the global one and is not
  empty; a refused password is removed on both protocols (ADC kept offering it
  on every run). 4 cases, each failing without the fix.
- 2026-11-24: one definition per setting, GUI included (C1b's last part). The
  settings page, hubs view, hub pane, search, home page and theme read every
  setting through `Qdc::Settings` (src/core/uisettings.h): the core's from its
  schema, re-exported by the bridge library (`constinit`) because a view may
  not include a core header, and the interface's own (chat media, boards,
  theme) defined there once rather than in two views. `ConfigBridge::read()`
  / `write()` take a setting. The copies had drifted: an unset nickname and
  description showed empty while the core announced "QuickDC" and "QuickDC
  user" (and Save then wrote the empty ones); "Active mode" defaulted on in
  the GUI and off in the core. `[Network] Mode` and `[Downloads] Directory`
  joined the core schema. The home page still asks for a nickname until one
  other than the default is chosen.
- 2026-11-24: hub events routed. `CoreChannel::deliver()` sends an event with
  a hub to that hub's `HubRoute`, queued once to the channel and handed on in
  the GUI thread; everything else stays on `received()`. Only `HubBridge`
  consumed hub events, so nothing else changes. Previously each `UserUpdated`
  at login was queued as a copy to every bridge (~10). 1 case; identity,
  census and full smoke scenarios pass against adcs://adcs.uhub.org:1511.
- 2026-11-24: the build is warning-free. The NMDC client tag was written into
  a fixed 50 bytes, which a long version or large counts cut short - '>'
  included; it is now built to length, with a test. The rest were cosmetic.
- 2026-11-24: LeakSanitizer clean. The 78 failures were 14 distinct leaks, three
  of them real: `Hub::send()` dropped its reference to a command no queue took
  without deleting it; the welcome message was allocated on every login to the
  local hub and leaked when there was no MOTD file (the usual case); and every
  ADCGET/ADCSND copied its file name for a session that only borrows it. The
  rest were test leaks. Whole suite under ASan/UBSan: 1306 of 1306.
- 2026-11-24: the two library suites run clean, so `-E '^(autotest|droptest)$'`
  is no longer needed. `droptest` aborted on samurai's `Buffer::append()`
  taking `&buf[0]` of an empty buffer for an empty datagram. `autotest`'s load
  failure was `sockets_a_second_connection_can_share_a_local_port`, and behind
  it a real bug: NAT traversal dials from the hub connection's port, but the
  hub connection was never made shareable, so on Linux every traversal failed
  to bind (EADDRINUSE) before dialling - the existing tests stopped at the DNAT
  /RNT exchange. The ADC hub connection is now opened with
  `setReuseLocalPort(0)`, and samurai binds a shareable socket explicitly even
  for port 0, since a port `connect()` assigns can be handed to other processes'
  connections, which is what failed under load. A new test dials a loopback
  peer through traversal and checks it arrives from the hub port. 40 parallel
  runs of `autotest`: no socket failures (3-6 before); full suite 1353 green
  three times running.
- 2026-11-24: Phase E (D2) done. `Samurai::EventLoop` owns the socket monitor,
  timers, posted messages, bandwidth counters and DNS resolver pool; every
  socket, timer, message listener and lookup, and the HTTP, UPnP, SOCKS and
  libdrop components on them, take the loop explicitly. No `getInstance()` is
  left but samurai's `OS` (host facts) and the CLI tool's console (stdin). A
  loop destroyed first detaches its sockets, timers, listeners and resolvers.
  Found on the way: OpenSSL's process state was torn down by whichever monitor
  went first - now reference-counted; messages were process-wide, so one core's
  posts reached another's listeners; the local hub's claim-check record was a
  file-scope list shared by every hub, now `ClaimChecks` per hub. The core owns
  its loop (declared first, destroyed last); `run()` keeps its turn order (poll
  and due timers, hash, transfers, queue, timers, messages, reconcile).
  `twocores.tcc` runs two full cores in one process - sockets, timers and
  messages dispatched only by their own core, both running on two threads at
  once, either destroyed first. samurai 1184 + 11 new, libdrop 109, full suite
  1360, sanitizer build 1313, all green; the four smoke scenarios pass against
  adcs://adcs.uhub.org:1511 and drop.quickdc.org.
- 2026-11-24: F1 done. `qdc` (quickdc/cli, namespace QuickDC::Cli): cores on
  threads of their own via `runUntilStopped()`, commands in by `submit()`, events
  out as JSON lines; `preset`/`set`/`save`/`open`/`connect`/`chat`/`pm`/`wait`/
  `sleep`; one file may include api/core.h, the rest only api/protocol.h (lint
  `cli_isolation`). The core gained `Core(const Core::Home&)`: everything a core
  writes - settings, session, queue, hash cache, chat, keys, lists, downloads, the
  local hub's acl, motd and boards - lives under that directory, and presets are
  applied before migrations so a test core contacts nobody. `Core()` is that with
  ~/.quickdc, unchanged for the GUI (smoke scenarios pass).
  The two-core chat scenario found a real race the move to a loop per core had
  opened: the ADC and NMDC command proxies read into one static buffer, so two
  cores on two threads read each other's bytes (a client saw the hub's greeting
  to the other core where its own user list belonged, about one run in twenty).
  Every such scratch buffer is now per thread (both proxies, SID and hash
  formatting) or local (the hub's uptime message), and the process-wide counters
  (timer serials, connection tickets, manual connection tokens) are atomic.
  200 runs in a row and 3x32 in parallel: no failures. Still process-wide by
  nature: TLS keys (the last core built presents its certificate for all - F2's
  TLS transfers between two cores in one process will need per-core keys), the
  trace log files, the default proxy settings. Api gaps noted for F3/F4: no event
  for the listening port or the local hub coming up, no core-up/snapshot event,
  private messages need the hub's id for a user, no acknowledgements for
  OpenHub/Connect/Chat, HubExtensions repeated per extension.
- 2026-11-24: F2 done, but for passive to passive. qdc gained `file`, `share`,
  `share-status`, `search`, `download`, `browse`, `transfers`, `priority`,
  `dequeue`, `expect-file`, and `wait` gained `*`, `!=` and `remember`/`$NAME`.
  Api: `ReportShare` / `ShareStatus` (files, size, still hashing - published
  whenever hashing falls idle), and `Transfer` carries `secure` and
  `peerKeyprint`. Scenarios on loopback: search, file list, active download,
  passive download (reverse connect), encrypted download in both directions
  checking each side saw the other's own certificate; 54 runs each, half in
  parallel, no failures. TLS keys are per event loop (`TlsContext`, a loop
  without keys uses the process-wide ones), so each core presents its own
  certificate. Found and fixed: every file list uploaded over ADC (and NMDC's
  ADCGET) leaked; the ADC client never read SF; parallel runs could be offered
  the same free port (now claimed for the life of the process); a download that
  occasionally never started, last seen with the process-wide OpenSSL context
  that two core threads released and rebuilt under each other - not seen in
  ~800 runs since contexts became per loop, so likely, not proven.
  Passive to passive cannot work on loopback: with no NAT to drop the first SYN,
  the kernel answers it with a reset before the other side's dial exists, so the
  simultaneous open traversal depends on never lines up. Testing it needs an
  emulated NAT (network namespaces, root) or the traversing side briefly
  listening on its hub port. Open for F3/F4: a passive core still sends active
  connection requests if "Active mode" is stored as 1 (peer connections read the
  raw key, not the mode - the connection-mode derivation item); search results
  carry no free slots; no event says which connection path a transfer took;
  NMDC's legacy $Get/$GetListLen still have the file list ownership pattern;
  the local hub has no TLS, so no adcs:// scenario.

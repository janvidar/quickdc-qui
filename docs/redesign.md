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
- [x] F3. (batch a 2026-11-25: connection mode, share folders, favourites/recent/completed; batch b: search attribution, picture policy; batch c: the public hub directory; batch d: bulletin boards) Core policy out of the GUI, each step covered by a CLI scenario before
      the GUI is switched to it: search results matched to their search in the
      core; picture auto-fetch policy and limits; the board store, fetching ahead
      and withdrawn posts; the public hub directory (merge, cache); favourites,
      recent hubs and completed downloads as core state; share folder management;
      the connection-mode derivation (one copy, the core's).
- [x] F4. The old `quickdc_cli` retired: what it does that the `Api` lacks (hub
      operator actions, local hub administration, the connection list, the local
      TTH search) added as commands and events, or dropped.
- [x] F5. A project skill for driving it from the AI loop: build, run a scenario,
      read the output.

### Phase G - emulated networks

Goal: test what loopback cannot - NAT traversal between two passive peers, UPnP
port mapping against a gateway, reachability checks, IPv6 variants and bad links -
with rootless podman: a fake internet network, home networks behind router
containers doing NAT with nftables, qdc in each client container.

- [ ] G1. qdc across processes: a control socket so one script drives qdc
      processes in several containers (`@a` = the qdc in container a), same script
      language, waits on any of their events.
- [ ] G2. Images (a base matching the host distribution with the build tree mounted
      read-only; a router image with nftables, iproute2, miniupnpd, tc) and a
      topology runner that sets networks up and tears them down; a separate ctest
      label, skipped where podman is missing.
- [ ] G3. NAT traversal: two passive clients behind NATs that drop unsolicited
      inbound, transfer through traversal; port-preserving vs symmetric NAT.
- [ ] G4. UPnP against miniupnpd, a local reachability server inside the network
      (from libdrop if it has one, else a small test server), and the "occupied"
      verdict seen with forwarding on.
- [ ] G5. IPv6 (dual stack, v6 only, no v6 route) and impaired links (tc netem).

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
- ~~Search tokens are numbered per SearchBridge from 1~~ - settled in B5 by a
  process-wide counter, and gone in F3b: the core numbers each search and
  draws its wire token, so no front end invents either.

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

- 2026-11-25: F3a done: the connection-mode derivation, share folder management,
  and favourites, recent hubs and completed downloads moved into the core.
  Api: `SetConnectionMode` / `ConnectionMode` (the mode in force and what it
  decides; the GUI mirror `src/core/networkmode.*` is gone and the page sends the
  mode with advanced's two switches); `AddShareFolder` / `RemoveShareFolder` /
  `RenameShareFolder` answered by `ShareFolders` + `ShareStatus` or
  `ShareFolderRefused`; `AddFavouriteHub` / `RemoveFavouriteHub`,
  `ForgetRecentHub` / `ClearRecentHubs`, `ForgetCompletedDownload` /
  `ClearCompletedDownloads` answered by `FavouriteHubs`, `RecentHubs`,
  `CompletedDownloads`; `ReportState` and `Core::state()` tell all five lists,
  the latter taken with the settings snapshot so a bridge is seeded without
  waiting; `Transfer::outbound` says which side dialled. The core records a visit
  on OpenHub and the name from HubName, and a completion on verification; the
  storage formats are the ones the GUI wrote, so no migration. Shares are live:
  a folder added is scanned and hashed at once, one taken out leaves every table
  and count at once and its files are freed when no upload reads them. The GUI
  classes keep their names as bridges over the announcements. qdc: `mode`,
  `state`, `share-add/remove/rename`, `favourite-add/remove`,
  `recent-forget/clear`, `completed-forget/clear`, `expect-no`; scenarios
  download-passive-stale-active, share-live, favourites, recent-hubs, completed,
  each 78 runs (30 in a row, 2x24 in parallel) without a failure.
  Fixed: a core in Mode passive with Active mode 1 still sent CTM and announced
  itself active - every peer path read the raw key; they now ask
  `networkModeKeysInForce()` (the plan uses it too), forced-active included. A
  core built from explicit files would write its file lists to ~/.quickdc
  whenever hashing fell idle (hashing an attachment is enough); they now go
  beside its configuration. 1399 green; sanitizer build 1352 green.
- 2026-11-25: F3b done: search results matched to their search, and the picture
  policy, in the core. Api: `Search` carries the caller's `request` instead of a
  token; the core numbers the search, draws its wire token, and answers
  `SearchSent{request, search, hubsAsked}`; `SearchResult` names its `search`
  and is not told at all when it answers none; `ForgetSearch` ends one (closing
  a tab sends it). `Share::SearchManager`, unused until now, is rewritten as the
  attribution: by token, else - NMDC - the newest search whose words the name
  holds and none of whose exclusions it does (the exclusions are new; the GUI
  matched words only), at most 64 live. The GUI's token counter and its name
  matching are gone; it files a result by its number. Pictures:
  `core/chat/chatmedia.*` reads the magnets a markdown message links to (links,
  images, autolinks; not in code spans or blocks - a scanner to the renderer's
  rules, since the GUI's markdown parser is Qt's) and decides by [Chat media]
  and the hub's own session group (`hubSettingsGroup()`, now also the GUI's
  `settingsUrl()`); a rich message's pictures are announced as
  `ChatMedia{hub, tth, name, size, state Queued|Ready|Skipped, why
  Off|Unsized|TooLarge|Refused, path}` - Ready at once where the file is
  attached or downloaded here, else queued with the sender as a known source and
  a source search, Ready again on verification and offered again by its hash.
  `FetchAttachment` fetches whatever the policy says (a clicked magnet),
  `SetChatMediaFetch{hubUrl, Follow|Always|Never}` writes the hub's choice,
  `AttachPasted{request, data, suffix}` keeps pasted bytes under
  `<home>/media` (answered by `PastedKept`, then AttachmentReady), and the core
  sweeps old pasted pictures at start. The settings and stored keys are the ones
  the GUI wrote, so no migration; the settings move to the core's schema. A core
  built from files keeps its media and default downloads beside its
  configuration (`Core::homeDirectory`). The GUI lost fetchMediaIn, reseed,
  fetchesMediaOn, the policy cache, the sweep, the paste folder and its
  attachmentsIn; it renders `ChatMedia` Ready paths and still reads the hub's
  choice to tick the menu. qdc: `search ... as NAME` remembers the core's number,
  `search-forget`, `attach`, `paste`, `chat-md` (with `$NAME`), `media-fetch`,
  `media-policy`; scenarios search-attributed (two cores, three searches in
  flight) and chat-media (fetched and byte-identical, already here, over the
  limit, offered not shown, inside code, off on the hub then fetched by hand,
  pasted), each 78 runs without a failure. Found while stressing: the first cut
  gave the sender's SID as the known source, the queue keys sources by CID, so
  the peer became two sources and the two connections fetched the leaf data in
  turn for ever (one run in three); the CID is given now. That the queue lets
  one peer be two sources and then never gets past the leaves is left as found.
  The smoke driver gained a `search` scenario on the public hub. 1415 green;
  sanitizer build 1368 green.
- 2026-11-25: a transfer bug found by the chat-media scenario failing one run in
  24: `adc_connect_token()` dropped a leading "TO" from any CTM/RCM/NAT/RNT token
  as if it were the ADC 0.12 field label, so one token in 1024 (ours are random
  base32) was read short, the peer dialled back quoting it, nobody recognised
  it, and the transfer sat idle until the request timed out after 120 s. Taken
  whole now; test `hubsession_a_connect_token_starting_with_to_is_kept_whole`
  failed before. Still open: a peer connection with an unknown token stays open
  and idle instead of closing; the queue can still hold one peer under two keys
  (SID vs CID, or a hub URL written differently) - not this bug, but a hazard.
- 2026-11-25: F3c done: the public hub directory in the core. Api:
  `AddHubListSource` / `RemoveHubListSource` / `ResetHubListSources` and
  `RefreshHubDirectory{force}`, answered by `HubListSources{request, sources}` -
  each `HubListSource{url, fetched, hubs, cached, fetching, error}` - and, where
  the merge changed, `HubDirectory{request, sources, hubs}` with each
  `DirectoryHub{hub, listedBy}` naming its lists by index into the event's own
  `sources`; both are in `ReportState` and `Core::state()`. Status changes go in
  the small event, so a fetch starting does not resend thousands of hubs.
  `core/network/hubdirectory.*` holds what the GUI's HubDirectory did: the
  sources in [Hublists] (defaults written the first time, a Total of nought
  respected, the withdrawn list dropped and the file rewritten on every load -
  the core reads the file, so that moved too), a fetch per source on the core's
  loop apart from FetchHubList's, the merge (precedence by order, blanks filled
  from later lists, the larger user count, the keyprinted address, one row per
  hub however spelled), and the cache: one file per source named by the SHA-1 of
  its address under `<home>/hublists`, the JSON the GUI wrote, so existing caches
  and settings are read as they are and no migration is needed (a reader for
  JSON rather than for that writer, since the files are on disks already).
  Nothing is fetched until a front end asks: `RefreshHubDirectory` fetches what
  has no list, an empty one or one older than 12 hours, and not a source asked
  in the last 15 minutes; `force` fetches all; an added source is fetched unless
  its cache is fresh; a removed one loses its cache and any fetch on its way. The
  GUI's HubDirectory is a bridge over the two events (seeded from the channel's
  latest, `changed()` only when the merge changed, `sourcesChanged()` for the
  status line); HubListBridge, the merge, the cache, the source list and the
  JSON are gone from the GUI, `Testing::announce` stands in for the core in view
  tests, and the merge and cache tests moved to `hubdirectory.tcc` (12 cases).
  qdc: `hublist-add/remove/reset/refresh [force]`, `serve <port> <dir>` (a small
  HTTP server in the harness on loopback, bound before the line returns, so the
  scenario needs neither the internet nor python), `expect-served`, and
  `restart` (the core built again over the same home, what it said forgotten).
  Scenario hub-directory: two served lists merged with both credited, a list on
  a closed port reported with the reason while the others list, the withdrawn
  list left out, a restart showing the directory from the cache with nothing
  fetched and a fresh cache not fetched again, force, remove and add, reset; 78
  runs (30 in a row, 2x24 in parallel) without a failure. The smoke driver's
  public run now goes through the window's directory: 3 lists, 360 hubs merged
  and shown, one cache file each, a public hub joined.
  Found: `Core::homeDirectory` was "~/.quickdc" unexpanded for the application,
  and F3b's pasted pictures went through std::filesystem with it, so a paste in
  the application would have been kept, and swept, in a directory literally
  named "~" under the working directory. The home is now resolved where it is
  set. 1430 green; sanitizer build 1383 green.
- 2026-11-25: F3d done, and with it F3: bulletin board policy in the core.
  `core/bbs/boardkeeper.*` holds what the GUI's BoardBridge and HubsView decided:
  it follows every board a hub describes from the highest timestamp held, unless
  somebody left it on that connection (forgotten when the session goes); a board
  described with RM1 or refused with 71 is gone; entries are kept in the index and
  told in batches (256, or 50 ms) since a replay is a whole board; a body is
  fetched only on a reader asking, the rest of its thread with it where "Fetch a
  thread ahead" holds for the hub (its session group, else [Boards]) - the thread
  found by parents, falling back to the hub's thread where a parent is not held;
  a held document is read off the disk without asking anybody; fetches are capped
  by the declared size and 4 MiB, two at a time, 64 MiB a board an hour, searched
  for on the hub they were read on, the author named after 20 s with nobody found,
  given up after 5 minutes with nothing arriving or past the cap; a document is
  verified (hash, header, author against the index's) and offered where "Offer what
  I have read" holds (the hub's group, else [Boards] for favourites only), withheld
  when that turns off; tombstones, the keep days and the cache size mark documents,
  and a marked one goes once no upload reads it; a post is written by the core -
  signed with its CID, subject on one line, the board's MS checked, kept under
  `<home>/outbox`, offered for the session without a slot, submitted once hashed
  and taken into the store when its entry comes back. The core owns one store per
  home, opened when first needed; front ends hold no handles. The settings joined
  the core's schema; the stores (`<home>/boards`, `posts`, `incoming`, `outbox`)
  and keys are the ones the GUI used, so nothing migrates. Board fetches are left
  out of TransfersSnapshot and are not completed downloads (F3a had recorded every
  verified post as one).
  Api removed: `BoardStoreHandle`, `BoardReadState`, `OpenBoardStore`,
  `CloseBoardStore`, `StoreBoardEntries`, `DescribeBoard`, `QueryBoardState`,
  `ResumeBoard`, `ServePosts`, `QueryPost`, `AdoptPost`, `EvictPosts`,
  `ReleasePosts`, `SubmitPost`, `EnqueueForReading`, `AddFallbackSource`, and the
  events `BoardDescribed`, `BoardEntryArrived`, `BoardEntriesStored`,
  `BoardStateReported`, `BoardLoaded`, `BoardResumed`, `BoardReadChanged`,
  `PostKnown`, `PostAdopted`, `PostsEvicted`. Added: `LoadBoard{hubUrl, board}`,
  `ReadPost{hubUrl, board, tth, fetch}`, `MarkRead{hubUrl, board, tth}` (empty for
  the whole board), `SetBoardPolicy{hubUrl, FetchAhead|Serve, Follow|Always|Never}`,
  `WritePost{request, hub, board, parent, subject, body, richText}`;
  `SubscribeBoard{hub, board, fromStart}` and `UnsubscribeBoard` are now "take it up
  again" and "leave"; events `BoardEntries{hubUrl, board, entries, whole, unread,
  read}`, `BoardState{hubUrl, board, described, descriptor, cursor, view,
  subscribed, left, gone}`, `BoardRead`, `PostStatus{tth, state, why, size, post,
  attributedTo}`, `PostSubmitted`, `PostRefused{request, why, size, limit,
  reason}`. `BoardsAvailable`, `BoardRefused`, `RequestBoardEntry`, `WithdrawPost`
  stay. The GUI's BoardBridge is a bridge over these (read state and post states
  kept for painting, the core's reasons put into words); HubsView lost the policy,
  the per-hub caches, the left/subscribed sets, the 71 rule, the composing and the
  composed-post copies; TransferBridge lost hidePath, enqueueForReading and
  addFallbackSource; MediaBridge lost the post offer. Which board rows were open
  on a hub stays a window matter, kept in the session store as before.
  qdc: `board-load`, `board-subscribe [from-start]`, `board-leave`, `board-read
  [fetch]`, `board-mark-read`, `board-post` (a thread, or `reply <tth>`),
  `board-withdraw`, `board-policy`. Scenario boards: three cores on c's hub - a
  posts, b sees the entry and fetches nothing until it reads, fetches from a,
  reads one reply without fetching ahead and then with it fetches the rest of the
  thread, a withdraws a reply and b and c see it withdrawn, c reads a's post from
  b after a has left, b reads its board back after a restart with its read state,
  lets every document go at a one byte cache, and a board the hub has not got is
  gone; 78 runs (30 in a row, 2x24 in parallel) without a failure. 17 keeper cases
  (timeouts, the author grace, eviction by age and size, concurrency, the cap,
  verification, attribution, serving, a written post's bytes) with the clock the
  test's. The smoke driver gained `boards`: the two boards of
  adcs://adcs.uhub.org:1511 shown, read-only.
  Found: the local hub announced its configured boards from readConfiguration()
  in its constructor before `processing` was given a value, so a hub with boards
  made over used memory could read the flag as set and stay "inside a broadcast"
  for good, deferring every quit and posted message (UBSan, first run of the
  scenario under the sanitizer build); initialised at its declaration, with a
  test that builds the hub over 0xff. A board fetched from the start also kept
  showing a gap until its next descriptor; the core now says when a replay closes
  one. Left as found: the Api still names a hub's boards both by session handle
  (subscribing, writing, withdrawing) and by address (reading, which works with no
  session); retry of a post given up on is the reader asking again. 1450 green;
  sanitizer build 1403 green; the five smoke scenarios pass.
- 2026-11-25: F4 done: the old `quickdc_cli` (core/test/cli) is gone, and with it
  the last front end that held core objects; `testdebug`, which never used its
  console, builds on its own. Its commands, against the Api: `connect`,
  `disconnect`, `msg`, `password`, `search`, `download`, `browse`, `queue`,
  `transfers`, `quit` were covered; `list`, `stats` and `info` are what UserUpdated
  already says, now printed by qdc's `users` and `info`; `allow_connections` is
  SetHubPolicy, now qdc's `hub-policy`; `help` is generated from qdc's command
  table. Added: `kick` and `redirect`, and a ban; `hubkick`, `hubban`, `hubunban`
  (the last two were never implemented) and `hubstats`; `connections`;
  `tthsearch`; `version` and `uptime`. Dropped: `ctm` (a connection is opened for
  a download or a list, and one for nothing did nothing), `close` (never
  implemented; a download is dequeued), `joins` (a display filter, and qdc shows
  every event), plain text as main chat and the host:port shorthand (qdc names a
  hub and a URL). Api: `KickUser{hub, userId, message, redirect, banSeconds}` -
  ADC's DSC with MS, RD and TL, NMDC's $Kick and $OpForceMove - refused with
  `KickRefused{hub, userId, NoSession|NoSuchUser|NotOperator|Unsupported}` (a ban
  on NMDC); `ReportStatus` and `CoreStatus{request, version, started, port, tcp,
  tcp6, udp, udp6, localHub}`, also in ReportState and `Core::state()` and told by
  the core whenever what it listens on, or whether its hub takes logins, changes
  (checked once a turn); `SnapshotConnections` and `ConnectionsSnapshot{request,
  connections}` of `PeerConnection{protocol, address, port, peer, secure,
  outbound}`; `FindShared{request, tth}` and `SharedFile{request, tth, found,
  path, name, size, attached}`; and the local hub's administration,
  `ReportLocalHub`, `KickFromLocalHub{who, message}`, `BanFromLocalHub{who,
  reason, seconds}`, `UnbanFromLocalHub{who}`, `SetLocalHubAccount{nick, password,
  User|Operator|Admin}`, `RemoveLocalHubAccount`, each answered by
  `LocalHubState{request, enabled, name, maxUsers, started, users, accounts,
  bans}` or `LocalHubRefused{request, NoHub|NoSuchUser|NoSuchEntry|Invalid|
  NotSaved}`. The GUI says why a kick was refused and has no UI for the rest.
  The local hub's access list was read and never consulted: it now keeps bans
  (by CID, nickname or address, until a time or for good, refused at login with
  231 or 232 and TL) and password accounts (asked with GPA, a fresh salt each
  time, answered by the tiger digest the client already sent; the rights given
  with CT; an entry without a password grants nothing, since a nickname is
  anybody's and a CID is told to every hub). An operator's DSC honours MS, RD and
  TL: the user is sent its own QUI with them - it was told only "Kicked", and a
  redirect sent no address - and a TL is kept as a ban against its CID. The ADC
  client reads a QUI naming itself as kicked, redirected (HubRedirect with the
  address) or banned, and does not reconnect by itself; its DSC carried neither
  the message nor the address before. qdc: `status`, `connections`,
  `find-shared`, `kick`, `redirect`, `ban`, `hub-policy`, `hub-status`,
  `hub-kick`, `hub-ban`, `hub-unban`, `hub-account`, `hub-account-remove`,
  `users`, `info`, `help`; and an interactive mode - a terminal and no script,
  or `-i` - reading the same language a line at a time, running each line as it
  comes, the cores starting with the first line that is not taken before them,
  a failed line reported and the next one read, events printed as `[core] Event
  path=value ...` (`--json` for the JSON lines). Scenario operator: three cores,
  b given an operator account on a's hub and logging in with its password, then
  kicking, redirecting and banning c through the hub (by CID, so c's next
  nickname does not get past it), c refused as no operator, a lifting the ban,
  kicking c as the owner, banning a nickname for good and lifting that, a's
  connections, find-shared and the account removed; two-cores-chat now turns the
  hub on live and waits for CoreStatus instead of for a's own login, and both
  logins run at once. cli_interactive drives `qdc -i` over a pipe: help, a typo
  reported without ending the run, an account seen as an event and in acl.xml,
  and --json. operator, interactive and two-cores-chat 78 runs each (30 in a row,
  2x24 in parallel) without a failure, operator 12 more under the sanitizers.
  Found: the hub's exotic cases built their hubs over ~/.quickdc/hub, and the
  access list wrote itself out there on every destruction, so the suite rewrote
  the developer's own acl.xml on every run (it had today's date); a table is now
  written only once changed, and the cases use scratch directories. An ACL entry
  read as expired when its time was still to come; the old table's identity
  matchers were stubs that matched nothing. A QUI's MS and a DSC's fields were
  looked for from the first argument, which is the SID, so a SID starting "MS"
  was read as a message. Left as found: HubRedirect's `followed` is what the
  policy said, and nothing in the core follows a redirect; port 0 binds TCP and
  UDP to different ports, and what is announced to hubs is the configured port,
  so CoreStatus tells the bound port but a front end cannot yet ask for one
  picked for it; passwords in acl.xml are kept as written. 1466 green; sanitizer
  build 1419 green.
- 2026-11-25: F5 done: the `drive-qdc` project skill (.claude/skills/drive-qdc,
  kept out of git like run-quickdc) - building qdc, running and writing scenarios
  that hold (preset what login depends on, wait on events, verdicts in the
  script), stressing them with their exact ctest arguments, and what loopback
  cannot test. Phase F is done.
- 2026-11-25: checking the drive-qdc skill's example against the traced app
  build found the trace logs still process-wide in the wrong way: opened and
  freed once per core, so the first core to go freed them under the others
  (qdc aborted), written without a lock by several core threads, and always
  under ~/.quickdc/debug and ~/.samurai/debug - a traced qdc truncated the logs
  of the QuickDC the user was running. Now opened by the first core and closed
  by the last, every write locked, and under the core's own home when it has
  one; the app's default home keeps the old locations. A traced qdc opens
  nothing under HOME (checked with strace).
- 2026-11-25: the leftovers batch. Queue: a source keeps its hub as
  `hubSettingsGroup()` spells it and matches by CID alone on ADC (by hub, any
  case, and nick on NMDC), so one peer is one source however it was learned;
  a SID given for a source is turned into the user's CID (`Hub::sourceIdOf`),
  and on ADC a source is never found by SID; the scheduler opens no second
  request to a peer already asked for that file; leaves are fetched into a file
  per connection and the first whole one adopted, so two connections can no
  longer delete each other's (the endless leaf ping-pong). A peer connection
  quoting a token nothing here waits for is answered `CSTA 240 Connection not
  expected TO..` and closed, and every peer connection has 30 s from coming up
  to both sides naming themselves. Redirects are followed where SetHubPolicy
  says so (`Hub::onRedirect`/`judgeRedirect`): same protocol family, never adcs
  to adc, never back to where the connection has been, at most
  QUICKDC_HUB_MAX_REDIRECTS; same handle, the hub's address changes, boards and
  recents follow; `HubRedirect` gains `url` and `why`. Board commands
  (SubscribeBoard, UnsubscribeBoard, RequestBoardEntry, WritePost,
  WithdrawPost) name the hub by address like the rest; the keeper finds the
  session and answers `BoardUnavailable{hubUrl, board, tth, asked}` (or
  PostRefused NoHub) where none carries boards; HubBridge lost the five board
  methods, BoardBridge has them by address; qdc `board-entry` added. TLS: the
  samurai BIO now answers BIO_CTRL_EOF, so a peer closing after a transfer is
  an end of stream rather than "SSL read error (5): no error reported" and
  "Protocol error"; a home with no hash cache yet logs no error; scenario.sh
  takes `!text` for what stderr must not hold, and cli_download_tls checks
  `!ERROR`. ADC search results carry SL (free upload slots, as the upload side
  counts them); NMDC parsed slots already, and still sends no search results at
  all. NMDC's $GetListLen and $Get of a list own the list they open. Port 0
  binds TCP and UDP to one picked port (retried where UDP has it taken), and
  every advertised port is `Core::peerPort()`, the bound one; two-cores-chat and
  download-active use port 0 and CoreStatus (qdc `open` expands `$NAME`).
  acl.xml must keep passwords as given - GPA/PAS proves a password with a
  fresh salt, so no stored digest could answer - so it is written 0600 (samurai
  `File::Mode::Private`, tightening an existing file) and the hub directory set
  0700. A dequeued picture leaves `mediaFetches`; SearchView forgets its
  searches when it goes. hubsession.tcc removes its peer key pair. 1476 green;
  sanitizer build 1429 green; redirect, boards, download-tls, search,
  two-cores-chat, download-active 78 runs each without a failure.

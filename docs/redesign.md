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

- [ ] B1. The core owns its loop and a thread-safe inbox: the job queue and
      notifier move out of `Qdc::CoreThread` into the core (`Core::post`,
      `Core::runUntilStopped`, `Core::stop`). `CoreThread` becomes a host.
- [ ] B2. Value types for what crosses the boundary (`QuickDC::Api`): user,
      hub state, chat line, queue item, transfer, search result, board entry,
      reachability verdict, setting value. No pointers into the core.
- [ ] B3. `Api::Event` (a variant of those) and an `EventSink` the host supplies
      - the core calls it on its own thread with a value; the host marshals.
- [ ] B4. `Api::Command` set and `Core::submit()`, replacing the GUI's lambdas
      that reach into `core->queue`, `core->hubs` and the rest.
- [ ] B5. Migrate bridges one at a time, smallest first: hublist, drop,
      media, config, filelist, search, transfer, hub, board. Each one: stop
      touching core objects, stop implementing core listener interfaces, consume
      events. GUI suite green after each.
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

- libdrop-cpp `droptest` bounds assertion (baseline).
- samurai `autotest` load sensitivity under `-j32` (baseline).

## Log

- 2026-11-23: clone created, baseline built and run, plan written.
- 2026-11-23: A1+A2 landed (quickdc `39a5c4e`). Full suite green.
- 2026-11-23: A3+A4 landed. No `Core::getInstance`, `quickdc_core`,
  `CID::getInstance` or `global_cid` left anywhere. Full suite green (1247);
  ASan core failures identical to baseline, no new sanitizer errors.

# Distribution, CI/CD, updates and crash reporting

The plan agreed on 2026-11-26. Nothing below is built yet except where it says so.

## Decisions

| Subject | Decision |
|---|---|
| Platforms | Linux, macOS and Windows, started together; the first release waits for all three. |
| Oldest supported | Linux: Debian stable, now Debian 13 (glibc 2.41, GLIBCXX 3.4.33); the AppImage is built on Ubuntu 24.04 with its own compiler and uses the system's libstdc++. macOS 13.3, universal (arm64 and x86_64). Windows 10 1809, Qt 6.5's own floor. |
| Linux package | AppImage. The Flatpak (`packaging/flatpak`) and the Snap plan (`packaging/snap.md`) stay as they are, outside CI. |
| macOS package | `QuickDC.app` in a DMG (`packaging/macos/build.sh`). |
| Windows package | An Inno Setup installer around a `windeployqt` tree (Qt, OpenSSL, the MSVC runtime). |
| Signing | None for now. Every signing step is written but runs only when its secrets exist: Developer ID + notarisation on macOS, Azure Trusted Signing or an OV certificate on Windows. |
| CI host | GitHub Actions on a public push-mirror of all five repositories. Gitea stays the source of truth. |
| Cadence | A tag `vX.Y.Z` builds a release; master builds a nightly once a day when it changed. |
| Downloads | `download.quickdc.org`, pushed by CI over SSH to a deploy user confined to one directory (`rrsync`). |
| Updates | An updater of our own in the core, fully silent, the channel fixed by the package, a signed feed, an off switch in Settings. |
| Crash reports | Crashpad on all three platforms, uploads to a self-hosted Sentry-compatible server (GlitchTip) at `crash.quickdc.org`, debug symbols uploaded by CI. |
| Diagnostics | Crashes, non-fatal errors (assertions, failed checks, failed updates) and an anonymous daily usage ping. All of it only after a single opt-in at first run. |

## CI on the mirror

- **Mirror:** Gitea push-mirrors `quickdc-qui`, `quickdc`, `samurai`, `libdrop-cpp` and `exotic` to public GitHub repositories. `.gitmodules` moves to relative URLs (`../quickdc.git`), so one file resolves on both hosts.
- **Workflows:** the submodules keep their `.github/workflows/ci.yml`, since both Gitea and GitHub read that path. The superproject gets its own, with these jobs:
  - `build` on ubuntu, macos and windows: configure, build, and run ctest without the `net` label. GUI tests run offscreen.
  - `sanitizers` on ubuntu (ASan/UBSan/LSan), as in the submodules.
  - `net` on ubuntu: the rootless-podman suite (`ctest -L net`). It runs nightly rather than per push, because it takes minutes and needs podman.
  - `package` on all three for nightlies and tags: AppImage, DMG and installer, plus debug symbols.
  - `publish`: signs the update feed entries, rsyncs to `download.quickdc.org` and uploads symbols to the crash server.
- **Toolchains:** on Linux, the AppImage is built on Ubuntu 24.04, older than Debian stable in both glibc and libstdc++, with `linuxdeploy` and its Qt plugin. CI checks that nothing in it needs more than Debian stable has. On macOS, Qt comes from `aqtinstall` and OpenSSL 3 from Homebrew, as `build.sh` already expects. On Windows, MSVC with `aqtinstall` Qt and OpenSSL from vcpkg.
- **Secrets on GitHub:** the deploy SSH key and the crash server's symbol-upload token. The update-signing key never goes to GitHub (see below). Signing certificates come later.

## Versions and channels

- **Versions:** a release is its tag. A nightly is `X.Y.Z~nightlyYYYYMMDD.<sha>`, the scheme the Flatpak bundles already use.
- **Channels:** each package carries its channel as a build-time constant (`stable` or `nightly`). A hidden setting overrides it for testers. Nobody on stable gets a nightly.

## The updater

- **Where it lives:** in the core, as an Api feature (`CheckForUpdate`, events `UpdateAvailable`, `UpdateReady`, `UpdateFailed`), so qdc can drive and test it the way it does everything else. The GUI only shows that an update was applied.
- **Feed:** `https://download.quickdc.org/<channel>/<platform>.json` holds the version, the package URL, its size and SHA-256, and an Ed25519 signature over all of that.
  - The public key is compiled into the app.
  - The private key stays on a machine of yours, never on the web server or on GitHub. CI produces unsigned feed entries, and a signing step run by you, or a dedicated offline signer, publishes them.
  - With no code signing, this signature is the only thing between a compromised server and every install, so it is not optional.
- **Behaviour (silent):**
  - It checks a few minutes after start and then daily, with jitter. It downloads in the background and verifies the size, hash and signature.
  - It installs on quit: the AppImage file is replaced in place, the `.app` bundle is swapped, and on Windows the installer runs with `/VERYSILENT`.
  - It never interrupts a running session. A failure becomes an `UpdateFailed` event (and a diagnostic, if opted in) and is retried on the next check.
- **Settings:** `[Updates] Enabled` (default on), shown on the Settings page; `[Updates] Channel` (hidden override).
- **Not chosen:** rollback and staged rollout. Silent installs with no rollback mean a broken release reaches every install, so the nightly channel and the release checklist have to carry that weight.

### The feed, as built (phase 5)

- **Where:** `<feed>/<channel>/<platform>.json`. `<feed>` is `[Updates] Feed`, by default `https://download.quickdc.org`; `<platform>` is `linux-x86_64`, `macos-universal` or `windows-x86_64`.
- **What:** one JSON object.

  ```json
  {
    "format": 1,
    "channel": "nightly",
    "platform": "linux-x86_64",
    "version": "0.1.1~nightly20261128.1a2b3c4",
    "url": "0.1.1~nightly20261128.1a2b3c4/QuickDC-0.1.1~nightly20261128.1a2b3c4-x86_64.AppImage",
    "size": 63350664,
    "sha256": "<64 lowercase hex digits>",
    "signature": "<128 lowercase hex digits>"
  }
  ```

- **What is signed:** the Ed25519 signature covers these lines, each ending in a line feed, not the JSON, so no serialiser has to agree with another about spacing or order:

  ```
  quickdc-update 1
  channel <channel>
  platform <platform>
  version <version>
  url <url>
  size <size in decimal>
  sha256 <sha256>
  ```

  The channel and platform are part of it, so a feed for one cannot be served as the feed for another.
- **The package's address:** relative to the feed's directory (`<feed>/<channel>/`) as above, or a full https URL. A relative one may not climb out of that directory. The address is signed as it is written.
- **Signing:** `quickdc/tools/update-feed` writes a signed feed from the private seed (64 hex digits in a file) and the package.
- **What is refused:**
  - a signature that does not verify against the channel's key;
  - a channel or platform other than the one asked for;
  - a version that is not newer than the running one, so there are no downgrades;
  - a package that is not https, or a relative one that climbs out of the feed's directory;
  - a download whose size or SHA-256 differs from the feed's.
- **Versions:** `X.Y.Z` releases and `X.Y.Z~nightlyYYYYMMDD.<sha>` nightlies, ordered as Debian orders them: a nightly comes before the release of the same number. So after a release is tagged, `project(VERSION)` moves to the next number at once, or the nightlies that follow would be older than it. A build whose version has a `+` (a local build) has no channel and never updates.
- **Channels:** set by the package as a build-time constant (`QUICKDC_QUI_CHANNEL`): `stable` for a tag, `nightly` for a nightly, empty for anything else. `[Updates] Channel` overrides it for testers.
- **Keys:** each channel's public key is compiled into the core (`core/update/keys.h`). A channel with no key yet never updates and says so. The `test` channel's key belongs to the test suite, whose private half is committed, and is accepted only from a feed on the loopback, which only something already running on the machine can serve.
- **The package an update fetches:** the AppImage on Linux, the installer on Windows, and on macOS a zip of `QuickDC.app` (the DMG is for people, not for swapping a bundle).

## Crash reporting and diagnostics

- **Client:** sentry-native (MIT) with its Crashpad backend, fetched pinned by URL and SHA-256 when `QUICKDC_QUI_CRASH_REPORTS` is on, which only the package builds turn on. It is a CMake project, where Crashpad alone needs Chromium's gn. The handler (`crashpad_handler`) runs as a separate process beside the program from the first second of start, so a corrupted process can still be dumped. The client is `src/app/diagnostics.cpp`; the core knows nothing of it.
- **Where reports go:** `QUICKDC_QUI_CRASH_DSN`, from the `QUICKDC_CRASH_DSN` secret in CI. Without it, dumps stay in the local crash database and are never sent.
- **What a report carries:**
  - the minidump;
  - the version, channel, OS and a random install ID, created at opt-in and resettable;
  - for non-fatal errors, a Sentry event with the message and a stack.
- **On the server:** the project is set not to store the IP address a report arrives from (GlitchTip and Sentry both have the switch); the client sends none.
- **What a report does not carry:** no logs, chat, nicknames or hub addresses beyond what happens to be in the minidump's memory. The opt-in text says that plainly.
- **Usage ping:** the sessions sentry-native records, one per run, carrying only the release, channel and install ID. It goes only from opted-in installs.
- **Consent:** one question at first run, which defaults to no. Turning it off later in Settings stops all reporting and deletes the pending dumps. A privacy notice on quickdc.org says what is sent, where it goes and how long it is kept; the GDPR applies.
- **Server:** GlitchTip at `crash.quickdc.org`, which takes Sentry's minidump and event endpoints. CI uploads `.sym` and debug files for every published build, so stacks come back symbolicated.

## Phases

1. **Mirror and CI:** relative submodule URLs, the GitHub mirrors, and the superproject workflow building and testing on Linux and macOS.
2. **Windows port:**
   - get samurai (sockets, event loop, files, OpenSSL), libdrop-cpp, the core and the GUI building with MSVC;
   - get the test suite green on the windows runner;
   - the exotic tests and qdc come along, while the podman net tests stay Linux-only.

   This is the largest and least known piece of the plan.
3. **Packages:** AppImage, DMG and Inno Setup installer from CI, with debug symbols and nightly and tag triggers.
4. **Publishing:**
   - on the server: the deploy user, `rrsync`, and the directory layout on `download.quickdc.org`;
   - from CI: the rsync step and the feed generation;
   - the offline feed-signing step.
5. **Updater:** the Api, the per-platform install-on-quit, the Settings switch, and qdc scenarios against a local feed: verified, a bad signature refused, a hash mismatch refused, switched off.
6. **Crash reporting:** GlitchTip on `crash.quickdc.org`, Crashpad in all three builds, the opt-in, the symbol upload, and a test crash per platform from a hidden debug action.
7. **First release:** the version, release notes, privacy notice and download page.

## Open questions

- Where the offline update-signing key lives, and who runs the signing step for nightlies. A nightly that waits for a person is not nightly.

## Progress

- 2026-11-26, phase 1: public GitHub mirrors of all five repositories (history
  rewritten first so every commit carries janvidar@extatic.org), relative submodule
  URLs, and `.github/workflows/ci.yml` in the superproject: Ubuntu 24.04 and macOS 15
  build and test all ~1500 tests green, the sanitizer job is green, Windows builds
  and fails as expected, the podman net job runs nightly. Getting there fixed real
  bugs: chat layouts leaked when a feed closed; kqueue reported a refused connect
  as a plain disconnect, so NAT traversal gave up on macOS instead of dialling
  again; a proxy hanging up mid-handshake read as a reset; the settings page
  dropped a quick switch back to the previous mode; qdc's free ports were neither
  claimed nor kept out of the ephemeral range on macOS. Left for the user: Gitea
  push mirrors (each needs a GitHub token), and whether to publish `drop` so the
  CI net job gets dropd. Known flake: samurai's socks5 hang-up test, seen once on
  macOS under the superproject's parallel ctest.
- 2026-11-27, phase 2: Windows builds with MSVC and runs the whole suite (1516 tests,
  qdc and its scenarios included, through Git's sh) green as a required job; samurai
  has a Windows job of its own (7 minutes, against 30 for the superproject). Left
  out there: samurai's poll() monitor tests and descriptors past FD_SETSIZE, the
  core's testdebug program, and the podman net tests (Linux only). Bugs the port
  found beyond Windows itself: a refused connect read as connected, IPv6 local
  ports read into a buffer too short for them, a handshake deadline test that its
  own pauses nearly filled. On Windows: TLS reads its files and the system's root
  store without OpenSSL's stdio, sockets shared for traversal, `%s` in timestamps,
  interfaces from GetAdaptersAddresses, CRLF in settings files.
- 2026-11-27, phase 3: `.github/workflows/package.yml` builds the AppImage (Ubuntu
  24.04 and its own GCC 13, for Debian stable and newer; Wayland and X11), a universal DMG
  (OpenSSL from vcpkg for both architectures at macOS 13.3, merged with lipo) and a
  per-user Inno Setup installer (`packaging/windows`), each with its debug symbols,
  and starts each once before keeping it. `packaging/version.sh` is the one version
  rule. A tag builds a release, the night a nightly when master moved, a manual run
  a nightly. Found on the way: a shipped build verified TLS against the build
  machine's certificate paths, so samurai now loads the keychain's roots on macOS
  and the distribution's bundle on Linux; Windows listed adapters without an
  address. Not yet exercised: a release tag. Not signed: macOS (Developer ID and
  notarisation) and Windows (Trusted Signing or an OV certificate) wait for secrets.
- 2026-11-27, phase 5: the updater, in the core (`core/update`): the feed format
  above, Ed25519 in samurai (RFC 8032's vectors), versions ordered as Debian orders
  them, the package fetched only when newer and kept only when it is the size and
  SHA-256 that was signed for, and installed as the program quits - the AppImage
  renamed over itself, the bundle swapped, the installer run silently. Settings has
  an Updates page with the one switch. qdc scenarios cover a good update, a forged
  signature, a package that is not what was signed, a feed for another channel, a
  package outside the feed's directory, a missing feed, one off the loopback for the
  test key, and updates turned off. Each package names its channel from its version
  and leaves its unsigned feed entry beside it. Left: the real stable and nightly
  keys (`core/update/keys.cpp`), and the signing and publishing of phase 4. The
  installs on macOS and Windows are written but have only been compiled; they want
  a run on each from a published feed. The download is held in memory (up to 512
  MB) rather than streamed to disk.

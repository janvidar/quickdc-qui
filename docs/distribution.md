# Distribution, CI/CD, updates and crash reporting

The plan agreed on 2026-11-26. Nothing below is built yet except where it says so.

## Decisions

| Subject | Decision |
|---|---|
| Platforms | Linux, macOS and Windows, started together; the first release waits for all three. |
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
- **Toolchains:** on Linux, the AppImage is built on the oldest glibc we support (an Ubuntu 22.04 runner or container) with `linuxdeploy` and its Qt plugin. On macOS, Qt comes from `aqtinstall` and OpenSSL 3 from Homebrew, as `build.sh` already expects. On Windows, MSVC with `aqtinstall` Qt and OpenSSL from vcpkg.
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

## Crash reporting and diagnostics

- **Client:** Crashpad, built from source in CI (it uses gn/ninja, or a CMake port) and linked into the app. The handler runs as a separate process from the first second of start, so a corrupted process can still be dumped.
- **What a report carries:**
  - the minidump;
  - the version, channel, OS and a random install ID, created at opt-in and resettable;
  - for non-fatal errors, a Sentry event with the message and a stack.
- **What a report does not carry:** no logs, chat, nicknames or hub addresses beyond what happens to be in the minidump's memory. The opt-in text says that plainly.
- **Usage ping:** daily, carrying only the version, channel, OS and install ID. It goes only to opted-in installs.
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

- The minimum versions to support: glibc for the AppImage, macOS (Qt 6.5 needs 11 or newer), and Windows 10 or 11.
- Whether the macOS DMG is universal (arm64 and x86_64) or arm64 only.
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


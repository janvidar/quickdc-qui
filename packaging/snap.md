# Snap packaging — a plan, not a package

Nothing here is wired up. There is no `snap/snapcraft.yaml` in the tree and
`snapcraft` will not build anything; this is the design written down so it can
be picked up later without working it out again.

The Flatpak in [flatpak/README.md](flatpak/README.md) is the Linux package
today. Read the next section before doing any of this, because the case for a
second Linux package format is narrow and worth checking against.

---

## Why bother, and when not to

There is one real argument, and it is about distribution rather than packaging.

The gap in the Flatpak setup is that a nightly is a file somebody downloads. A
`.flatpak` bundle carries no update path, so every night is a fresh manual
download, and closing that gap means hosting an ostree repository with a GPG
key and somewhere to serve it from.

Snap's `edge` channel closes it with no infrastructure at all. A tester runs
`snap install --edge quickdc` once, and every nightly after that arrives on its
own, because the Snap Store is doing the hosting.

A second thing falls out for free, which the Flatpak fought hard for and lost:
Snap always points `$HOME` at `~/snap/quickdc/current/`, so the hardcoded
`~/.quickdc` becomes private state automatically. No `--persist`, and none of
the shadowing problem that made `--persist` and `--filesystem=home` mutually
exclusive.

**Not worth it if** the answer to updates turns out to be a hosted ostree
repository anyway — that serves the same purpose, keeps one package format, and
needs no store account. And a `.snap` file handed to somebody directly is
strictly worse than the Flatpak bundle that already exists: same manual
download, more moving parts. **Snap is only worth building if it is published
to the Snap Store.** If that is not going to happen, stop here.

---

## What it would look like

`snap/snapcraft.yaml`, at the repository root:

```yaml
name: quickdc
adopt-info: quickdc-qui
base: core24
confinement: strict

# devel, because these are nightlies. A devel snap can be released to edge and
# beta and nowhere else, which is the right constraint - it makes it impossible
# to accidentally push a nightly to the channel people track by default.
grade: devel

apps:
  quickdc:
    command: usr/bin/quickdc-qui
    desktop: usr/share/applications/org.extatic.QuickDC.desktop

    # Qt 6 and the QPA plugins, from KDE's kf6-core24 content snap. This is the
    # direct counterpart of naming org.kde.Platform in the Flatpak manifest,
    # and it brings the desktop, wayland, x11 and opengl plugs with it, so
    # those are not listed below.
    extensions: [kde-neon-6]

    plugs:
      - network         # hub connections
      - network-bind    # the listener on 1411, without which it will not start
      - home            # shares and the download directory
      - removable-media # /mnt and /media; NOT auto-connected, see below

parts:
  quickdc-qui:
    plugin: cmake
    source: .

    # Takes the version, summary, description and icon from the metainfo that
    # already exists, so the text lives in one place rather than two.
    parse-info: [usr/share/metainfo/org.extatic.QuickDC.metainfo.xml]

    cmake-parameters:
      - -DCMAKE_INSTALL_PREFIX=/usr
      - -DCMAKE_BUILD_TYPE=RelWithDebInfo
      - -DQUICKDC_QUI_BUILD_TESTS=OFF
      - -DQUICKDC_TRACE=OFF
      - -DQUICKDC_ALLOW_LOOPBACK_AUTH=OFF
      # See "The one code change" below.
      - -DQUICKDC_QUI_DESKTOP_FILE_NAME=quickdc_quickdc

    build-packages: [libssl-dev, zlib1g-dev, libbz2-dev]
    stage-packages: [libssl3t64, zlib1g, libbz2-1.0]
```

That is the whole thing. It is short because most of the work is already done:
the CMake install rules, the desktop entry, the AppStream metainfo and the icon
were all written for the Flatpak and are reused unchanged.

`adopt-info` plus `parse-info` is the part worth noticing. It reads the
metainfo at build time and takes the snap's version, summary and description
straight out of it — including the `<release version="…">` element, which
`build.sh --nightly` already stamps. So the nightly version scheme carries over
with no second mechanism.

One limit to check: a snap version is at most 32 characters and may contain
only alphanumerics and `.+~-`. `0.1.0~nightly20260811.fe654c9` is 29, so it
fits, but a longer base version would not. If that ever overflows, shorten the
date rather than the commit.

---

## The one code change

snapd does not install a desktop entry under the name we gave it. It exports it
to `/var/lib/snapd/desktop/applications/` renamed to `<snap>_<app>.desktop` —
`quickdc_quickdc.desktop` here.

`src/app/main.cpp` calls `setDesktopFileName(QUICKDC_QUI_APP_ID)`, which under
a snap would name a file that does not exist. That is exactly the bug fixed for
the Flatpak, arriving from the other direction: a Wayland compositor cannot
match the window to any entry, so the window has no icon and no association
with whatever launched it.

Today one variable does two jobs. They need separating:

```cmake
# The app id: the metainfo component, the basenames of the entry and the icon
# as this build installs them, and the macOS bundle identifier.
set(QUICKDC_QUI_APP_ID "org.extatic.QuickDC" CACHE STRING ...)

# What the desktop calls that entry once it is installed, which is not always
# what we called it: snapd renames the file to <snap>_<app>.desktop on export,
# and this is the name a compositor matches a window against.
set(QUICKDC_QUI_DESKTOP_FILE_NAME "${QUICKDC_QUI_APP_ID}" CACHE STRING
    "Basename of the installed desktop entry, as the desktop sees it")
```

`main.cpp` then uses `QUICKDC_QUI_DESKTOP_FILE_NAME` for
`setDesktopFileName()`, and everything else keeps using the app id. The default
makes Flatpak and a plain `cmake --install` behave exactly as they do now; only
the snap build overrides it.

**Unverified:** the icon is a separate question. `setWindowIcon(QIcon::fromTheme(APP_ID))`
depends on how snapd exposes the hicolor icon to the theme search path, and
whether it rewrites the entry's `Icon=` line to an absolute path inside the
snap. Check both on a real build before assuming the X11 icon works; the
Wayland one follows the desktop file name and should be fixed by the above.

---

## Confinement

| Interface | Auto-connects | Why |
| --- | --- | --- |
| `network` | yes | Hub connections, hub lists, chat media |
| `network-bind` | yes | TCP and UDP 1411; the application exits if it cannot bind |
| `home` | yes | Shares and the download directory |
| `removable-media` | **no** | A collection on `/mnt` or `/media` |

`removable-media` has to be connected by hand, which is the counterpart of the
`flatpak override` line in the Flatpak docs:

```sh
snap connect quickdc:removable-media
```

The `home` interface covers **non-hidden** files in the real home only. That is
fine here — shares live in ordinary directories, and the application's own
state is not in the real home at all under snap — but it does mean a share
inside a dotdir would be invisible, which is not true of the Flatpak.

Note also that `home` gives the real home at its real path while `$HOME` points
elsewhere. The share picker starts at `QDir::homePath()`, which under snap is
`~/snap/quickdc/current/` — nearly empty. The Flatpak has the same wrinkle in
reverse and it is worth deciding once, for both, whether that picker should
start somewhere better.

### State, and the per-revision trap

`$SNAP_USER_DATA` is `~/snap/quickdc/<revision>/`, a **new directory each
revision**, with `current` symlinked to the live one. snapd copies the previous
revision's data forward on refresh, so nothing is lost, but two consequences
follow: a nightly that refreshes daily leaves a directory per revision until
snapd prunes old ones, and a hash cache of any size is copied each time.

If that becomes annoying, `$SNAP_USER_COMMON` (`~/snap/quickdc/common/`) is
shared across revisions and never copied, which is where a hash cache belongs.
Using it would mean the core taking a base directory from the environment
instead of hardcoding `~/.quickdc` — the same change the Flatpak notes list as
a follow-up, wanted here for a different reason.

---

## Building it locally

```sh
sudo snap install snapcraft --classic
snapcraft                       # builds in LXD, which is already installed here
sudo snap install --dangerous ./quickdc_*.snap
snap connect quickdc:removable-media
quickdc
```

`--dangerous` because a locally built snap is unsigned. Auto-connecting
interfaces still connect for a local install; `removable-media` still does not.

## Publishing

```sh
snapcraft login                 # a free Ubuntu One account
snapcraft register quickdc      # globally unique, and may already be taken
snapcraft upload --release=edge quickdc_*.snap
```

The name is the part to settle first, because it is not `org.extatic.QuickDC` —
snap names are a flat global namespace, lowercase, and `quickdc` may belong to
somebody else. `snap info quickdc` will say.

After that a nightly is one more line at the end of a build script, and a
tester's side of it is:

```sh
snap install --edge quickdc
```

and never again. Channels are `edge` for nightlies, `beta` when there is
something worth wider testing, and `stable` once `grade` can honestly be
`stable`.

---

## What it costs

- A second manifest to keep in step with the first. Small, but it is another
  thing that goes stale silently — the `stage-packages` list in particular
  duplicates knowledge the Flatpak gets from its runtime for free.
- `snapcraft` builds in a container, so it is slower than `flatpak-builder` and
  needs LXD working.
- A Snap Store account and a registered name.
- The desktop-file-name change above, which touches shared code and so needs
  the Flatpak re-checked after it.

## What was actually checked

Written on Linux against the store, not built. Confirmed: this machine is
Ubuntu 24.04 with snapd running and LXD installed, `kf6-core24` exists in the
store and is published by KDE, and `snapcraft` is not installed. Everything
else here — the manifest, the interface behaviour, the desktop-file rename — is
from documentation and has not been run.

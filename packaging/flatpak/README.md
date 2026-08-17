# Building, packaging and distributing the Flatpak

QuickDC ships on Linux as a Flatpak. This directory holds the two files that
make one: `org.extatic.QuickDC.yml`, the manifest, and `build.sh`, which drives
`flatpak-builder` over it. The desktop entry, the AppStream metainfo and the
icon that go into the package live in `../linux` and are installed by CMake, so
they are part of an ordinary `cmake --install` too and not something only the
Flatpak knows about.

The short version:

```sh
./packaging/flatpak/build.sh            # build and install for this user
flatpak run org.extatic.QuickDC
```

---

## What you need

Nothing but `flatpak` itself. The first run of `build.sh` fills in the rest:

- **flatpak-builder.** If there is not one on `PATH`, the script installs the
  `org.flatpak.Builder` Flatpak and uses that. It is the better of the two
  fallbacks because it carries `appstreamcli`, `desktop-file-validate` and
  `flatpak-builder-lint` as well, which is the whole verification set in one
  install.
- **The runtime and SDK**, `org.kde.Platform` and `org.kde.Sdk` at the branch
  the manifest names. Together these are a little over 1.5 GB the first time
  and nothing after that.
- **A user `flathub` remote.** Everything here installs `--user`, and a remote
  configured system-wide is not visible to a user installation, so the script
  adds one with `--if-not-exists`. A side effect worth knowing about: once both
  exist, a bare `flatpak remote-info flathub …` becomes ambiguous and wants
  `--user` or `--system` to say which you meant.

Nothing else is needed for the build itself. The submodules compile in tree as
static libraries, and OpenSSL, zlib and bzip2 come from the runtime, so the
manifest has one module in it and fetches nothing.

## Why the KDE runtime

`org.kde.Platform` is the only Flatpak runtime that carries Qt at all -
`org.freedesktop.Platform` has none and `org.gnome.Platform` is GTK. Despite
the name it is the freedesktop runtime with Qt 6 and KDE Frameworks on top, and
the frameworks this application does not use cost it nothing: they are in the
shared runtime rather than in the app, which is why the installed application
is about 3.4 MB. What it does use is Qt Core and Widgets, the QPA plugins and
the platform theme - without which a sandboxed Qt application's dialogs and
decorations match no desktop at all.

The alternatives are worse. Building Qt as a module of our own would add hours
to every build and some hundreds of megabytes to the app, and would make Qt
security updates our problem rather than the runtime's. There is no general Qt
BaseApp on Flathub; the one that exists, PyQt's, is itself built on this
runtime.

Which branch is a live decision rather than a settled one. Pick the newest that
is still being rebuilt, since a package built every night should not sit on a
base that stopped receiving fixes. To see where things stand:

```sh
flatpak remote-ls flathub --system --runtime | grep org.kde.Platform
flatpak remote-info flathub --system org.kde.Platform//6.11 | grep Date
```

Changing branch is two lines: `runtime-version` in the manifest, which
`build.sh` reads back so the two cannot disagree. The application needs Qt 6.5
or newer, so every current branch clears the floor.

---

## Building

`build.sh` is the only entry point, and it takes four options that combine
freely.

| Option | What it does |
| --- | --- |
| *(none)* | Build, then install for the current user |
| `--bundle` | Also write a single-file `.flatpak` into `dist/` |
| `--nightly` | Stamp a dated version on the build (see below) |
| `--pristine <ref>` | Build a clean checkout of a ref rather than the working tree |
| `--no-install` | Build only |

Two of those deserve explaining.

**`--pristine`** matters because the manifest's source is `type: dir`, which
copies the working tree exactly as it stands - uncommitted edits, half-finished
experiments and all. That is what you want when you are testing a change, and
the last thing you want in something handed to other people. `--pristine`
clones into `build/flatpak-stage` and checks out the ref you name.

It does not use `git clone --recursive`, and the reason is worth recording: every
submodule URL in `.gitmodules` is *relative* - `../exotic.git`, `../samurai.git`,
`../quickdc.git` - so they resolve against whatever the superproject's remote is,
which here is an `ssh://` one. The script points each submodule at the copy
already on this disk instead, which is both faster and means a nightly needs no
network access beyond the runtime. The three are siblings, so it is one pass
over them rather than one per level of nesting.

**`--nightly`** writes `packaging/nightly-version.txt` containing something like

```
0.1.0~nightly20260811.fe654c9
```

and the top-level `CMakeLists.txt` reads that file before it tries anything
else. It goes through a file rather than a command-line argument because
`flatpak-builder` has no way to pass a CMake variable into a build, so the
version has to arrive as part of the source tree. Without the file, the version
falls back to `git describe` and then to the bare project version.

The tilde is not decoration. It makes `0.1.0~nightly20260811.fe654c9` sort
*before* `0.1.0` under both Debian and AppStream version comparison, so the
eventual real release supersedes every nightly that led up to it. Without it,
`0.1.0-nightly…` would sort after and a tester would be pinned on a nightly
forever.

The version reaches the AppStream `<release>` element, so `flatpak info` and
any software centre report the build you actually have.

### Running it nightly

A crontab line is the whole mechanism:

```
17 3 * * *  cd ~/source/quickdc-qui && git fetch -q && \
            packaging/flatpak/build.sh --nightly --pristine origin/master \
            --bundle --no-install >> ~/.cache/quickdc-nightly.log 2>&1
```

`--no-install` because a cron job has no business replacing whatever you have
installed, and `--pristine` because of the working-tree point above.

If this ever moves to CI, the workflow belongs at `.github/workflows/` - which
is the path the core's own `ci.yml` uses precisely because Gitea Actions reads
it as well as GitHub does, so one file serves both remotes. It would check out
recursively, `apt-get install flatpak-builder`, cache `.flatpak-builder/ccache`
rather than the runtime, and call this same script.

---

## What the sandbox allows, and what it does not

The permissions are in `finish-args:` in the manifest, each with the reason it
is there. Three are worth understanding before you change any of them.

**`--share=network`** is not optional. The core binds TCP and UDP 1411 on every
interface at startup, and `main.cpp` refuses to open a window if that fails.
`--share=network` drops the network namespace rather than proxying it, which is
what puts the listener on the real interfaces where a peer can reach it. You
can see it from outside the sandbox:

```sh
ss -ltnp 'sport = :1411'
```

Note that a private namespace would *not* make the application fail to start -
`bind(0.0.0.0)` succeeds happily in an empty namespace. It would simply be
unreachable, which is a worse failure because it is silent.

**The filesystem grants are the standard media directories**, not the home
directory: `xdg-download`, `xdg-music`, `xdg-videos`, `xdg-pictures` and
`xdg-documents`. Anything else a person wants to share is one line, once:

```sh
flatpak override --user --filesystem=/mnt/media org.extatic.QuickDC
```

**`--persist=.quickdc`** is what keeps the package's state out of the real home
directory. Every path this application keeps is hardcoded under `~/.quickdc` -
configuration, session, download queue, hash cache, self-signed certificate -
and `--persist` redirects that one directory into
`~/.var/app/org.extatic.QuickDC/.quickdc` so a Flatpak cannot write over the
files a native build is using.

> **The two interact, and not in the obvious direction.** `--persist` and
> `--filesystem=home` cannot both be had: granting the whole home directory
> mounts it over the top of the persist bind, `$HOME` becomes the real home,
> and the application writes straight into the real `~/.quickdc` with no
> warning that anything was ignored. If you ever widen the filesystem grant to
> `home`, the persist line silently stops working. Check with:
>
> ```sh
> flatpak run --command=sh org.extatic.QuickDC -c 'ls ~/.quickdc'
> ```
>
> against the real `~/.quickdc`; if they match, persist is not in effect.

One consequence of all this is that a Flatpak install does **not** inherit an
existing native `~/.quickdc`. That is deliberate, but it means a tester coming
from a native build starts with an empty hub list and an empty hash cache.

---

## Distributing

### Single-file bundles

`--bundle` writes `dist/quickdc-qui-<version>-<arch>.flatpak`, about a megabyte.
Anyone can install one with:

```sh
flatpak install --user ./quickdc-qui-0.1.0~nightly20260811.flatpak
flatpak run org.extatic.QuickDC
```

The bundle is built with `--runtime-repo=…flathub.flatpakrepo`, which is what
lets somebody who has never installed a Flatpak before be offered the KDE
runtime rather than told it is missing. They will still be downloading that
runtime: about 405 MB, or nothing at all if they already have a Qt application
installed.

Two things bundles do not give you. They are unsigned, and there is no update
path - `flatpak update` knows nothing about a file somebody was handed, so every
new nightly is a fresh download of the whole thing. For a pre-release handed to
a handful of testers that is a fair trade against having to host anything.

The bundle carries the application only. The build also produces an
`org.extatic.QuickDC.Debug` ref in `build/flatpak-repo` holding the split-out
debug symbols, which is why the manifest builds `RelWithDebInfo` and why
`/lib/debug` is deliberately absent from the `cleanup:` list. It is not in the
bundle. If a tester sends you a stack trace and you want it readable, bundle
that ref too:

```sh
flatpak build-bundle --runtime build/flatpak-repo \
    quickdc-debug.flatpak org.extatic.QuickDC.Debug master
```

That one is about 24 MB, which is the other reason it is not in the app bundle.

### A hosted repository, when updates start to matter

The step up from bundles is an ostree repository served over plain HTTPS, after
which `flatpak update` works like it does for anything else. The pieces:

1. Keep one authoritative `repo/` on the server and pull it down before each
   build rather than rebuilding it from nothing - ostree repositories are
   incremental, and two divergent copies are a mess.
2. After `flatpak-builder --repo=repo`, run

   ```sh
   flatpak build-update-repo --generate-static-deltas --prune --prune-depth=3 \
       --title="QuickDC nightly" --gpg-sign=$KEY_ID repo
   ```

   The static deltas are what make a nightly update a few megabytes instead of
   the whole application again.
3. `rsync -a --delete repo/ user@host:/srv/flatpak/repo/` and serve it as
   static files.
4. Publish a `quickdc-nightly.flatpakrepo` beside it - `Url=`, `Title=` and
   `GPGKey=` holding the base64 of the exported public key - so that a user
   does this once and never again:

   ```sh
   flatpak remote-add --if-not-exists quickdc-nightly \
       https://dl.extatic.org/flatpak/quickdc-nightly.flatpakrepo
   flatpak install quickdc-nightly org.extatic.QuickDC
   ```

### What Flathub would additionally want

Nothing in this layout has to be redone, but four things are missing:

- **A git-source manifest.** Flathub will not take `type: dir`; it wants
  `type: git` with pinned commits, reachable anonymously. Because two of the
  submodule URLs are relative and resolve against an SSH remote, that manifest
  has to name all four repositories with absolute URLs and explicit `dest:`
  entries (`exotic`, `samurai`, `quickdc`), which bypasses `.gitmodules`
  altogether. Now that the three are siblings those are flat paths rather than
  nested ones. It also needs those repositories to be readable without
  credentials.
- **A screenshot.** `appstreamcli validate` warns without one and checks that
  the URL resolves when it is allowed network access, which Flathub's run is.
- **A real release history** rather than the single `type="development"` entry
  the metainfo carries today.
- **A defence of the filesystem permissions.** The reviewers will ask why the
  portal file chooser is not used, and the honest answer is that all three
  `QFileDialog` call sites pass `DontUseNativeDialog`. Fixing that first is the
  cheaper path.

---

## Verifying a build

```sh
./packaging/flatpak/build.sh

# The metadata, which flatpak-builder also checks and will fail the build over
P=~/.local/share/flatpak/app/org.extatic.QuickDC/current/active/files
desktop-file-validate $P/share/applications/org.extatic.QuickDC.desktop
appstreamcli validate --no-net $P/share/metainfo/org.extatic.QuickDC.metainfo.xml

# What ended up in /app
find build/flatpak/files -type f -not -path '*/lib/debug/*'

# It starts, which for this application means it bound its socket
QT_QPA_PLATFORM=offscreen timeout 20 flatpak run org.extatic.QuickDC; echo $?   # 124

# State is the package's own, not the real ~/.quickdc
ls ~/.var/app/org.extatic.QuickDC/.quickdc/
```

Exit code 124 is the pass: it means `timeout` had to stop the application, so
it got past `core.startAndWait()` and into the event loop. Any other code means
it gave up, and the reason will be on stderr.

The metadata validators run from CMake too, without a Flatpak in sight:

```sh
cmake --build build --target validate-metadata
```

Expect `bin/quickdc-qui`, the desktop entry, the metainfo and the icon, plus
three things nobody wrote by hand: `manifest.json` and the rasterised
`share/app-info/` icons, both generated by `flatpak-builder`, and
`share/licenses/org.extatic.QuickDC/quickdc-qui/LICENSE`, which it picks up
from the repository root by itself.

`find` excludes `/lib/debug` because that is the debug extension being staged
and is meant to be there. What should *not* appear is anything under
`/include`, `/lib/cmake`, or any `.a` file - the two vendored libraries install
themselves unconditionally, and the `cleanup:` list is what sweeps that up
rather than a patch to a pinned submodule.

---

## Things that will catch you out

**`icon-file-read-error`, and a build that fails on `appstreamcli compose`.**
The tool decides whether a file is an SVG by looking for `<svg` near the start
of it and gives up somewhere past the first couple of hundred bytes. A header
comment of any real length above the element pushes it out of reach and the
build fails with nothing pointing at the cause. This is why the commentary in
`../linux/app.svg` sits *inside* the element. If you edit that file, check:

```sh
python3 -c "print(open('packaging/linux/app.svg','rb').read().find(b'<svg'))"
```

Keep it small. Two hundred is fine; six hundred is not.

**Port 1411 already taken.** If you have a native build running - and during
development you usually do - it holds TCP and UDP 1411, and the sandboxed one
lands on top of it. `ss -ltnp 'sport = :1411'` will then show a listener that
is not the one you just built. Check whose it is before drawing conclusions:

```sh
ss -ltnp 'sport = :1411'    # note the pid
tr '\0' ' ' < /proc/<pid>/cmdline
```

A `bwrap` parent means the sandbox; a shell parent means your native build.

**A stale `packaging/nightly-version.txt`.** It is gitignored and `build.sh`
removes it on a non-`--nightly` run, but if one is left behind by hand every
build will claim to be that nightly.

**`--force-clean` does not clean the ccache.** That is the point - it lives in
`.flatpak-builder/` and is what makes the second build of the day quick. If you
suspect it, delete `.flatpak-builder/` outright.

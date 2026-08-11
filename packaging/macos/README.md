# Building, packaging and distributing the macOS bundle

QuickDC ships on macOS as a `QuickDC.app` bundle, handed out in a disk image.
This directory holds `build.sh`, which drives the whole thing, and `icon.png`,
the master the application icon is built from. The bundle's `Info.plist` and
the rules that assemble the icon live in `../../src/app`, next to the target
they belong to.

```sh
./packaging/macos/build.sh --dmg
open build/macos/src/app/QuickDC.app
```

> **Read the Gatekeeper section before you send the result to anybody.** These
> builds are not signed with a Developer ID and not notarised, and the first
> thing a recipient sees is a claim that the application is damaged.

---

## What you need

- **Xcode command line tools** — `xcode-select --install`. That is where
  `codesign`, `hdiutil`, `sips` and `iconutil` come from; the last two build
  the icon and are part of macOS, so there is nothing to install for it.
- **Qt 6.5 or newer for macOS.** The script looks for it in `$QT_ROOT`, then
  via `qmake6` on `PATH`, then in Homebrew's usual prefixes. If it finds none
  it says so and stops. `QT_ROOT=~/Qt/6.5.3/macos ./packaging/macos/build.sh`
  is the explicit form.
- **OpenSSL 3** — `brew install openssl@3`. Homebrew keeps it keg-only, because
  the LibreSSL that ships with macOS is older than the floor samurai enforces,
  so the script sets `OPENSSL_ROOT_DIR` from `brew --prefix` unless you have
  already set it yourself.
- **Ninja** — `brew install ninja`.

zlib and bzip2 come with the system.

## How this differs from the Flatpak

The two are deliberately the same shape — the same `--nightly`, `--pristine`
and version scheme, the same figure on the icon — but three things about macOS
have no Flatpak counterpart, and they are the parts worth understanding.

**There is no runtime.** A Flatpak names `org.kde.Platform` and gets Qt from
it; a `.app` has to carry Qt inside it. That is what `macdeployqt` does: it
copies the frameworks and the platform plugins into `Contents/Frameworks` and
rewrites every load command to match, which is what lets the bundle start on a
Mac that has never had Qt installed. Expect tens of megabytes where the Flatpak
was three, and expect Qt security updates to be yours to ship rather than the
runtime's.

**There is no sandbox to describe.** The whole `finish-args:` block has no
analogue. The application can read and write whatever the person running it
can, which among other things means the share picker and the download
directory need no special arrangement, and the `--persist` problem that shapes
the Flatpak's permissions does not arise. `~/.quickdc` is simply `~/.quickdc`,
shared with any other build of QuickDC on that Mac.

The one thing macOS does ask about is the local network, and it asks at
runtime: the first time the application reaches a LAN address, macOS puts up a
prompt, and `NSLocalNetworkUsageDescription` in the plist is the sentence shown
in it. That answer is remembered against the bundle identifier and the
signature, so it is re-asked whenever either changes — which, with ad-hoc
signing, is on more or less every build.

**Gatekeeper.** See below. This is the real difference.

## Architecture

The build targets whatever Mac it runs on. On Apple Silicon that is arm64, and
the result will not run on an Intel Mac at all — Rosetta translates the other
direction.

A universal binary is possible but not free: Qt's official macOS builds are
universal, but Homebrew's OpenSSL is built for the host architecture only, so
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` fails at the link. Getting there
means configuring and building OpenSSL 3 twice, `darwin64-arm64` and
`darwin64-x86_64`, then `lipo -create` on the two static libraries and pointing
`OPENSSL_ROOT_DIR` at the result. Until somebody needs it, building on each
machine and shipping two disk images is less work.

`CMAKE_OSX_DEPLOYMENT_TARGET` is pinned to 13.3 in the top-level
`CMakeLists.txt`. Qt 6.5's own floor is 11.0, but `std::format` sets a higher
one: libc++ keeps the floating point half of it in the dylib, which arrived in
13.3, so a lower target does not compile at all. Without any target the build
stamps whatever SDK the build machine has and quietly refuses to launch on
anything older.

---

## Building

| Option | What it does |
| --- | --- |
| *(none)* | Build `QuickDC.app` under `build/macos` |
| `--dmg` | Also write a disk image into `dist/` |
| `--nightly` | Stamp a dated version on the build |
| `--pristine <ref>` | Build a clean checkout of a ref rather than the working tree |
| `--open` | Reveal the bundle in Finder |

`--pristine` and `--nightly` work exactly as they do for the Flatpak, and for
the same reasons — see [../flatpak/README.md](../flatpak/README.md), which
explains both at length. The one macOS-specific wrinkle is the version:

`CFBundleShortVersionString` must be one to three integers separated by dots,
and Apple enforces it. `0.1.0~nightly20260811.fe654c9` is not a legal value, so
the plist puts the project version there — the number a person is shown — and
the nightly string in `CFBundleVersion`, which accepts anything. Both are
visible in Finder's Get Info panel, and the second is the one to ask a bug
reporter for.

### The icon

Built during the build rather than committed, from `icon.png` in this
directory: `sips` scales it into the ten sizes `iconutil` expects, `iconutil`
assembles `QuickDC.icns`, and CMake puts that in `Contents/Resources` where the
plist's `CFBundleIconFile` points at it.

`icon.png` is itself generated, from `../linux/app.svg` — the same drawing the
Linux icon uses, so there is one figure and not two to keep in step. Neither
`sips` nor `iconutil` can read an SVG, which is why a PNG master is committed
rather than the vector. To regenerate it after changing the SVG, on a machine
with librsvg:

```sh
rsvg-convert -w 1024 -h 1024 packaging/linux/app.svg -o packaging/macos/icon.png
```

1024 is deliberate: it makes the largest size `iconutil` wants (512@2x) a
straight copy rather than an upscale.

The plate is a plain rounded rectangle rather than the superellipse Apple's own
icons have used since Big Sur, and it fills the canvas where the guidelines ask
for a margin. It reads correctly at every size, but it will not look native
beside Apple's. Worth redrawing if this ever stops being a nightly.

---

## Gatekeeper, and what to tell people

The bundle is signed **ad-hoc** — `codesign --sign -`. That is not a choice so
much as a requirement: Apple Silicon will not load an unsigned binary at all,
and `macdeployqt` invalidates the signatures on every framework it rewrites, so
something has to re-sign the result. An ad-hoc signature satisfies the loader.
It does nothing whatsoever for Gatekeeper.

So when somebody downloads the disk image, macOS attaches a quarantine
attribute to it, finds no Developer ID, and reports:

> "QuickDC" is damaged and can't be opened. You should move it to the Bin.

This is a lie in every respect that matters — the download is intact — but it
is what people will see, and unprompted it reads as a broken build rather than
a policy. Say so first, and give them one of these:

```sh
# After dragging it to Applications, once:
xattr -dr com.apple.quarantine /Applications/QuickDC.app
```

or, without a terminal: right-click the application, choose **Open**, then
**Open** again in the dialog that appears. On recent macOS versions that path
has moved — if the dialog offers no Open button, it is **System Settings →
Privacy & Security**, scroll to the bottom, **Open Anyway**.

Copying the app out of the disk image before doing any of this matters; the
attribute is on the image as well.

### Making it go away properly

Signing and notarising is the only real fix, and it needs a paid Apple
Developer account. With one, the additions to `build.sh` are:

```sh
# Inside out: every framework macdeployqt copied, then the bundle itself.
# Not --deep, whatever the ad-hoc line in build.sh does - Apple documents it
# as the wrong tool for this and it signs nested code with the wrong flags.
find "$app/Contents/Frameworks" -name '*.framework' -o -name '*.dylib' \
    | while read -r item; do
        codesign --force --options runtime --timestamp \
            --sign "Developer ID Application: Your Name (TEAMID)" "$item"
      done

codesign --force --options runtime --timestamp \
    --sign "Developer ID Application: Your Name (TEAMID)" "$app"

xcrun notarytool submit "$out" --keychain-profile quickdc --wait
xcrun stapler staple "$out"
```

`--options runtime` — the hardened runtime — is not optional; notarisation
rejects anything without it. It also disallows a few things by default, so a
first attempt may need entitlements. `notarytool` uploads the image to Apple,
which takes a few minutes, and `stapler` writes the resulting ticket into the
image so it validates without network access.

Nothing in the current layout has to change to add this: it is one flag and two
extra commands in the script.

---

## Distributing

`--dmg` writes `dist/QuickDC-<version>-<arch>.dmg`, a compressed image
containing the application and a symlink to `/Applications` to drag it onto.
There is no update mechanism — no equivalent of the Flatpak repository idea —
so a new nightly is a fresh download. If that becomes tiresome before a real
release exists, [Sparkle](https://sparkle-project.org/) is what Mac
applications normally use, and it wants signed builds and an appcast feed.

Both architectures cannot come from one machine unless you do the universal
work above, so the honest options for now are to build on whichever Macs you
have and label the images by architecture, which the filename already does.

---

## Verifying a build

Everything here is worth running on a **different Mac** from the one that built
it, because most of what can go wrong — a framework that was not copied, a load
path that was not rewritten, a deployment target that is too new — cannot fail
on the build machine, which has Qt installed anyway.

```sh
# Nothing outside the bundle and the system. Any /opt/homebrew or ~/Qt path
# in here is a dependency that did not get copied in, and it will fail on
# somebody else's Mac.
otool -L build/macos/src/app/QuickDC.app/Contents/MacOS/QuickDC

# The same question, asked of everything at once
find build/macos/src/app/QuickDC.app -type f -perm +111 \
    -exec otool -L {} \; | grep -E '/opt/homebrew|/usr/local|Qt' | grep -v '@rpath'

# The signature is intact - macdeployqt runs after the link, so this checks
# nothing was rewritten afterwards
codesign --verify --deep --strict --verbose=2 build/macos/src/app/QuickDC.app

# What the bundle claims to be
plutil -p build/macos/src/app/QuickDC.app/Contents/Info.plist

# It starts, which for this application means it bound TCP and UDP 1411
build/macos/src/app/QuickDC.app/Contents/MacOS/QuickDC &
lsof -nP -iTCP:1411 -sTCP:LISTEN
lsof -nP -iUDP:1411
```

Running the binary directly rather than through `open` is deliberate: it puts
stderr in the terminal, where the "QuickDC could not start" case says why.

Then, by hand, the things the bundle changes: that the local-network prompt
appears on the first hub connection and carries the sentence from the plist;
that the icon is right in the Dock and in Finder; and that a hub connects over
TLS, which is the one that proves OpenSSL came along.

---

## Things that will catch you out

**`macdeployqt` invalidates signatures, so it has to run before `codesign`.**
The script does them in that order. Reverse them and the bundle will not launch
on Apple Silicon, with an error that says nothing useful.

**A second `macdeployqt` run over an already-deployed bundle** leaves the first
run's copies in place unless it is given `-always-overwrite`, which the script
passes. If a bundle behaves strangely after a rebuild, delete `build/macos` and
start again.

**`~/.quickdc` is shared with every other build on the Mac.** There is no
sandbox here, so a bundle and a development build use the same configuration,
the same download queue and the same hash cache. Do not run two at once — they
will both try to bind 1411 and both write `queue.xml`.

**The local-network prompt is remembered against the signature.** Ad-hoc
signatures change from build to build, so expect to answer it again after
almost every rebuild, and do not read a re-prompt as a bug.

**A stale `packaging/nightly-version.txt`.** Shared with the Flatpak script, and
gitignored. A non-`--nightly` run removes it, but one left behind by hand makes
every build claim to be that nightly.

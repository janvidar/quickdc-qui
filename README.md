# quickdc-qui

A Qt 6 user interface for [QuickDC](quickdc/README), a client for the Direct
Connect file sharing networks.

## Building

Qt 6.5 or newer, a C++20 compiler, and CMake 3.16 or newer. OpenSSL 3, zlib and
bzip2 are wanted by `samurai`, which the core is built on.

```sh
git submodule update --init
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Three submodules, side by side rather than nested: `exotic` is the test
framework, `samurai` the platform library the core is built on, and `quickdc`
the core itself. Each is added once, from here, and each finds the ones it needs
already present - so the init above does not need `--recursive`.

All three are named relatively, as `../exotic.git` and so on, so they are
fetched from whichever host this repository was cloned from and never from a
host named in a file. A clone from the staging remote takes its submodules from
there and a clone from the public one takes them from there, without either
having to be told which it is.

That builds this project's own tests. The core's suite and samurai's are off by
default, being some 1160 and 1170 tests against this project's 12; ask for
either and it is built and run from the same build directory:

```sh
cmake -S . -B build -DQUICKDC_BUILD_TESTS=ON -DSAMURAI_BUILD_TESTS=ON
```

`cmake -DCMAKE_PREFIX_PATH=/path/to/qt6` if Qt is somewhere CMake will not find
by itself.

## Packaging

On Linux, as a Flatpak:

```sh
./packaging/flatpak/build.sh
flatpak run org.extatic.QuickDC
```

On macOS, as an application bundle in a disk image:

```sh
./packaging/macos/build.sh --dmg
```

[packaging/flatpak/README.md](packaging/flatpak/README.md) and
[packaging/macos/README.md](packaging/macos/README.md) cover how each works,
how nightly builds are made and how they are distributed. Both take the same
`--nightly` and `--pristine` options and produce the same version strings.

The desktop entry, the icon and the AppStream metadata the Linux package
installs are in [packaging/linux](packaging/linux), and are part of an ordinary
`cmake --install` as well; the macOS bundle draws its icon from the same
drawing.

Note that macOS builds are not signed with an Apple Developer ID, so anybody
who is handed one will be told the application is damaged until they clear the
quarantine attribute. The macOS README explains what to tell them.

A Snap is sketched in [packaging/snap.md](packaging/snap.md) and deliberately
not built: it is worth doing only if it goes to the Snap Store, whose edge
channel is the one thing that would give nightlies automatic updates.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).

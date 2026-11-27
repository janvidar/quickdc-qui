#!/bin/sh
#
## Build QuickDC as an AppImage.
##
##   appimage.sh            the version from git (packaging/version.sh)
##   appimage.sh --nightly  a dated nightly version
##
## Needs Qt ($QT_ROOT, or qmake6 on PATH), the build dependencies of the core,
## Ninja, curl and objcopy. Build it on the oldest distribution it should run on:
## an AppImage carries its libraries but not glibc, so it runs where glibc is at
## least as new as where it was built. Build it with that distribution's own
## compiler too: the C++ runtime is the system's, never carried, so it has to be
## no newer than the one on the oldest system supported.
##
## Writes dist/QuickDC-<version>-x86_64.AppImage and the debug symbols beside it as
## dist/QuickDC-<version>-x86_64-symbols.tar.xz.
#

set -eu

here=$(cd -- "$(dirname -- "$0")" && pwd)
root=$(cd -- "$here/../.." && pwd)
cd "$root"

nightly=
case "${1:-}" in
	--nightly) nightly=--nightly ;;
	"") ;;
	-h|--help) sed -n 's/^## \{0,1\}//p' "$0"; exit 0 ;;
	*) echo "$0: unknown option: $1" >&2; exit 2 ;;
esac

version=$(packaging/version.sh $nightly)
# The update channel follows from the version: a nightly's, a release's, or none
# for a build made by hand, which then never updates.
case "$version" in
	*~nightly*) channel=nightly ;;
	*+*)        channel= ;;
	*)          channel=stable ;;
esac

if [ -n "${QT_ROOT:-}" ]; then
	qmake="$QT_ROOT/bin/qmake"
elif command -v qmake6 >/dev/null 2>&1; then
	qmake=$(command -v qmake6)
else
	echo "$0: cannot find Qt; set QT_ROOT to the Qt prefix" >&2
	exit 1
fi
qt_prefix=$("$qmake" -query QT_INSTALL_PREFIX)

# Pinned, and checked: these run with this user's rights over the build.
tools="$root/build/appimage-tools"
mkdir -p "$tools"
fetch() {
	url=$1 file=$2 sum=$3
	if [ ! -f "$tools/$file" ] || ! echo "$sum  $tools/$file" | sha256sum -c --status; then
		curl -fsSL -o "$tools/$file" "$url"
		echo "$sum  $tools/$file" | sha256sum -c --status || {
			echo "$0: $file does not have the checksum it was pinned with" >&2
			rm -f "$tools/$file"
			exit 1
		}
	fi
	chmod +x "$tools/$file"
}
fetch https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20250213-2/linuxdeploy-x86_64.AppImage \
	linuxdeploy-x86_64.AppImage 4648f278ab3ef31f819e67c30d50f462640e5365a77637d7e6f2ad9fd0b4522a
fetch https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage \
	linuxdeploy-plugin-qt-x86_64.AppImage 15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724

build="$root/build/appimage"
appdir="$build/AppDir"
mkdir -p "$root/dist"

echo "Building QuickDC $version"

cmake -S . -B "$build" -G Ninja \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DCMAKE_PREFIX_PATH="$qt_prefix" \
	-DQUICKDC_QUI_VERSION_STRING="$version" \
	-DQUICKDC_UPDATE_CHANNEL="$channel" \
	-DQUICKDC_QUI_INSTALL=ON \
	-DQUICKDC_QUI_BUILD_TESTS=OFF \
	-DQUICKDC_BUILD_TESTS=OFF \
	-DQUICKDC_BUILD_CLI=OFF \
	-DQUICKDC_TRACE=OFF \
	-DQUICKDC_SANITIZE=OFF \
	-DQUICKDC_ALLOW_LOOPBACK_AUTH=OFF

cmake --build "$build" --target quickdc-qui

app_id=$(sed -n 's/^QUICKDC_QUI_APP_ID:STRING=//p' "$build/CMakeCache.txt")
exe="$build/src/app/quickdc-qui"

# The symbols kept apart and linked by name, then stripped from what ships.
symbols="$root/dist/QuickDC-$version-x86_64-symbols.tar.xz"
objcopy --only-keep-debug "$exe" "$build/quickdc-qui.debug"
objcopy --strip-debug --add-gnu-debuglink="$build/quickdc-qui.debug" "$exe" "$build/quickdc-qui"
tar -C "$build" -cJf "$symbols" quickdc-qui.debug

rm -rf "$appdir"
cp "$here/app.svg" "$build/$app_id.svg"

# No FUSE on a CI runner, so the tools unpack themselves instead of mounting.
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE="$qmake"
# Wayland as well as X11, which is the only one the plugin takes by default.
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so"
export LDAI_OUTPUT="$root/dist/QuickDC-$version-x86_64.AppImage"
export PATH="$tools:$PATH"

"$tools/linuxdeploy-x86_64.AppImage" --appdir "$appdir" \
	--executable "$build/quickdc-qui" \
	--desktop-file "$build/packaging/linux/$app_id.desktop" \
	--icon-file "$build/$app_id.svg" \
	--plugin qt --output appimage

[ -f "$LDAI_OUTPUT" ] || { echo "$0: linuxdeploy wrote no $LDAI_OUTPUT" >&2; exit 1; }

[ -z "$channel" ] || "$root/packaging/feed-entry.sh" "$channel" linux-x86_64 "$version" "$LDAI_OUTPUT" \
	> "$root/dist/linux-x86_64.unsigned.json"

echo "AppImage: $LDAI_OUTPUT"
echo "Symbols:  $symbols"

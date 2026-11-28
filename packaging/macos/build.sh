#!/bin/sh
#
## Build QuickDC as a macOS application bundle
##
##   build.sh                              build QuickDC.app
##   build.sh --dmg                        also write a disk image
##   build.sh --nightly                    stamp a dated version on it
##   build.sh --pristine origin/master     build a clean checkout of a ref
##   build.sh --open                       reveal the result in Finder
##   build.sh --universal                  Apple Silicon and Intel in one; needs an
##                                         OpenSSL with both ($OPENSSL_ROOT_DIR)
##
## A nightly, from cron or launchd:
##
##   17 3 * * *  cd ~/source/quickdc-qui && git fetch -q && \
##               packaging/macos/build.sh --nightly --pristine origin/master \
##               --dmg >> ~/Library/Logs/quickdc-nightly.log 2>&1
##
## The result is NOT signed with a Developer ID and NOT notarised, so anyone who
## downloads the disk image will be told the application is damaged. That is
## Gatekeeper reacting to the quarantine attribute, not a broken build; see
## packaging/macos/README.md for what to tell them.
##
## Copyright (c) 2026, Jan Vidar Krey
#

set -eu

here=$(cd -- "$(dirname -- "$0")" && pwd)
root=$(cd -- "$here/../.." && pwd)

BUNDLE_NAME=QuickDC

dmg=no
nightly=no
reveal=no
universal=no
pristine=

while [ $# -gt 0 ]; do
	case $1 in
		--dmg)     dmg=yes ;;
		--nightly) nightly=yes ;;
		--open)    reveal=yes ;;
		--universal) universal=yes ;;
		--pristine)
			if [ $# -lt 2 ]; then
				echo "$0: --pristine needs a ref" >&2
				exit 2
			fi
			pristine=$2
			shift
			;;
		-h|--help)
			sed -n 's/^## \{0,1\}//p' "$0"
			exit 0
			;;
		*)
			echo "$0: unknown option: $1" >&2
			exit 2
			;;
	esac
	shift
done

if [ "$(uname -s)" != "Darwin" ]; then
	echo "$0: this builds a macOS bundle and only runs on macOS." >&2
	echo "    For Linux, see packaging/flatpak/build.sh" >&2
	exit 1
fi

#
# Qt, and macdeployqt beside it.
#
# macdeployqt is the whole reason a bundle is more than what CMake produces: it
# copies the Qt frameworks and the platform plugins into Contents/Frameworks and
# rewrites the load paths to match, which is what lets the application start on
# a Mac that has never had Qt on it.
#
if [ -n "${QT_ROOT:-}" ]; then
	qt_bin="$QT_ROOT/bin"
elif command -v qmake6 >/dev/null 2>&1; then
	qt_bin=$(qmake6 -query QT_INSTALL_BINS)
elif [ -d /opt/homebrew/opt/qt6/bin ]; then
	qt_bin=/opt/homebrew/opt/qt6/bin
elif [ -d /usr/local/opt/qt6/bin ]; then
	qt_bin=/usr/local/opt/qt6/bin
else
	echo "$0: cannot find Qt. Set QT_ROOT to the Qt prefix, e.g." >&2
	echo "    QT_ROOT=~/Qt/6.5.3/macos $0" >&2
	exit 1
fi

macdeployqt="$qt_bin/macdeployqt"

if [ ! -x "$macdeployqt" ]; then
	echo "$0: no macdeployqt in $qt_bin" >&2
	exit 1
fi

#
# OpenSSL. Homebrew keeps it keg-only because the system LibreSSL is older than
# what samurai will accept, so CMake has to be pointed at it by hand.
#
if [ -z "${OPENSSL_ROOT_DIR:-}" ] && command -v brew >/dev/null 2>&1; then
	if brew --prefix openssl@3 >/dev/null 2>&1; then
		OPENSSL_ROOT_DIR=$(brew --prefix openssl@3)
		export OPENSSL_ROOT_DIR
	fi
fi

#
# A clean checkout, when one is asked for. Same reasoning as the Flatpak
# script: the submodule URLs point at remotes this may have no credentials for,
# so each is pointed at the copy already on this disk. The four are siblings,
# so one pass over them is the whole of it.
#
if [ -n "$pristine" ]; then
	stage="$root/build/macos-stage"

	rm -rf "$stage"
	git clone --quiet --no-checkout "$root" "$stage"
	git -C "$stage" checkout --quiet "$pristine"

	git -C "$stage" submodule --quiet init

	for module in exotic samurai libdrop-cpp quickdc; do
		git -C "$stage" config "submodule.$module.url" "$root/$module"
	done

	# protocol.file.allow, for the reason the Flatpak script gives: git stopped
	# allowing a submodule URL to name a local path by default in 2.38.1, and
	# every URL here was written a line above.
	git -C "$stage" -c protocol.file.allow=always submodule --quiet update

	source_root="$stage"
else
	source_root="$root"
fi

#
# The version, by the rule every package follows (packaging/version.sh).
#
if [ "$nightly" = yes ]; then
	version=$(cd "$source_root" && packaging/version.sh --nightly)
else
	version=$(cd "$source_root" && packaging/version.sh)
fi

# The update channel follows from the version: a nightly's, a release's, or none
# for a build made by hand, which then never updates.
case "$version" in
	*~nightly*) channel=nightly ;;
	*+*)        channel= ;;
	*)          channel=stable ;;
esac

if [ "$universal" = yes ]; then
	architectures="arm64;x86_64"
	arch=universal
else
	architectures=$(uname -m)
	arch=$(uname -m)
fi

builddir="$root/build/macos"
app="$builddir/src/app/$BUNDLE_NAME.app"

echo "Building $BUNDLE_NAME.app $version from $source_root"

cmake -S "$source_root" -B "$builddir" -G Ninja \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DQUICKDC_QUI_BUILD_TESTS=OFF \
	-DQUICKDC_TRACE=OFF \
	-DQUICKDC_SANITIZE=OFF \
	-DQUICKDC_ALLOW_LOOPBACK_AUTH=OFF \
	-DQUICKDC_QUI_VERSION_STRING="$version" \
	-DQUICKDC_UPDATE_CHANNEL="$channel" \
	-DQUICKDC_QUI_CRASH_REPORTS="${QUICKDC_CRASH_REPORTS:-ON}" \
	-DQUICKDC_QUI_CRASH_DSN="${QUICKDC_CRASH_DSN:-}" \
	-DCMAKE_OSX_ARCHITECTURES="$architectures" \
	-DCMAKE_PREFIX_PATH="${QT_ROOT:-$qt_bin/..}"

cmake --build "$builddir" --target quickdc-qui

if [ ! -d "$app" ]; then
	echo "$0: expected a bundle at $app and there is none" >&2
	exit 1
fi

#
# Everything the bundle needs to run elsewhere.
#
# -always-overwrite because a second run over a bundle macdeployqt has already
# processed otherwise leaves the first run's copies in place.
#
#
# The debug symbols, taken before anything below rewrites the binary, for the
# crash reports of phase 6 (docs/distribution.md) to be read against.
#
mkdir -p "$root/dist"
dsym="$builddir/$BUNDLE_NAME.dSYM"
rm -rf "$dsym"
dsymutil "$app/Contents/MacOS/$BUNDLE_NAME" -o "$dsym"
symbols="$root/dist/QuickDC-$version-$arch-symbols.zip"
rm -f "$symbols"
(cd "$builddir" && ditto -c -k --keepParent "$BUNDLE_NAME.dSYM" "$symbols")
echo "Symbols: $symbols"

echo "Deploying Qt into the bundle"
"$macdeployqt" "$app" -always-overwrite

#
# Apple Silicon will not load an unsigned binary at all, and macdeployqt has
# just rewritten load commands in every framework it copied, which invalidates
# whatever signature they arrived with. An ad-hoc signature - the "-" identity -
# is not a Developer ID and does nothing for Gatekeeper, but it is what makes
# the thing runnable on the machine that built it.
#
echo "Signing ad-hoc"
codesign --force --deep --sign - "$app"
codesign --verify --verbose=1 "$app" || {
	echo "$0: the ad-hoc signature did not verify" >&2
	exit 1
}

echo "Bundle: $app"

# What the updater installs from: the bundle itself, zipped, not the disk image.
appzip="$root/dist/QuickDC-$version-$arch.app.zip"
rm -f "$appzip"
(cd "$(dirname "$app")" && ditto -c -k --keepParent "$BUNDLE_NAME.app" "$appzip")
echo "Update package: $appzip"
if [ -n "$channel" ] && [ "$universal" = yes ]; then
	"$root/quickdc/tools/feed-entry.sh" "$channel" macos-universal "$version" "$appzip" \
		> "$root/dist/macos-universal.unsigned.json"
fi

if [ "$dmg" = yes ]; then
	out="$root/dist/QuickDC-$version-$arch.dmg"
	staging="$builddir/dmg"

	# A staging directory rather than macdeployqt -dmg, for the one thing that
	# makes a disk image usable: the symlink people drag the application onto.
	rm -rf "$staging" "$out"
	mkdir -p "$staging"
	cp -R "$app" "$staging/"
	ln -s /Applications "$staging/Applications"

	hdiutil create -volname "$BUNDLE_NAME" -srcfolder "$staging" \
		-ov -format UDZO "$out"

	echo "Disk image: $out"
fi

if [ "$reveal" = yes ]; then
	open -R "$app"
fi

cat <<EOF

Built, but not signed with a Developer ID and not notarised. Opening it on
another Mac will report that the application is damaged; that is the quarantine
attribute, and packaging/macos/README.md says what to do about it.

EOF

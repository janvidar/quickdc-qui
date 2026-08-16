#!/bin/sh
#
## Build QuickDC as a Flatpak
##
##   build.sh                              build and install for this user
##   build.sh --bundle                     also write a single-file .flatpak
##   build.sh --nightly                    stamp a dated version on it
##   build.sh --pristine origin/master     build a clean checkout of a ref
##   build.sh --no-install                 build only
##
## The sandbox can see the standard media directories and nothing else, so a
## share kept anywhere else has to be let in by hand, once:
##
##   flatpak override --user --filesystem=/mnt/media org.extatic.QuickDC
##
## A nightly is the three of them together, from cron:
##
##   17 3 * * *  cd ~/source/quickdc-qui && git fetch -q && \
##               packaging/flatpak/build.sh --nightly --pristine origin/master \
##               --bundle --no-install >> ~/.cache/quickdc-nightly.log 2>&1
##
## Copyright (c) 2026, Jan Vidar Krey
#

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)

APP_ID=org.extatic.QuickDC

# One place, because the manifest names it too and the two have to agree.
RUNTIME_VERSION=$(sed -n "s/^runtime-version: *'\\(.*\\)'/\\1/p" \
	"$here/$APP_ID.yml")
BRANCH=master

bundle=no
nightly=no
install=yes
pristine=

while [ $# -gt 0 ]; do
	case $1 in
		--bundle)     bundle=yes ;;
		--nightly)    nightly=yes ;;
		--no-install) install=no ;;
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

#
# Everything below installs --user, and a system-wide flathub remote is not
# visible to a user installation - so the remote has to exist for this user
# whether or not the machine already has one. Idempotent.
#
flatpak remote-add --user --if-not-exists \
	flathub https://dl.flathub.org/repo/flathub.flatpakrepo

#
# flatpak-builder, from wherever there is one.
#
# The org.flatpak.Builder flatpak is preferred over nothing rather than over a
# distribution package, and it is the better fallback of the two: it carries
# appstreamcli, desktop-file-validate and flatpak-builder-lint as well, which is
# the whole verification set in one install.
#
if command -v flatpak-builder >/dev/null 2>&1; then
	builder='flatpak-builder'
else
	echo "flatpak-builder not found; installing org.flatpak.Builder"
	flatpak install --user --noninteractive --or-update \
		flathub org.flatpak.Builder
	builder='flatpak run org.flatpak.Builder'
fi

flatpak install --user --noninteractive --or-update flathub \
	"org.kde.Platform//$RUNTIME_VERSION" "org.kde.Sdk//$RUNTIME_VERSION"

#
# A clean checkout, when one is asked for.
#
# Not `git clone --recursive`: two of the three submodule URLs are relative and
# resolve against the SSH remote, and exotic's is a GitHub one, so each is
# pointed back at the tree already on this disk instead. That also means a
# nightly does not need network access to anything but the runtime.
#
# One pass rather than one per level: the three are siblings, so there is no
# inner checkout to descend into and init a second time.
#
if [ -n "$pristine" ]; then
	stage="$root/build/flatpak-stage"

	rm -rf "$stage"
	git clone --quiet --no-checkout "$root" "$stage"
	git -C "$stage" checkout --quiet "$pristine"

	git -C "$stage" submodule --quiet init

	for module in exotic samurai quickdc; do
		git -C "$stage" config "submodule.$module.url" "$root/$module"
	done

	# protocol.file.allow, because pointing a submodule at a path is exactly what
	# git stopped allowing by default in 2.38.1 - CVE-2022-39253, where a
	# submodule URL naming a local directory could be made to read one the
	# cloner did not intend. Every URL here was just written a line above, so
	# there is nothing here for that to reach.
	git -C "$stage" -c protocol.file.allow=always submodule --quiet update

	source_root="$stage"
else
	source_root="$root"
fi

#
# The version this build claims to be.
#
# There are no tags in this repository, so `git describe` has nothing to
# describe and a nightly has to name itself. The tilde is what makes
# 0.1.0~nightly20260811.fe654c9 sort before 0.1.0 rather than after it, so a
# real release supersedes every nightly that led up to it.
#
# It reaches the metainfo through the file rather than through an argument:
# flatpak-builder has no way to pass a CMake variable in, and the top level
# reads packaging/nightly-version.txt before it tries anything else.
#
version_file="$source_root/packaging/nightly-version.txt"

if [ "$nightly" = yes ]; then
	version="0.1.0~nightly$(date -u +%Y%m%d).$(git -C "$source_root" rev-parse --short HEAD)"
	echo "$version" > "$version_file"
else
	rm -f "$version_file"
	version=$(git -C "$source_root" describe --always --dirty 2>/dev/null || echo unknown)
fi

builddir="$root/build/flatpak"
repo="$root/build/flatpak-repo"

echo "Building $APP_ID $version from $source_root"

# --disable-rofiles-fuse because FUSE is not available in most containers and
# costs nothing here; --ccache because the second build of the day should not
# recompile samurai from the top.
$builder --user --force-clean --ccache --disable-rofiles-fuse \
	--install-deps-from=flathub \
	--repo="$repo" --default-branch="$BRANCH" \
	"$builddir" "$source_root/packaging/flatpak/$APP_ID.yml"

if [ "$bundle" = yes ]; then
	mkdir -p "$root/dist"
	out="$root/dist/quickdc-qui-$version-$(uname -m).flatpak"

	# --runtime-repo so that somebody who has never installed a Flatpak before
	# is offered the KDE runtime rather than told it is missing.
	flatpak build-bundle \
		--runtime-repo=https://flathub.org/repo/flathub.flatpakrepo \
		"$repo" "$out" "$APP_ID" "$BRANCH"

	echo "Bundle: $out"
fi

if [ "$install" = yes ]; then
	flatpak install --user --noninteractive --or-update --reinstall \
		"$repo" "$APP_ID"

	cat <<EOF

Installed. Run it with:

    flatpak run $APP_ID

EOF
fi

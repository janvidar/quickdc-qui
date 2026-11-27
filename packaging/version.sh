#!/bin/sh
#
## The version a package is stamped with, from the checkout in the current directory.
##
##   version.sh            X.Y.Z if HEAD is tagged vX.Y.Z, else X.Y.Z+<describe>
##   version.sh --nightly  X.Y.Z~nightlyYYYYMMDD.<sha>
##
## X.Y.Z is project(VERSION) in CMakeLists.txt; a tag that disagrees with it is an
## error, so a release cannot go out calling itself something else.
#

set -eu

project=$(sed -n '/^project(quickdc-qui/,/)/s/^ *VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$project" ] || { echo "version.sh: no project VERSION in CMakeLists.txt" >&2; exit 1; }

sha=$(git rev-parse --short HEAD)

if [ "${1:-}" = "--nightly" ]; then
	echo "$project~nightly$(date -u +%Y%m%d).$sha"
	exit 0
fi

tag=$(git tag --points-at HEAD 'v[0-9]*' | head -n 1)
if [ -n "$tag" ]; then
	if [ "${tag#v}" != "$project" ]; then
		echo "version.sh: HEAD is tagged $tag but CMakeLists.txt says $project" >&2
		exit 1
	fi
	echo "$project"
	exit 0
fi

echo "$project+$(git describe --always --dirty)"

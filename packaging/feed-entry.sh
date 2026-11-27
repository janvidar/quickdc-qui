#!/bin/sh
#
## The feed entry for one package, unsigned: what the signing step (phase 4,
## quickdc/tools/update-feed) is handed. Prints JSON.
##
##   feed-entry.sh <channel> <platform> <version> <package file>
#

set -eu

[ $# -eq 4 ] || { sed -n 's/^## \{0,1\}//p' "$0" >&2; exit 2; }
channel=$1 platform=$2 version=$3 package=$4

size=$(wc -c < "$package" | tr -d ' ')
if command -v sha256sum >/dev/null 2>&1; then
	sha=$(sha256sum "$package" | cut -d' ' -f1)
else
	sha=$(shasum -a 256 "$package" | cut -d' ' -f1)
fi
file=$(basename "$package")

cat <<J
{
  "format": 1,
  "channel": "$channel",
  "platform": "$platform",
  "version": "$version",
  "url": "$version/$file",
  "size": $size,
  "sha256": "$sha"
}
J

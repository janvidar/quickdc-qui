#!/bin/sh
#
## On the download server: remove nightlies older than a number of days, but
## never one a feed still names. For the deploy user's crontab, e.g.
##
##   17 4 * * *  /home/deploy/prune-nightlies.sh /web/download.quickdc.org 14
##
##   prune-nightlies.sh <download root> [days, default 14]
#

set -eu

root=${1:?usage: prune-nightlies.sh <download root> [days]}
days=${2:-14}
nightly="$root/nightly"
[ -d "$nightly" ] || exit 0

# What the feeds name: the first path component of each "url".
named=$(sed -n 's/^  "url": "\([^/"]*\)\/.*/\1/p' "$nightly"/*.json 2>/dev/null | sort -u)

find "$nightly" -mindepth 1 -maxdepth 1 -type d -name '*~nightly*' -mtime +"$days" | while read -r dir; do
	version=$(basename "$dir")
	if printf '%s\n' "$named" | grep -qxF "$version"; then continue; fi
	rm -rf -- "$dir"
	echo "removed $version"
done

#!/bin/sh
#
## Sign and publish a release's feeds, on your own machine: the one step of a
## release that needs the stable key, which never goes to CI or to the server.
##
##   packaging/release-feeds.sh <version>
##
## CI has put the packages and their unsigned entries in stable/<version>/ already.
## This fetches the entries, checks each against its package as served, signs it
## with $STABLE_SEED (default ~/quickdc-keys/stable.seed) and uploads the signed
## feeds to stable/ with the deploy key ($DEPLOY_KEY, default ~/quickdc-deploy).
## Every install on the stable channel takes the update from then on.
#

set -eu

version=${1:?usage: release-feeds.sh <version>}
seed=${STABLE_SEED:-$HOME/quickdc-keys/stable.seed}
key=${DEPLOY_KEY:-$HOME/quickdc-deploy}
site=${DOWNLOAD_SITE:-https://download.quickdc.org}
host=${DEPLOY_HOST:-deploy@download.quickdc.org}
here=$(cd -- "$(dirname -- "$0")/.." && pwd)

[ -r "$seed" ] || { echo "release-feeds.sh: no stable seed at $seed" >&2; exit 1; }
[ -r "$key" ] || { echo "release-feeds.sh: no deploy key at $key" >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/stable"

for platform in linux-x86_64 macos-universal windows-x86_64; do
	entry="$work/$platform.unsigned.json"
	curl -fsS -o "$entry" "$site/stable/$version/$platform.unsigned.json" \
		|| { echo "release-feeds.sh: $site/stable/$version/$platform.unsigned.json is not there" >&2; exit 1; }

	grep -q "\"channel\": \"stable\"" "$entry" || { echo "release-feeds.sh: $platform's entry is not for stable" >&2; exit 1; }
	grep -q "\"version\": \"$version\"" "$entry" || { echo "release-feeds.sh: $platform's entry is not $version" >&2; exit 1; }

	# What is signed is checked against what is served, so a changed package is not vouched for.
	url=$(sed -n 's/^  "url": "\(.*\)",$/\1/p' "$entry")
	size=$(sed -n 's/^  "size": \([0-9]*\),$/\1/p' "$entry")
	sha=$(sed -n 's/^  "sha256": "\([0-9a-f]*\)"$/\1/p' "$entry")
	curl -fsS -o "$work/package" "$site/stable/$url"
	served_size=$(wc -c < "$work/package" | tr -d ' ')
	if command -v sha256sum >/dev/null 2>&1; then served_sha=$(sha256sum "$work/package" | cut -d' ' -f1)
	else served_sha=$(shasum -a 256 "$work/package" | cut -d' ' -f1); fi
	[ "$served_size" = "$size" ] && [ "$served_sha" = "$sha" ] \
		|| { echo "release-feeds.sh: the $platform package served is not the one CI built" >&2; exit 1; }

	"$here/quickdc/tools/sign-feed.sh" "$entry" "$seed" > "$work/stable/$platform.json"
	echo "$platform: signed $url"
done

ssh="ssh -i $key -o IdentitiesOnly=yes"
rsync -rt --delay-updates --chmod=D755,F644 -e "$ssh" "$work/stable" "$host:"

for platform in linux-x86_64 macos-universal windows-x86_64; do
	curl -fsS "$site/stable/$platform.json" | grep -q "\"version\": \"$version\"" \
		|| { echo "release-feeds.sh: $site/stable/$platform.json does not offer $version" >&2; exit 1; }
done
echo "Stable now offers $version."

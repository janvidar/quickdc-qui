#!/bin/sh
#
# Nothing a view can reach may wait for the core thread.
#
# The whole layer exists so that the thread which draws never blocks on the one
# that runs the reactor, and a reactor turn is bounded by nothing - it writes
# transfer chunks to disk, rewrites the download queue, compresses the share's
# file list and answers other people's searches out of the whole share index.
# One blocking call on a drawing path is a frozen window for as long as that
# takes, and it is one line to add by accident.
#
# So the blocking primitive is named for what it does, and this says where it is
# allowed to appear. CoreThread owns it; ConfigBridge uses it once, at start-up,
# to fill its copy of the preference files; the tests use it as a barrier.
#
# Usage: nothing-blocks-the-views.sh <source directory>

set -eu

root="${1:-.}"

allowed='src/core/corethread.h
src/core/corethread.cpp
src/core/configbridge.cpp
tests/'

status=0

for file in $(cd "$root" && git grep -l 'runOnCoreBlocking(' -- src tests || true)
do
	ok=0

	for pattern in $allowed
	do
		case "$file" in
			"$pattern"*) ok=1 ;;
		esac
	done

	if [ "$ok" -eq 0 ]
	then
		echo "$file: waits for the core thread, which nothing that draws may do." >&2
		echo "  Use CoreThread::call() and take the answer when it arrives." >&2
		status=1
	fi
done

exit $status

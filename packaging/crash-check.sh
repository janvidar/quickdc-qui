#!/bin/sh
#
## After QuickDC --test-crash: whether the crash handler left a minidump where the
## program said it keeps them. Fails the CI step if not.
##
##   crash-check.sh <the program's output>
#

set -eu

where=$(sed -n 's/^QuickDC: crash reports are kept in //p' "$1" | tail -n 1 | tr -d '\r')
if [ -z "$where" ]; then
	echo "::error::QuickDC did not say where crash reports are kept: built without them?"
	exit 1
fi
case "$where" in [A-Za-z]:*) command -v cygpath >/dev/null 2>&1 && where=$(cygpath -u "$where") ;; esac

# The handler writes after the process has died, so it is given a moment.
for n in 1 2 3 4 5 6 7 8 9 10; do
	if find "$where" -name '*.dmp' 2>/dev/null | grep -q .; then
		echo "minidump written:"
		find "$where" -name '*.dmp'
		exit 0
	fi
	sleep 1
done

echo "::error::no minidump in $where"
find "$where" 2>/dev/null | head -20
exit 1

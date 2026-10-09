#!/bin/sh
# $Id$
#
# Run the smolbox test suite inside a smolBSD dev microVM (host side).
#
#	tests/run-vm.sh [--rebuild]
#
# Requires a smolBSD checkout (SMOLBSD env var, default ~/src/smolBSD)
# with its builder image and kernels/netbsd-SMOL already fetched.
# The suite output is streamed from test-results.txt on return.

SMOLBSD=${SMOLBSD:-$HOME/src/smolBSD}
SRC=$(cd "$(dirname "$0")/.." && pwd)
IMG="smolbox-dev-amd64:latest"
RESULTS=$SRC/test-results.txt
DONE=$SRC/test-done
LOG=/tmp/smolbox-vm.log

if [ ! -d "$SMOLBSD" ]; then
	echo "smolBSD checkout not found in $SMOLBSD (set SMOLBSD=)" >&2
	exit 1
fi

if [ ! -f "$SMOLBSD/images/$IMG.img" ] || [ "$1" = "--rebuild" ]; then
	echo "==> building dev image (first time takes a while)"
	(cd "$SMOLBSD" && ./smoler.sh build -y "$SRC/tests/smolbox-dev.smol") || exit 1
fi

rm -f "$RESULTS" "$DONE"

echo "==> booting dev microVM (console: $LOG)"
( cd "$SMOLBSD" && exec ./startnb.sh -k kernels/netbsd-SMOL \
	-i "images/$IMG.img" -w "$SRC" -m 512 ) </dev/null >"$LOG" 2>&1 &
nbpid=$!

kill_vm() {
	if [ -f "$SMOLBSD/qemu-smolbox-dev.pid" ]; then
		kill "$(cat "$SMOLBSD/qemu-smolbox-dev.pid")" 2>/dev/null
		rm -f "$SMOLBSD/qemu-smolbox-dev.pid"
	fi
	kill "$nbpid" 2>/dev/null
}

i=0
while [ ! -f "$DONE" ]; do
	i=$((i + 1))
	if ! kill -0 "$nbpid" 2>/dev/null; then
		echo "!! startnb exited early, see $LOG" >&2
		exit 1
	fi
	if [ "$i" -gt 600 ]; then
		echo "!! timeout after ${i}s, see $LOG" >&2
		kill_vm
		exit 1
	fi
	sleep 1
done

sleep 2
kill_vm
wait "$nbpid" 2>/dev/null

[ -f "$RESULTS" ] && cat "$RESULTS"
grep -q '^exit=0$' "$RESULTS"

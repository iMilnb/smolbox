#!/bin/sh
# $Id$
#
# smolbox test runner — run on NetBSD from the smolbox source root:
#
#	tests/run.sh [tool ...]
#
# Builds a symlink farm (argv[0] dispatch) in a temp dir, runs each
# tests/t_<tool>.sh with it, and exits non-zero if any file fails.

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SMOULBOX=$ROOT/smolbox
export SMOULBOX

if [ ! -x "$SMOULBOX" ]; then
	echo "smolbox not built — run make first" >&2
	exit 1
fi

FARM=$(mktemp -d /tmp/sbfarm.XXXXXX) || exit 1
for tool in cp init ln ls mount rm sh sysctl; do
	ln -s "$SMOULBOX" "$FARM/$tool" || exit 1
done
export FARM

rc=0

# run_one <test-file> — run a test file with a wall-clock cap so a
# single hung tool cannot block the whole suite. NetBSD base has no
# timeout(1), so use a background poller.
run_one() {
	_tf=$1
	sh "$_tf" &
	_pid=$!
	_i=0
	while kill -0 "$_pid" 2>/dev/null; do
		_i=$((_i + 1))
		if [ "$_i" -ge 60 ]; then
			printf '# TIMEOUT after 60s: %s\n' "$_tf"
			kill -9 "$_pid" 2>/dev/null
			break
		fi
		sleep 1
	done
	wait "$_pid" 2>/dev/null
}

for tf in "$ROOT"/tests/t_*.sh; do
	name=${tf##*/t_}
	name=${name%.sh}
	if [ $# -gt 0 ]; then
		found=
		for want in "$@"; do
			[ "$want" = "$name" ] && found=y
		done
		[ -z "$found" ] && continue
	fi
	echo "# --- $name"
	run_one "$tf" || rc=1
done

rm -rf "$FARM"
exit $rc

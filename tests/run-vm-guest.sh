#!/bin/sh
# $Id$
#
# Guest-side build+test driver for the smolbox dev microVM.
#
# This runs as the image CMD, reached through the 9P share at /mnt.
# Edit this file to iterate on the build/test flow — no image rebuild
# is needed, since the baked CMD only invokes this script by path.
#
# The 9P share reports no free space, so the tree is copied onto the
# local root filesystem before building; only the results are written
# back through the share (test-results.txt, then test-done to signal
# the host-side runner tests/run-vm.sh).
#
# A .fast marker in the shared dir builds with -O0 for a quicker
# edit/test loop; leave it out for a faithful -O2 build.

SRC=/mnt
BUILD=/root/smolbox-build
OUT=/mnt/test-results.txt
DONE=/mnt/test-done

{
	echo "== uname =="; uname -a
	echo "== df =="; df -h
	echo "== build =="
	rm -rf "$BUILD"
	mkdir -p "$BUILD"
	cp -R "$SRC"/. "$BUILD"/ || { echo "copy failed"; exit 1; }
	cd "$BUILD" || exit 1
	# cp -R gives every copied file the same mtime, which makes make
	# treat any copied .o as up to date. Drop them so we always build
	# from the current sources.
	make clean >/dev/null 2>&1
	if [ -f "$SRC/.fast" ]; then
		make 'CFLAGS=-O0 -fPIE -std=gnu11 -Werror' && sh tests/run.sh
	else
		make && sh tests/run.sh
	fi
} > "$OUT" 2>&1
echo "exit=$?" >> "$OUT"
sync
touch "$DONE"

#!/bin/sh
# $Id$
# mount — needs root and a vnd node; skips itself otherwise.

. "${0%/*}/helpers.sh"

if [ "$(id -u)" -ne 0 ]; then
	echo "# t_mount: skipped (not root)"
	exit 0
fi

sb_new
M=$FARM/mount

t "list mounts" 0 "$M"
t_grep "root filesystem listed" ' / ' "$M"

# ffs round-trip on a file-backed vnd
if [ ! -e /dev/vnd0d ]; then
	(cd /dev && sh MAKEDEV vnd0) >/dev/null 2>&1
fi
if [ ! -e /dev/vnd0d ]; then
	echo "# t_mount: no vnd0 node, skipping ffs tests"
	done_tests
	exit $?
fi

raw=/dev/vnd0rd
[ -e "$raw" ] || raw=/dev/r/vnd0d

dd if=/dev/zero of=disk bs=1m count=8 >/dev/null 2>&1
if ! vnconfig vnd0 disk >/dev/null 2>&1; then
	echo "# t_mount: vnconfig failed, skipping ffs tests"
	done_tests
	exit $?
fi
if ! newfs "$raw" >/dev/null 2>&1; then
	echo "# t_mount: newfs failed, skipping ffs tests"
	vnconfig -u vnd0 >/dev/null 2>&1
	done_tests
	exit $?
fi
mkdir mnt2
t "mount ffs" 0 "$M" /dev/vnd0d mnt2
t "mounted fs writable" 0 touch mnt2/x
t_grep "mount listed" 'mnt2' "$M"
t "remount ro" 0 "$M" -u -o ro /dev/vnd0d mnt2
t "ro fs rejects write" 1 touch mnt2/y
/sbin/umount mnt2
vnconfig -u vnd0

done_tests

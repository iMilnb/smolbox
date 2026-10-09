#!/bin/sh
# $Id$
# ln

. "${0%/*}/helpers.sh"
sb_new
LN=$FARM/ln

echo x > f
t "hard link" 0 "$LN" f h
t "hard link shares inode" 0 sh -c \
	'[ "$(stat -f %i f)" = "$(stat -f %i h)" ]'

t "symbolic link" 0 "$LN" -s target slink
t "symlink target" 0 sh -c '[ "$(readlink slink)" = target ]'

t "duplicate hard link fails" 1 "$LN" f h
t "-f replaces existing" 0 "$LN" -f f h

mkdir d
t "link into directory" 0 "$LN" f d
t "file present in dir" 0 test -e d/f

echo y > f2
mkdir d2
t "multi-source into dir" 0 "$LN" f f2 d2
t "second file in dir" 0 test -e d2/f2
t "multi-source without dir fails" 1 "$LN" f f2

done_tests

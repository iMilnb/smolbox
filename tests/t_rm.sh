#!/bin/sh
# $Id$
# rm

. "${0%/*}/helpers.sh"
sb_new
RM=$FARM/rm

touch f
t "remove file" 0 "$RM" f
t "file gone" 1 test -e f

mkdir -p d/s
touch d/s/x
t "-r removes tree" 0 "$RM" -r d
t "tree gone" 1 test -e d

t "-f on missing is ok" 0 "$RM" -f nosuchfile

mkdir dd
t "directory without -r fails" 1 "$RM" dd

t ". is refused" 1 "$RM" .
t ".. is refused" 1 "$RM" ..

mkdir empty
t "-d removes empty dir" 0 "$RM" -d empty
mkdir ne
touch ne/f
t "-d refuses non-empty" 1 "$RM" -d ne

# -r must unlink a symlinked dir, not recurse into it
mkdir real
touch real/x
ln -s real ld
t "-r on symlinked dir" 0 "$RM" -r ld
t "symlink removed" 1 test -e ld
t "target contents intact" 0 test -e real/x

done_tests

#!/bin/sh
# $Id$
# dispatcher (argv[0] / argv[1] selection)

. "${0%/*}/helpers.sh"
sb_new
SB=$SMOULBOX

t "no args exits 1" 1 "$SB"
t "unknown command exits 1" 1 "$SB" nosuchtool
t_out "dispatch via argv[1]" "hi" "$SB" sh -c 'echo hi'

# symlink dispatch
ln -s "$SMOULBOX" ./mysmol 2>/dev/null
t_out "symlink dispatch" "hi" ./mysmol sh -c 'echo hi'

# login-shell convention: argv[0] basename prefixed with '-'
ln -s "$SMOULBOX" ./-sh 2>/dev/null
t_out "dash-stripped dispatch" "dash" ./-sh -c 'echo dash'

done_tests

#!/bin/sh
# $Id$
# ls

. "${0%/*}/helpers.sh"
sb_new
LS=$FARM/ls

touch b c
mkdir a
exp=$(printf 'a\nb\nc')
t_out "sorted single column" "$exp" "$LS"

t_grep "-a shows dot entries" '^.$' "$LS" -a
t_grep "-d lists dir itself" '^d$' sh -c "mkdir d; '$LS' -d d"

t_grep "-l shows mode" '^-rw' "$LS" -l
t_grep "-l shows name" ' b$' "$LS" -l

mkdir s
touch s/ins
t_grep "-R shows subdir contents" 'ins' "$LS" -R

t "missing directory fails" 1 "$LS" /nonexistent-does-not-exist

# -1: exactly one entry per line (counted in a controlled dir)
mkdir cnt
touch cnt/x cnt/y
mkdir cnt/z
n=$("$LS" -1 cnt | wc -l)
[ "$n" -eq 3 ] && pass "-1 one per line" || fail "-1 one per line" "got $n lines"

# long names must not be truncated
touch "averyveryverylongfilenamethatshouldnotbetru_0123456789"
t_grep "long names intact" 'shouldnotbetru_0123456789$' "$LS"

done_tests

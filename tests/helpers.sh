# $Id$
#
# smolbox test helpers — sourced by t_*.sh, driven by the host /bin/sh.
#
# Environment (set by tests/run.sh):
#	SMOULBOX	path to the built smolbox binary
#	FARM		directory with one symlink per tool

n_total=0
n_fail=0
sb=

sb_new() {
	sb=$(mktemp -d /tmp/sbtest.XXXXXX) || exit 1
	cd "$sb" || exit 1
}

pass() {
	n_total=$((n_total + 1))
	printf 'ok - %s\n' "$1"
}

fail() {
	n_total=$((n_total + 1))
	n_fail=$((n_fail + 1))
	printf 'NOT OK - %s\n' "$1"
	[ -n "$2" ] && printf '#   %s\n' "$2"
}

# t <desc> <expected-status> <cmd...> — compare exit status only.
t() {
	_d=$1; _e=$2; shift 2
	( "$@" ) >/dev/null 2>&1
	_st=$?
	if [ "$_st" -eq "$_e" ]; then
		pass "$_d"
	else
		fail "$_d" "status $_st, expected $_e"
	fi
}

# t_out <desc> <expected-stdout> <cmd...> — compare stdout exactly.
t_out() {
	_d=$1; _e=$2; shift 2
	_g=$( ( "$@" ) 2>/dev/null )
	if [ "$_g" = "$_e" ]; then
		pass "$_d"
	else
		fail "$_d" "expected [$_e] got [$_g]"
	fi
}

# t_in <desc> <expected-status> <stdin-with-%b-escapes> <cmd...>
t_in() {
	_d=$1; _e=$2; _i=$3; shift 3
	printf '%b' "$_i" | ( "$@" ) >/dev/null 2>&1
	_st=$?
	if [ "$_st" -eq "$_e" ]; then
		pass "$_d"
	else
		fail "$_d" "status $_st, expected $_e"
	fi
}

# t_grep <desc> <ERE pattern> <cmd...> — stdout must match pattern.
t_grep() {
	_d=$1; _p=$2; shift 2
	if ( "$@" ) 2>/dev/null | grep -Eq "$_p"; then
		pass "$_d"
	else
		fail "$_d" "output lacks /$_p/"
	fi
}

# t_file <desc> <path> <expected-content> — file content check.
t_file() {
	_d=$1; _f=$2; _e=$3
	if [ -f "$_f" ] && [ "$(cat "$_f")" = "$_e" ]; then
		pass "$_d"
	else
		fail "$_d" "content of $_f"
	fi
}

done_tests() {
	if [ -n "$sb" ]; then
		cd /
		rm -rf "$sb"
	fi
	printf '# %s: %d tests, %d failures\n' "${0##*/}" "$n_total" "$n_fail"
	[ "$n_fail" -eq 0 ]
}

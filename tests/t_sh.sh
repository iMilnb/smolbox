#!/bin/sh
# $Id$
# sh — run scripts under smolbox sh; the runner itself is the host /bin/sh.

. "${0%/*}/helpers.sh"
sb_new
S=$FARM/sh

# sc <desc> <expected-stdout> — script body on stdin
sc() {
	_d=$1; _e=$2
	printf '%s\n' "$_e" > exp
	cat > case.sh
	t_out "$_d" "$(cat exp)" "$S" case.sh
}

# --- invocation modes
t_out "-c command string" "hi" "$S" -c 'echo hi'
t "exit status from -c" 3 "$S" -c 'exit 3'
printf 'echo scripted\n' > s1.sh
t_out "script file" "scripted" "$S" s1.sh
printf 'echo "$1-$#"\n' > s2.sh
t_out "script positional" "a-2" "$S" s2.sh a b
printf 'touch marker-should-not-exist\n' > s3.sh
t "-n parses without executing" 0 "$S" -n s3.sh
t "no-exec left no marker" 1 test -e marker-should-not-exist

# --- echo / quoting
t_out "double quotes kept as one arg" "hello world" "$S" -c 'echo "hello world"'
t_out "single quotes literal" '$x' "$S" -c 'echo '"'"'$x'"'"''
t_out "escaped space" "a b" "$S" -c 'echo a\ b'

# --- variables
t_out "var expand" "5" "$S" -c 'x=5; echo $x'
t_out "brace expand" "5y" "$S" -c 'x=5; echo ${x}y'
t_out "positional 1" "one" "$S" -c 'set -- one two; echo $1'
t_out "status var" "1" "$S" -c 'false; echo $?'
t_out "export visible to child" "bar" "$S" -c 'export FOO=bar; /bin/sh -c "echo \$FOO"'
t_out "unset" "[]" "$S" -c 'x=1; unset x; echo "[$x]"'
t_out "set and positional" "2 b" "$S" -c 'set -- a b; echo $# $2'
t_out "at-var" "a b" "$S" -c 'set -- a b; echo "$@"'

# --- control flow
t_out "and list" "yes" "$S" -c 'true && echo yes'
t_out "or list" "yes" "$S" -c 'false || echo yes'
t "and short-circuits" 1 "$S" -c 'false && echo no'
t_out "if then else" "e" "$S" -c 'if false; then echo t; else echo e; fi'
sc "if multi-line" "in" <<'EOS'
if [ 1 -eq 1 ]; then
	echo in
fi
EOS
sc "for loop" "1
2
3" <<'EOS'
for i in 1 2 3; do
	echo "$i"
done
EOS
sc "while loop" "3" <<'EOS'
i=0
while [ "$i" -lt 3 ]; do
	i=$((i + 1))
done
echo "$i"
EOS
sc "empty for list runs zero times" "none" <<'EOS'
n=none
for x in; do
	n=ran
done
echo "$n"
EOS

# --- test builtin
t_out "test -n" "t" "$S" -c 'test -n x && echo t'
t_out "brackets eq" "t" "$S" -c '[ 1 -eq 1 ] && echo t'
t_out "test string ne" "t" "$S" -c '[ abc != xyz ] && echo t'
t "test = with missing operand" 2 "$S" -c 'test = x'

# --- pipelines and redirections
t_out "pipe" "hi" "$S" -c 'echo hi | cat'
t_out "three-stage pipe" "c" "$S" -c 'echo c | cat | cat'
t_out "redirect out" "to" "$S" -c 'echo to > f; cat f'
t_out "redirect append" "12" "$S" -c 'echo 1 > f; echo 2 >> f; tr -d "\n" < f'
t_out "redirect stdin" "in" "$S" -c 'echo in > src; cat < src'
t_out "shell stdout not hijacked" "still-here" "$S" -c 'echo x > /dev/null; echo still-here'

# --- globbing
touch ga1 ga2 gb
t_out "star glob" "ga1 ga2" "$S" -c 'echo ga*'
t_out "question glob" "ga1 ga2" "$S" -c 'echo ga?'
t_out "bracket glob" "ga1" "$S" -c 'echo g[ab]1'

# --- builtins
t_out "pwd builtin" "$PWD" "$S" -c 'pwd'
t_out "cd builtin" "/tmp" "$S" -c 'cd /tmp; pwd'
t_out "colon builtin" "" "$S" -c ':'
t_grep "type builtin" "echo" "$S" -c 'type echo'
t_grep "printenv builtin" "^PATH=" "$S" -c 'printenv'

# --- robustness regressions
t_in "empty lines do not crash" 0 "\n\n\n" "$S"
t_in "trailing backslash line" 0 'echo ok\\\n' "$S"
t "script with syntax error nonzero" 2 "$S" -c 'if true'

done_tests

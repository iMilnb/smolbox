#!/bin/sh
# $Id$
# cp

. "${0%/*}/helpers.sh"
sb_new
CP=$FARM/cp

echo hello > a
t "copy file" 0 "$CP" a b
t_file "content preserved" b hello
t "missing source fails" 1 "$CP" nosuchfile b2

mkdir d
echo x > d/f
mkdir d/s
echo y > d/s/g
t "recursive copy" 0 "$CP" -r d d2
t_file "recursive content" d2/f x
t_file "recursive subdir" d2/s/g y

t "copy into directory" 0 "$CP" a d
t_file "into-dir basename" d/a hello

echo ro > tgt
chmod 444 tgt
t "-f overwrites read-only" 0 "$CP" -f a tgt
t_file "overwrite content" tgt hello

touch srcmode
chmod 750 srcmode
t "-p preserves mode" 0 "$CP" -p srcmode pm
t_out "mode is 750" "750" stat -f "%OLp" pm

# same-file: must not truncate the source (POSIX: error)
echo keep > same
t "same file does not truncate" 0 sh -c \
	"'$CP' same same; [ \$(cat same) = keep ]"
t_file "same file content kept" same keep

# source symlink is followed
echo linkme > realf
ln -s realf lnk
t "follow source symlink" 0 "$CP" lnk outl
t_file "symlink content copied" outl linkme

# over-long path must fail, not silently truncate
long=$(printf 'x%.0s' 1 2 3 4 5 6 7 8 9)
i=0
while [ $i -lt 5 ]; do long="$long$long"; i=$((i + 1)); done
t "over-long target fails" 1 "$CP" a "$long"

done_tests

# smolbox - a BSD equivalent of BusyBox

PROG=	smolbox
MAN=	
SRCS=	smolbox.c \
	sbin/init/init.c \
	bin/cp/cp.c \
	bin/ls/ls.c \
	bin/ln/ln.c \
	bin/rm/rm.c \
	sbin/mount/mount.c \
	sbin/sysctl/sysctl.c \
	bin/sh/alias.c \
	bin/sh/arith_yacc.c \
	bin/sh/arith_yylex.c \
	bin/sh/builtins.c \
	bin/sh/cd.c \
	bin/sh/error.c \
	bin/sh/eval.c \
	bin/sh/exec.c \
	bin/sh/expand.c \
	bin/sh/histedit.c \
	bin/sh/init.c \
	bin/sh/input.c \
	bin/sh/jobs.c \
	bin/sh/mail.c \
	bin/sh/main.c \
	bin/sh/memalloc.c \
	bin/sh/miscbltin.c \
	bin/sh/mystring.c \
	bin/sh/nodes.c \
	bin/sh/options.c \
	bin/sh/output.c \
	bin/sh/parser.c \
	bin/sh/redir.c \
	bin/sh/show.c \
	bin/sh/signames.c \
	bin/sh/syntax.c \
	bin/sh/system.c \
	bin/sh/trap.c \
	bin/sh/var.c \
	bin/sh/bltin/printf.c \
	bin/sh/bltin/test.c \
	bin/sh/bltin/times.c

CFLAGS+=	-O2 -fPIE -std=gnu11 -Werror
CFLAGS+=	-I bin/sh -I bin/sh/bltin -DHAVE_CONFIG_H -DSHELL
LDFLAGS+=	-static -pie
LDADD+=	-lutil

cleanobjdir=	no

.PHONY:	clean test test-vm

clean:
	find . -name '*.o' -type f -delete
	rm -f smolbox

test:
	sh tests/run.sh

test-vm:
	sh tests/run-vm.sh

.include <bsd.prog.mk>

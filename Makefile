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
	bin/sh/sh.c \
	bin/sh/lexer.c \
	bin/sh/parser.c \
	bin/sh/exec.c \
	bin/sh/builtin.c \
	bin/sh/var.c \
	bin/sh/redirect.c \
	bin/sh/glob.c

CFLAGS+=	-O2 -fPIE -std=gnu11 -Werror
LDFLAGS+=	-static -pie
LDADD+=	-lutil

cleanobjdir=	no

.PHONY:	clean

clean:
	find . -name '*.o' -type f -delete
	rm -f smolbox

.include <bsd.prog.mk>

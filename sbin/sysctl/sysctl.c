/*-
 * Copyright (c) 2026 Emile 'iMil' Heitor & Qwen3.6 + Crush.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/sysctl.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* NetBSD extension: get MIB info by name. */
extern int sysctlgetmibinfo(const char *, int *, u_int *,
    char *, size_t *, struct sysctlnode **, int);

static int	aflag, nflag, rflag, wflag, xflag;
static int	errs;

#define MAXSYSCTLSZ 65536

/*
 * Top-level sysctl categories.
 */
static const char * const top_categories[] = {
	"kern", "vm", "vfs", "net", "debug", "ddb", "hw", "machdep",
	"user", "security", "kmem", "compat", "userconfig", NULL,
};

static void	 usage(void) __attribute__((__noreturn__));
static int	 resolve_node(const char *, int *, size_t *,
		    struct sysctlnode **);
static void	 walk_tree(const char *);
static void	 get_value(const char *);
static void	 set_value(const char *, const char *);
static void	 print_value(const char *, int *, size_t, u_int);
static void	 hex_dump(const uint8_t *, size_t);
static int	 parse_number(const char *, void *, size_t, u_int);

/*
 * Main entry point.
 */
int
main_sysctl(int argc, char *argv[])
{
	int ch;

	while ((ch = getopt(argc, argv, "abdnqwrx")) != -1)
		switch (ch) {
		case 'a':
			aflag = 1;
			break;
		case 'b':
			/* Raw binary output (same as -r). */
			rflag = 1;
			break;
		case 'd':
			/* Descriptions (reserved for future). */
			break;
		case 'n':
			nflag = 1;
			break;
		case 'q':
			/* Quiet: suppress errors (reserved for future). */
			break;
		case 'w':
			wflag = 1;
			break;
		case 'r':
			rflag = 1;
			break;
		case 'x':
			xflag = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}

	argc -= optind;
	argv += optind;

	if (aflag) {
		/* List all sysctl variables. */
		const char * const *cat;

		for (cat = top_categories; *cat != NULL; cat++)
			walk_tree(*cat);
		return errs ? 1 : 0;
	}

	if (argc == 0)
		usage();

	while (argc-- > 0) {
		char *arg, *eq;

		arg = *argv++;
		eq = strchr(arg, '=');

		if (eq != NULL) {
			/* Setting a value: name=value */
			*eq = '\0';
			set_value(arg, eq + 1);
		} else {
			if (wflag) {
				(void)fprintf(stderr,
				    "sysctl: must specify value with -w\n");
				errs++;
				continue;
			}
			get_value(arg);
		}
	}

	return errs ? 1 : 0;
}

/*
 * Resolve a sysctl name to its MIB and node via sysctlgetmibinfo(3).
 * Any of miblenp / nodep may be NULL.  Returns 0 on success, -1 on error.
 */
static int
resolve_node(const char *name, int *mib, size_t *miblenp,
    struct sysctlnode **nodep)
{
	struct sysctlnode *node = NULL;
	char sname[256];
	size_t sz = sizeof(sname);
	u_int miblen = CTL_MAXNAME;

	if (sysctlgetmibinfo(name, mib, &miblen, sname, &sz, &node,
	    SYSCTL_VERSION) != 0 || node == NULL)
		return -1;
	if (miblenp != NULL)
		*miblenp = miblen;
	if (nodep != NULL)
		*nodep = node;
	return 0;
}

/*
 * Walk a sysctl subtree recursively.
 * Children are queried by numeric index (prefix.0, prefix.1, ...).
 */
static void
walk_tree(const char *prefix)
{
	struct sysctlnode *node;
	size_t sz;
	char sname[256];
	int name[CTL_MAXNAME];
	u_int miblen;
	u_int i;

	sz = sizeof(sname);
	miblen = CTL_MAXNAME;
	node = NULL;
	if (sysctlgetmibinfo(prefix, name, &miblen, sname, &sz, &node,
	    SYSCTL_VERSION) != 0 || node == NULL)
		return;

	if (SYSCTL_TYPE(node->sysctl_flags) != CTLTYPE_NODE) {
		/* Leaf node: print value. */
		get_value(prefix);
		return;
	}

	/* Internal node: iterate children by numeric index. */
	for (i = 0; i < node->sysctl_clen; i++) {
		char child[512];
		struct sysctlnode *cnode;
		size_t csz;
		char csname[256];
		u_int cmiblen;

		(void)snprintf(child, sizeof(child), "%s.%u", prefix, i);
		csz = sizeof(csname);
		cmiblen = CTL_MAXNAME;
		cnode = NULL;
		if (sysctlgetmibinfo(child, name, &cmiblen, csname, &csz,
		    &cnode, SYSCTL_VERSION) == 0 && cnode != NULL)
			walk_tree(csname);
	}
}

/*
 * Get and print a sysctl value by name.
 */
static void
get_value(const char *name)
{
	int mib[CTL_MAXNAME];
	size_t miblen;
	size_t sz;

	miblen = CTL_MAXNAME;
	if (sysctlbyname(name, NULL, &sz, NULL, 0) == -1) {
		/* Try numeric MIB. */
		if (sysctlnametomib(name, mib, &miblen) == -1) {
			(void)fprintf(stderr,
			    "sysctl: unknown oid '%s'\n", name);
			errs++;
			return;
		}
		print_value(name, mib, miblen, 0);
		return;
	}

	/* Allocate buffer and read value. */
	{
		void *buf;
		u_int type;
		int rc;

		if (sz == 0)
			sz = 1;
		if (sz > MAXSYSCTLSZ) {
			if (!aflag) {
				(void)fprintf(stderr,
				    "sysctl: %s: value too large\n", name);
				errs++;
			}
			return;
		}
		buf = malloc(sz);
		if (buf == NULL) {
			if (!aflag) {
				warn("sysctl: %s", name);
				errs++;
			}
			return;
		}

		rc = sysctlbyname(name, buf, &sz, NULL, 0);
		if (rc == -1 && errno == ENOMEM) {
			/* Value grew since the size probe; retry once. */
			void *nbuf = realloc(buf, sz);
			if (nbuf != NULL) {
				buf = nbuf;
				rc = sysctlbyname(name, buf, &sz, NULL, 0);
			}
		}
		if (rc == -1) {
			if (!aflag) {
				warn("sysctl: %s", name);
				errs++;
			}
			free(buf);
			return;
		}

		/* Determine type for formatting. */
		type = 0;
		{
			struct sysctlnode *node;
			if (resolve_node(name, mib, NULL, &node) == 0)
				type = SYSCTL_TYPE(node->sysctl_flags);
		}

		if (!nflag)
			(void)printf("%s = ", name);

		if (rflag) {
			(void)fwrite(buf, 1, sz, stdout);
		} else if (xflag) {
			hex_dump(buf, sz);
		} else {
			switch (type) {
			case CTLTYPE_INT:
				if (sz >= sizeof(int))
					(void)printf("%d\n", *(int *)buf);
				else
					hex_dump(buf, sz);
				break;
			case CTLTYPE_QUAD:
				if (sz >= sizeof(int64_t))
					(void)printf("%" PRId64 "\n",
					    *(int64_t *)buf);
				else
					hex_dump(buf, sz);
				break;
			case CTLTYPE_STRING:
				(void)printf("%s\n", (char *)buf);
				break;
			case CTLTYPE_STRUCT:
				hex_dump(buf, sz);
				break;
			default:
				/* Fallback: try string, then hex. */
				if (sz > 0 && ((uint8_t *)buf)[sz - 1] == '\0')
					(void)printf("%s\n", (char *)buf);
				else
					hex_dump(buf, sz);
				break;
			}
		}

		free(buf);
	}
}

/*
 * Set a sysctl value by name.
 */
static void
set_value(const char *name, const char *value)
{
	int mib[CTL_MAXNAME];
	size_t miblen, sz;
	u_int type;
	struct sysctlnode *node;
	void *obuf, *nbuf;
	int rc;

	if (resolve_node(name, mib, &miblen, &node) == -1) {
		(void)fprintf(stderr,
		    "sysctl: unknown oid '%s'\n", name);
		errs++;
		return;
	}

	type = SYSCTL_TYPE(node->sysctl_flags);
	sz = node->sysctl_size;

	/* Read old value. */
	obuf = malloc(sz > 0 ? sz : 1024);
	if (obuf == NULL) {
		warn("sysctl: %s", name);
		errs++;
		return;
	}

	sz = node->sysctl_size > 0 ? node->sysctl_size : 1024;
	if (sysctl(mib, miblen, obuf, &sz, NULL, 0) == -1) {
		warn("sysctl: %s", name);
		free(obuf);
		errs++;
		return;
	}

	/* Parse new value. */
	nbuf = malloc(sz);
	if (nbuf == NULL) {
		warn("sysctl: %s", name);
		free(obuf);
		errs++;
		return;
	}

	if (parse_number(value, nbuf, sz, type) == -1) {
		free(obuf);
		free(nbuf);
		return;
	}

	/* Write new value. */
	{
		size_t nsz;

		switch (type) {
		case CTLTYPE_INT:
		case CTLTYPE_QUAD:
			nsz = node->sysctl_size;
			break;
		case CTLTYPE_STRING:
			nsz = strlen(value) + 1;
			if (nsz > node->sysctl_size &&
			    node->sysctl_size != 0) {
				(void)fprintf(stderr,
				    "sysctl: string too long for %s\n", name);
				errs++;
				free(obuf);
				free(nbuf);
				return;
			}
			(void)memcpy(nbuf, value, nsz);
			break;
		default:
			nsz = sz;
			break;
		}

		rc = sysctl(mib, miblen, NULL, NULL, nbuf, nsz);
	}

	if (rc == -1) {
		warn("sysctl: %s", name);
		errs++;
	} else {
		if (!nflag)
			(void)printf("%s: ", name);
		if (type == CTLTYPE_STRING) {
			(void)printf("%s -> %s\n", (char *)obuf, value);
		} else if (type == CTLTYPE_INT) {
			(void)printf("%d -> %d\n",
			    *(int *)obuf, *(int *)nbuf);
		} else if (type == CTLTYPE_QUAD) {
			(void)printf("%" PRId64 " -> %" PRId64 "\n",
			    *(int64_t *)obuf, *(int64_t *)nbuf);
		} else {
			(void)printf("(changed)\n");
		}
	}

	free(obuf);
	free(nbuf);
}

/*
 * Print a sysctl value by MIB.
 */
static void
print_value(const char *name, int *mib, size_t miblen, u_int type)
{
	struct sysctlnode node;
	size_t sz, nsz;
	void *buf;
	char nbuf[512];
	int rc;

	/* Get node info. */
	nsz = sizeof(node);
	rc = sysctl(mib, miblen, &node, &nsz, NULL, 0);
	if (rc == -1)
		return;

	if (type == 0)
		type = SYSCTL_TYPE(node.sysctl_flags);

	/* Build name string. */
	if (name == NULL) {
		size_t nlen;

		nlen = sizeof(nbuf);
		if (sysctl(mib, miblen, nbuf, &nlen, NULL, 0) == -1) {
			/* Fallback: use numeric name. */
			u_int i;
			char numbuf[32];

			nbuf[0] = '\0';
			for (i = 0; i < miblen; i++) {
				if (i > 0)
					(void)strlcat(nbuf, ".",
					    sizeof(nbuf));
				(void)snprintf(numbuf, sizeof(numbuf),
				    "%u", mib[i]);
				(void)strlcat(nbuf, numbuf, sizeof(nbuf));
			}
		}
		name = nbuf;
	}

	/* Read value. */
	sz = node.sysctl_size;
	if (sz == 0) {
		sysctl(mib, miblen, NULL, &sz, NULL, 0);
	}
	if (sz == 0 || sz > MAXSYSCTLSZ)
		return;

	buf = malloc(sz);
	if (buf == NULL)
		return;

	if (sysctl(mib, miblen, buf, &sz, NULL, 0) == -1) {
		free(buf);
		return;
	}

	if (!nflag)
		(void)printf("%s = ", name);

	if (rflag) {
		(void)fwrite(buf, 1, sz, stdout);
	} else if (xflag) {
		hex_dump(buf, sz);
	} else {
		switch (type) {
		case CTLTYPE_INT:
			if (sz >= sizeof(int))
				(void)printf("%d\n", *(int *)buf);
			else
				hex_dump(buf, sz);
			break;
		case CTLTYPE_QUAD:
			if (sz >= sizeof(int64_t))
				(void)printf("%" PRId64 "\n", *(int64_t *)buf);
			else
				hex_dump(buf, sz);
			break;
		case CTLTYPE_STRING:
			(void)printf("%s\n", (char *)buf);
			break;
		case CTLTYPE_STRUCT:
			hex_dump(buf, sz);
			break;
		default:
			if (sz > 0 && ((uint8_t *)buf)[sz - 1] == '\0')
				(void)printf("%s\n", (char *)buf);
			else
				hex_dump(buf, sz);
			break;
		}
	}

	free(buf);
}

/*
 * Parse a string value into the appropriate type.
 */
static int
parse_number(const char *value, void *buf, size_t sz, u_int type)
{
	char *endp;
	int64_t num;

	switch (type) {
	case CTLTYPE_INT:
		errno = 0;
		num = strtoll(value, &endp, 0);
		if (endp == value || *endp != '\0' || errno != 0) {
			(void)fprintf(stderr,
			    "sysctl: '%s' is not a valid integer\n", value);
			errs++;
			return -1;
		}
		if (num < INT_MIN || num > INT_MAX) {
			(void)fprintf(stderr,
			    "sysctl: '%s' out of range\n", value);
			errs++;
			return -1;
		}
		*(int *)buf = (int)num;
		break;
	case CTLTYPE_QUAD:
		errno = 0;
		num = strtoll(value, &endp, 0);
		if (endp == value || *endp != '\0' || errno != 0) {
			(void)fprintf(stderr,
			    "sysctl: '%s' is not a valid number\n", value);
			errs++;
			return -1;
		}
		*(int64_t *)buf = num;
		break;
	case CTLTYPE_STRING:
		if (strlen(value) >= sz) {
			(void)fprintf(stderr,
			    "sysctl: string too long\n");
			errs++;
			return -1;
		}
		(void)memcpy(buf, value, strlen(value) + 1);
		break;
	default:
		(void)fprintf(stderr,
		    "sysctl: cannot set type %u\n", type);
		errs++;
		return -1;
	}

	return 0;
}

/*
 * Hex dump a buffer.
 */
static void
hex_dump(const uint8_t *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (i > 0)
			(void)putchar(' ');
		(void)printf("%02x", buf[i]);
	}
	(void)putchar('\n');
}

/*
 * Print usage and exit.
 */
static void
usage(void)
{

	(void)fprintf(stderr,
	    "usage: sysctl [-bdnqrwx] [name[=value] ...]\n"
	    "       sysctl [-bnrx] -a\n");
	exit(1);
}

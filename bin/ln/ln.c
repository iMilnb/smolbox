/*-
 * Copyright (c) 2026 Emile 'iMil' Heitor & Qwen3.8 Flash Next + maki.
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
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int	f_flag;	/* -f: force (unlink existing) */
static int	h_flag;	/* -h: don't follow symlink target */
static int	s_flag;	/* -s: symbolic link */
static int	v_flag;	/* -v: verbose */

static int	 linkit(const char *, const char *, int);
static void	 usage(void) __attribute__((__noreturn__));

/*
 * Main entry point.
 */
int
main_ln(int argc, char *argv[])
{
	struct stat	 sb;
	const char	*targetdir;
	int		 ch, exitval;

	while ((ch = getopt(argc, argv, "fhsv")) != -1)
		switch (ch) {
		case 'f':
			f_flag = 1;
			break;
		case 'h':
			h_flag = 1;
			break;
		case 'n':
			h_flag = 1;
			break;
		case 's':
			s_flag = 1;
			break;
		case 'v':
			v_flag = 1;
			break;
		default:
			usage();
		}

	argv += optind;
	argc -= optind;

	if (argc == 0)
		usage();

	/* ln source */
	if (argc == 1)
		exit(linkit(argv[0], ".", 1));

	/* ln source target */
	if (argc == 2)
		exit(linkit(argv[0], argv[1], 0));

	/* ln source1 ... target_dir */
	targetdir = argv[argc - 1];
	if (h_flag && lstat(targetdir, &sb) == 0 && S_ISLNK(sb.st_mode)) {
		errno = ENOTDIR;
		err(1, "%s", targetdir);
	}
	if (stat(targetdir, &sb))
		err(1, "%s", targetdir);
	if (!S_ISDIR(sb.st_mode))
		usage();

	for (exitval = 0; argc > 1; argv++, argc--)
		exitval |= linkit(argv[0], targetdir, 1);

	exit(exitval);
}

/*
 * Create a link from source to target.
 */
static int
linkit(const char *source, const char *target, int isdir)
{
	struct stat	 sb;
	char		 path[PATH_MAX];
	const char	*p;

	/* For hard links, source must exist. */
	if (!s_flag && stat(source, &sb)) {
		warn("%s", source);
		return 1;
	}

	/* If target is a directory, create link inside it. */
	if (isdir ||
	    (lstat(target, &sb) == 0 && S_ISDIR(sb.st_mode)) ||
	    (!h_flag && stat(target, &sb) == 0 && S_ISDIR(sb.st_mode))) {
		if ((p = strrchr(source, '/')) != NULL)
			p++;
		else
			p = source;
		if (snprintf(path, sizeof(path), "%s/%s", target, p) >=
		    (ssize_t)sizeof(path)) {
			errno = ENAMETOOLONG;
			warn("%s", source);
			return 1;
		}
		target = path;
	}

	/* If target exists, unlink it with -f. */
	if (f_flag && lstat(target, &sb) == 0) {
		if (S_ISDIR(sb.st_mode)) {
			if (rmdir(target)) {
				warn("%s", target);
				return 1;
			}
		} else {
			if (unlink(target)) {
				warn("%s", target);
				return 1;
			}
		}
	}

	/* Create the link. */
	if (s_flag ? symlink(source, target) : link(source, target)) {
		warn("%s", target);
		return 1;
	}

	if (v_flag)
		(void)printf("%s -> %s\n", target, source);

	return 0;
}

/*
 * Usage message.
 */
static void
usage(void)
{

	(void)fprintf(stderr,
	    "usage: ln [-fhns] source [target]\n"
	    "       ln [-fhns] source ... directory\n");
	exit(1);
}

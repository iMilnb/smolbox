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

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int	d_flag;	/* -d: remove directories */
static int	f_flag;	/* -f: force */
static int	i_flag;	/* -i: interactive */
static int	r_flag;	/* -r: recursive */
static int	v_flag;	/* -v: verbose */

static int	 remove_dir(const char *);
static int	 remove_one(const char *);
static int	 prompt(const char *, const char *);
static void	 usage(void) __attribute__((__noreturn__));

/*
 * Main entry point.
 */
int
main_rm(int argc, char *argv[])
{
	int	 ch, exitval;

	while ((ch = getopt(argc, argv, "dfirv")) != -1)
		switch (ch) {
		case 'd':
			d_flag = 1;
			break;
		case 'f':
			f_flag = 1;
			i_flag = 0;
			break;
		case 'i':
			i_flag = 1;
			f_flag = 0;
			break;
		case 'r':
			r_flag = 1;
			break;
		case 'v':
			v_flag = 1;
			break;
		default:
			usage();
		}

	argv += optind;
	argc -= optind;

	if (argc < 1)
		usage();

	for (exitval = 0; *argv; argv++)
		exitval |= remove_one(*argv);

	exit(exitval);
}

/*
 * Remove a single file or directory.
 */
static int
remove_one(const char *path)
{
	struct stat sb;

	/* Skip "." and ".." (POSIX requirement). */
	{
		const char *base;

		base = strrchr(path, '/');
		if (base != NULL)
			base++;
		else
			base = path;
		if (strcmp(base, ".") == 0 || strcmp(base, "..") == 0) {
			if (!f_flag)
				warnx("\".\" and \"..\" may not be removed");
			return 1;
		}
	}

	if (lstat(path, &sb)) {
		if (f_flag && (errno == ENOENT || errno == ENAMETOOLONG ||
		    errno == ENOTDIR))
			return 0;
		warn("%s", path);
		return 1;
	}

	if (S_ISDIR(sb.st_mode)) {
		if (r_flag)
			return remove_dir(path);
		if (d_flag) {
			/* -d removes empty directories only. */
			if (i_flag && !prompt(path, "remove"))
				return 0;
			if (rmdir(path)) {
				warn("%s", path);
				return 1;
			}
			if (v_flag)
				(void)printf("%s\n", path);
			return 0;
		}
		warnx("%s: is a directory", path);
		return 1;
	}

	if (i_flag && !prompt(path, "remove"))
		return 0;

	if (unlink(path)) {
		warn("%s", path);
		return 1;
	}
	if (v_flag)
		(void)printf("%s\n", path);

	return 0;
}

/*
 * Remove a directory recursively.
 */
static int
remove_dir(const char *path)
{
	DIR		*dirp;
	struct dirent	*dp;
	int		 ret;
	char		 child[PATH_MAX];

	if (i_flag && !prompt(path, "remove"))
		return 0;

	dirp = opendir(path);
	if (dirp == NULL) {
		warn("%s", path);
		return 1;
	}

	ret = 0;
	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_name[0] == '.' &&
		    (dp->d_name[1] == '\0' ||
		    (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
			continue;

		if (snprintf(child, sizeof(child), "%s/%s",
		    path, dp->d_name) >= (ssize_t)sizeof(child)) {
			errno = ENAMETOOLONG;
			warn("%s", dp->d_name);
			ret = 1;
			continue;
		}

		ret |= remove_one(child);
	}

	(void)closedir(dirp);

	if (rmdir(path)) {
		warn("%s", path);
		ret = 1;
	} else if (v_flag) {
		(void)printf("%s\n", path);
	}

	return ret;
}

/*
 * Prompt user for confirmation.
 */
static int
prompt(const char *path, const char *action)
{
	int ch, first;

	(void)fprintf(stderr, "%s '%s'? ", action, path);
	(void)fflush(stderr);

	ch = first = getchar();
	while (ch != '\n' && ch != EOF)
		ch = getchar();

	return (first == 'y' || first == 'Y');
}

/*
 * Usage message.
 */
static void
usage(void)
{

	(void)fprintf(stderr,
	    "usage: rm [-dfirv] file ...\n");
	exit(1);
}

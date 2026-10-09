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
#include <sys/time.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	BUF_SIZE	(32 * 1024)

static int	f_flag;	/* -f: force overwrite */
static int	i_flag;	/* -i: interactive */
static int	p_flag;	/* -p: preserve mode/times */
static int	r_flag;	/* -r: recursive */
static int	v_flag;	/* -v: verbose */

static int	 copy_file(const char *, const char *);
static int	 copy_dir(const char *, const char *);
static int	 copy_one(const char *, const char *);
static void	 usage(void) __attribute__((__noreturn__));

/*
 * Main entry point.
 */
int
main_cp(int argc, char *argv[])
{
	struct stat	 sb;
	const char	*targetdir;
	int		 ch, exitval;

	while ((ch = getopt(argc, argv, "fiprv")) != -1)
		switch (ch) {
		case 'f':
			f_flag = 1;
			i_flag = 0;
			break;
		case 'i':
			i_flag = 1;
			f_flag = 0;
			break;
		case 'p':
			p_flag = 1;
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

	if (argc < 2)
		usage();

	/* cp src dst */
	if (argc == 2)
		exit(copy_one(argv[0], argv[1]));

	/* cp src1 ... srcN dir */
	targetdir = argv[argc - 1];
	if (stat(targetdir, &sb))
		err(1, "%s", targetdir);
	if (!S_ISDIR(sb.st_mode))
		usage();

	for (exitval = 0; argc > 1; argv++, argc--)
		exitval |= copy_one(argv[0], targetdir);

	exit(exitval);
}

/*
 * Copy one source to target (handling directory target).
 */
static int
copy_one(const char *source, const char *target)
{
	struct stat	 sb;
	char		 path[PATH_MAX];
	const char	*p;
	int		 is_dir;

	if (lstat(source, &sb)) {
		warn("%s", source);
		return 1;
	}

	is_dir = S_ISDIR(sb.st_mode);

	/* If target is a directory, create source inside it. */
	if (is_dir) {
		if (!r_flag) {
			warnx("%s is a directory (not copied)", source);
			return 1;
		}
	} else {
		if (lstat(target, &sb) == 0 && S_ISDIR(sb.st_mode)) {
			if ((p = strrchr(source, '/')) != NULL)
				p++;
			else
				p = source;
			if (snprintf(path, sizeof(path), "%s/%s",
			    target, p) >= (ssize_t)sizeof(path)) {
				errno = ENAMETOOLONG;
				warn("%s", source);
				return 1;
			}
			target = path;
		}
	}

	if (is_dir)
		return copy_dir(source, target);
	return copy_file(source, target);
}

/*
 * Copy a regular file.
 */
static int
copy_file(const char *source, const char *target)
{
	struct stat	 sb;
	struct timeval	 times[2];
	int		 fdin, fdout, ret;
	ssize_t		 n;
	char		*buf;

	/* Interactive prompt. */
	if (i_flag && stat(target, &sb) == 0) {
		(void)fprintf(stderr, "overwrite %s? ", target);
		(void)fflush(stderr);
		if (getchar() != 'y') {
			(void)fprintf(stderr, "not written\n");
			return 0;
		}
	}

	/* Force: unlink existing. */
	if (f_flag && stat(target, &sb) == 0) {
		if (unlink(target)) {
			warn("%s", target);
			return 1;
		}
	}

	fdin = open(source, O_RDONLY);
	if (fdin == -1) {
		warn("%s", source);
		return 1;
	}

	if (fstat(fdin, &sb)) {
		warn("%s", source);
		(void)close(fdin);
		return 1;
	}

	/* Refuse to copy a file onto itself (would truncate the source). */
	{
		struct stat tsb;
		if (stat(target, &tsb) == 0 &&
		    tsb.st_dev == sb.st_dev && tsb.st_ino == sb.st_ino) {
			(void)close(fdin);
			return 0;
		}
	}

	/* Preserve timestamps. */
	if (p_flag) {
		times[0].tv_sec = sb.st_atime;
		times[0].tv_usec = 0;
		times[1].tv_sec = sb.st_mtime;
		times[1].tv_usec = 0;
	}

	fdout = open(target, O_WRONLY | O_CREAT | O_TRUNC, sb.st_mode);
	if (fdout == -1) {
		warn("%s", target);
		(void)close(fdin);
		return 1;
	}

	buf = malloc(BUF_SIZE);
	if (buf == NULL) {
		warn(NULL);
		(void)close(fdin);
		(void)close(fdout);
		return 1;
	}

	ret = 0;
	while ((n = read(fdin, buf, BUF_SIZE)) > 0) {
		ssize_t w = 0;
		while (w < n) {
			ssize_t r = write(fdout, buf + w, (size_t)(n - w));
			if (r == -1) {
				warn("%s", target);
				ret = 1;
				break;
			}
			w += r;
		}
		if (ret)
			break;
	}

	if (n == -1) {
		warn("%s", source);
		ret = 1;
	}

	/* Preserve mode and timestamps. */
	if (p_flag && !ret) {
		(void)chmod(target, sb.st_mode);
		(void)utimes(target, times);
	}

	free(buf);
	(void)close(fdin);
	(void)close(fdout);

	if (v_flag)
		(void)printf("%s -> %s\n", source, target);

	return ret;
}

/*
 * Copy a directory recursively.
 */
static int
copy_dir(const char *source, const char *target)
{
	DIR		*dirp;
	struct dirent	*dp;
	struct stat	 sb;
	int		 ret;
	char		 spath[PATH_MAX], tpath[PATH_MAX];

	/* Create target directory. */
	if (stat(target, &sb)) {
		if (mkdir(target, 0777)) {
			warn("%s", target);
			return 1;
		}
		if (p_flag && lstat(source, &sb) == 0) {
			(void)chmod(target, sb.st_mode);
		}
	}

	dirp = opendir(source);
	if (dirp == NULL) {
		warn("%s", source);
		return 1;
	}

	ret = 0;
	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_name[0] == '.' &&
		    (dp->d_name[1] == '\0' ||
		    (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
			continue;

		if (snprintf(spath, sizeof(spath), "%s/%s",
		    source, dp->d_name) >= (ssize_t)sizeof(spath) ||
		    snprintf(tpath, sizeof(tpath), "%s/%s",
		    target, dp->d_name) >= (ssize_t)sizeof(tpath)) {
			errno = ENAMETOOLONG;
			warn("%s", dp->d_name);
			ret = 1;
			continue;
		}

		ret |= copy_one(spath, tpath);
	}

	(void)closedir(dirp);

	/* Preserve directory timestamps. */
	if (p_flag && lstat(source, &sb) == 0) {
		struct timeval times[2];
		times[0].tv_sec = sb.st_atime;
		times[0].tv_usec = 0;
		times[1].tv_sec = sb.st_mtime;
		times[1].tv_usec = 0;
		(void)utimes(target, times);
	}

	if (v_flag)
		(void)printf("%s -> %s\n", source, target);

	return ret;
}

/*
 * Usage message.
 */
static void
usage(void)
{

	(void)fprintf(stderr,
	    "usage: cp [-fiprv] source [target]\n"
	    "       cp [-fiprv] source ... directory\n");
	exit(1);
}

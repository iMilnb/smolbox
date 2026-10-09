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
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---- flags ---- */
static int	f_all;		/* -a: show hidden files */
static int	f_longform;		/* -l: long listing */
static int	f_oneline;		/* -1: one entry per line */
static int	f_listdir;		/* -d: list directory itself */
static int	f_recursive;	/* -R: recursive */
static int	f_reversesort;	/* -r: reverse sort order */
static int	f_sorttime;		/* -t: sort by modification time */
static int	f_sortsize;		/* -S: sort by file size */
static int	f_showtype;		/* -F: append type indicator */

/* ---- entry structure for sorting ---- */
struct entry {
	char		*name;
	struct stat	 sb;
};

static int	 cmp_name(const void *, const void *);
static int	 cmp_time(const void *, const void *);
static int	 cmp_size(const void *, const void *);
static void	 print_long(const char *, const struct stat *);
static const char	*mode_string(mode_t);
static int	 list_dir(const char *, int);
static void	 usage(void) __attribute__((__noreturn__));

/*
 * Main entry point.
 */
int
main_ls(int argc, char *argv[])
{
	struct winsize	 win;
	int		 ch, rval;

	/* Default format: columns on tty, single column otherwise. */
	if (isatty(STDOUT_FILENO)) {
		if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &win) == 0 &&
		    win.ws_col > 0)
			(void)win; /* reserved for column layout */
	} else {
		f_oneline = 1;
	}

	while ((ch = getopt(argc, argv, "1adFlRrst")) != -1)
		switch (ch) {
		case '1':
			f_oneline = 1;
			break;
		case 'a':
			f_all = 1;
			break;
		case 'd':
			f_listdir = 1;
			f_recursive = 0;
			break;
		case 'F':
			f_showtype = 1;
			break;
		case 'l':
			f_longform = 1;
			f_oneline = 0;
			break;
		case 'R':
			f_recursive = 1;
			break;
		case 'r':
			f_reversesort = 1;
			break;
		case 's':
			f_sortsize = 1;
			break;
		case 't':
			f_sorttime = 1;
			break;
		default:
			usage();
		}

	argc -= optind;
	argv += optind;
	rval = 0;

	if (argc == 0) {
		static char *dotav[] = { ".", NULL };
		argv = dotav;
		argc = 1;
	}

	for (int i = 0; i < argc; i++) {
		if (argc > 1 && !f_listdir)
			(void)printf("%s:\n", argv[i]);
		rval |= list_dir(argv[i], 0);
	}

	return rval;
}

/*
 * List a single directory (or file with -d).
 */
static int
list_dir(const char *path, int depth)
{
	DIR		*dirp;
	struct dirent	*dp;
	struct entry	*entries = NULL;
	size_t		 nentries = 0, alloc = 0;
	struct stat	 sb;
	int		 is_dir, rval;

	rval = 0;

	if (lstat(path, &sb) == -1) {
		(void)fprintf(stderr, "ls: %s: %s\n",
		    path, strerror(errno));
		return (1);
	}

	is_dir = S_ISDIR(sb.st_mode);

	/* With -d, list the entry itself, not its contents. */
	if (f_listdir) {
		if (f_longform)
			print_long(path, &sb);
		else
			(void)puts(path);
		return (0);
	}

	if (!is_dir) {
		/* Not a directory: print name and done. */
		if (f_longform)
			print_long(path, &sb);
		else
			(void)puts(path);
		return (0);
	}

	dirp = opendir(path);
	if (dirp == NULL) {
		(void)fprintf(stderr, "ls: %s: %s\n",
		    path, strerror(errno));
		return (1);
	}

	while ((dp = readdir(dirp)) != NULL) {
		/* Skip . and .. unless -a. */
		if (!f_all && (dp->d_name[0] == '.' &&
		    (dp->d_name[1] == '\0' ||
		    (dp->d_name[1] == '.' && dp->d_name[2] == '\0'))))
			continue;

		/* Build full path for stat. */
		char fpath[PATH_MAX];
		size_t plen = strlen(path);

		/* Avoid double slash for "/". */
		if (plen > 1 && path[plen - 1] == '/')
			plen--;
		(void)snprintf(fpath, sizeof(fpath), "%.*s/%s",
		    (int)plen, path, dp->d_name);

		if (lstat(fpath, &sb) == -1) {
			(void)fprintf(stderr, "ls: %s: %s\n",
			    fpath, strerror(errno));
			continue;
		}

		if (nentries == alloc) {
			alloc = alloc == 0 ? 64 : alloc * 2;
			entries = realloc(entries,
			    alloc * sizeof(*entries));
			if (entries == NULL) {
				(void)fprintf(stderr, "ls: %s\n",
				    strerror(errno));
				(void)closedir(dirp);
				return (1);
			}
		}

		entries[nentries].name = strdup(dp->d_name);
		entries[nentries].sb = sb;
		nentries++;
	}

	(void)closedir(dirp);

	if (nentries == 0)
		return (0);

	/* Sort entries. */
	if (f_sorttime)
		qsort(entries, nentries, sizeof(*entries), cmp_time);
	else if (f_sortsize)
		qsort(entries, nentries, sizeof(*entries), cmp_size);
	else
		qsort(entries, nentries, sizeof(*entries), cmp_name);

	/* Reverse if requested. */
	if (f_reversesort) {
		size_t i, j = nentries - 1;
		for (i = 0; i < j; i++, j--) {
			struct entry tmp = entries[i];
			entries[i] = entries[j];
			entries[j] = tmp;
		}
	}

	/* Print entries. */
	for (size_t i = 0; i < nentries; i++) {
		if (f_longform)
			print_long(entries[i].name, &entries[i].sb);
		else {
			const char *name = entries[i].name;
			if (f_showtype) {
				if (S_ISDIR(entries[i].sb.st_mode))
					(void)printf("%s/", name);
				else if (entries[i].sb.st_mode &
				    (S_IXUSR | S_IXGRP | S_IXOTH))
					(void)printf("%s*", name);
				else
					(void)printf("%s", name);
			} else {
				(void)printf("%s", name);
			}

			if (f_oneline)
				(void)putchar('\n');
			else
				(void)putchar(' ');
		}
	}

	/* Ensure trailing newline for non-long, non-oneline formats. */
	if (!f_longform && !f_oneline)
		(void)putchar('\n');

	/* Recurse into subdirectories. */
	if (f_recursive) {
		for (size_t i = 0; i < nentries; i++) {
			if (!S_ISDIR(entries[i].sb.st_mode))
				continue;

			char subpath[PATH_MAX];
			size_t plen = strlen(path);

			if (plen > 1 && path[plen - 1] == '/')
				plen--;
			(void)snprintf(subpath, sizeof(subpath),
			    "%.*s/%s", (int)plen, path, entries[i].name);
			list_dir(subpath, depth + 1);
		}
	}

	/* Free entries. */
	for (size_t i = 0; i < nentries; i++)
		free(entries[i].name);
	free(entries);

	return (rval);
}

/*
 * Print a long-format line.
 */
static void
print_long(const char *name, const struct stat *sb)
{
	struct passwd	*pw;
	struct group	*gr;
	struct tm	*tm;
	char		 timebuf[32];
	mode_t		 mode = sb->st_mode;
	time_t		 t;

	/* File type and permissions. */
	(void)printf("%s ", mode_string(mode));

	/* Links. */
	(void)printf("%4lu ", (u_long)sb->st_nlink);

	/* Owner. */
	pw = getpwuid(sb->st_uid);
	(void)printf("%-8s ", pw ? pw->pw_name : "?");

	/* Group. */
	gr = getgrgid(sb->st_gid);
	(void)printf("%-8s ", gr ? gr->gr_name : "?");

	/* Size. */
	(void)printf("%8lld ", (long long)sb->st_size);

	/* Modification time. */
	t = sb->st_mtime;
	tm = localtime(&t);

	/* Use "Mon DD HH:MM" for recent, "Mon DD YYYY" for old. */
	time_t now;
	(void)time(&now);
	if (difftime(now, t) < 15552000) { /* ~6 months */
		(void)strftime(timebuf, sizeof(timebuf),
		    "%b %e %H:%M", tm);
	} else {
		(void)strftime(timebuf, sizeof(timebuf),
		    "%b %e  %Y", tm);
	}
	(void)printf("%-12s ", timebuf);

	/* Name with optional type indicator. */
	if (f_showtype) {
		if (S_ISDIR(mode))
			(void)printf("%s/\n", name);
		else if (mode & (S_IXUSR | S_IXGRP | S_IXOTH))
			(void)printf("%s*\n", name);
		else
			(void)printf("%s\n", name);
	} else {
		(void)puts(name);
	}
}

/*
 * Convert mode_t to "---rwxr-xr-t" style string.
 */
static const char *
mode_string(mode_t mode)
{
	static char buf[11];
	const char *perm = "rwxrwxrwx";
	int i;

	buf[0] =
	    S_ISDIR(mode) ? 'd' :
	    S_ISLNK(mode) ? 'l' :
	    S_ISBLK(mode) ? 'b' :
	    S_ISCHR(mode) ? 'c' :
	    S_ISFIFO(mode) ? 'p' :
	    S_ISSOCK(mode) ? 's' : '-';

	for (i = 0; i < 9; i++)
		buf[i + 1] =
		    (mode & (1 << (8 - i))) ? perm[i] : '-';

	/* Setuid/setgid/sticky override execute bits. */
	if (mode & S_ISUID)
		buf[3] = (buf[3] == 'x') ? 's' : 'S';
	if (mode & S_ISGID)
		buf[6] = (buf[6] == 'x') ? 's' : 'l';
	if (mode & S_ISVTX)
		buf[9] = (buf[9] == 'x') ? 't' : 'T';

	buf[10] = '\0';
	return buf;
}

/*
 * Compare functions for qsort.
 */
static int
cmp_name(const void *a, const void *b)
{
	const struct entry *ea = (const struct entry *)a;
	const struct entry *eb = (const struct entry *)b;

	return strcmp(ea->name, eb->name);
}

static int
cmp_time(const void *a, const void *b)
{
	const struct entry *ea = (const struct entry *)a;
	const struct entry *eb = (const struct entry *)b;

	if (ea->sb.st_mtime > eb->sb.st_mtime)
		return -1;
	if (ea->sb.st_mtime < eb->sb.st_mtime)
		return 1;
	return cmp_name(a, b);
}

static int
cmp_size(const void *a, const void *b)
{
	const struct entry *ea = (const struct entry *)a;
	const struct entry *eb = (const struct entry *)b;

	if (ea->sb.st_size > eb->sb.st_size)
		return -1;
	if (ea->sb.st_size < eb->sb.st_size)
		return 1;
	return cmp_name(a, b);
}

/*
 * Usage message.
 */
static void
usage(void)
{

	(void)fprintf(stderr,
	    "usage: ls [-1adFlRrst] [file ...]\n");
	exit(1);
}

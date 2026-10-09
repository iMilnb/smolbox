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
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <ufs/ufs/ufsmount.h>

#include <err.h>
#include <errno.h>
#include <fstab.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pathnames.h"

#define MNT_FAKEFLAG	0x1	/* -f: parse only */
#define MNT_UPDATEFLAG	0x2	/* -u: remount */

static int	debug, verbose, force;

static void	 usage(void) __attribute__((__noreturn__));
static int	 do_mount(const char *, const char *, const char *,
		    int, const char *);
static int	 do_mount_netbsd(const char *, const char *, const char *,
		    int, const char *);
static int	list_mounts(void);
static int	mount_all(int, int);
static int	 has_option(const char *, const char *);
static void	 append_option(char **, const char *);
static int	 update_mount(const char *, int, const char *);

/*
 * Main entry point.
 */
int
main_mount(int argc, char *argv[])
{
	int	 ch, all, flags;
	char	*options, *fstype, *spec, *node;

	all = 0;
	flags = 0;
	options = NULL;
	fstype = NULL;
	spec = NULL;
	node = NULL;

	while ((ch = getopt(argc, argv, "AadFfo:rt:uvw")) != -1)
		switch (ch) {
		case 'A':
			all = 1;
			force = 1;
			break;
		case 'a':
			all = 1;
			break;
		case 'd':
			debug = 1;
			break;
		case 'F':
			/* FreeBSD compatibility: ignored. */
			break;
		case 'f':
			/* Fake mode: parse only, don't mount. */
			flags |= MNT_FAKEFLAG;
			break;
		case 'o':
			append_option(&options, optarg);
			break;
		case 'r':
			append_option(&options, "ro");
			break;
		case 't':
			fstype = optarg;
			break;
		case 'u':
			/* Update mode: remount with new options. */
			flags |= MNT_UPDATEFLAG;
			break;
		case 'v':
			verbose++;
			break;
		case 'w':
			append_option(&options, "rw");
			break;
		case '?':
		default:
			usage();
			/* NOTREACHED */
		}

	argc -= optind;
	argv += optind;

	/*
	 * No arguments: list mounted filesystems.
	 */
	if (argc == 0) {
		if (all)
			return mount_all(force, flags);
		return list_mounts();
	}

	/*
	 * Update mode: mount -u [-o options] node
	 */
	if (flags & MNT_UPDATEFLAG) {
		if (argc != 1)
			usage();
		return update_mount(argv[0], flags, options);
	}

	/*
	 * Mount a single filesystem.
	 */
	switch (argc) {
	case 1:
		/*
		 * Single argument: look up in fstab.
		 */
		{
			struct fstab *fs;

			if ((fs = getfsfile(argv[0])) == NULL &&
			    (fs = getfsspec(argv[0])) == NULL)
				errx(1, "%s: not found in %s",
				    argv[0], _PATH_FSTAB);
			spec = fs->fs_spec;
			node = fs->fs_file;
			if (fstype == NULL && fs->fs_vfstype != NULL)
				fstype = fs->fs_vfstype;
			if (options == NULL && fs->fs_mntops != NULL &&
				    (options = strdup(fs->fs_mntops)) == NULL)
				err(1, NULL);
		}
		break;
	case 2:
		spec = argv[0];
		node = argv[1];
		break;
	default:
		usage();
		/* NOTREACHED */
	}

	if (fstype == NULL)
		fstype = "ffs";

	if (debug) {
		(void)printf("mount: spec=%s node=%s type=%s options=%s\n",
		    spec, node, fstype, options ? options : "(none)");
		return 0;
	}

	return do_mount(fstype, spec, node, flags, options);
}

/*
 * Perform the actual mount syscall.
 */
static int
do_mount(const char *fstype, const char *spec, const char *node,
    int flags, const char *options)
{
	int mntflags;

	if (flags & 1) {
		/* Fake mode: just print what would be done. */
		(void)printf("would mount %s on %s type %s",
		    spec, node, fstype);
		if (options != NULL)
			(void)printf(" (%s)", options);
		(void)putchar('\n');
		return 0;
	}

	mntflags = 0;
	if (flags & MNT_UPDATEFLAG)
		mntflags |= MNT_UPDATE;
	if (options != NULL) {
		if (has_option(options, "ro"))
			mntflags |= MNT_RDONLY;
		if (has_option(options, "nosuid"))
			mntflags |= MNT_NOSUID;
		if (has_option(options, "nodev"))
			mntflags |= MNT_NODEV;
		if (has_option(options, "noexec"))
			mntflags |= MNT_NOEXEC;
		if (has_option(options, "noatime"))
			mntflags |= MNT_NOATIME;
		if (has_option(options, "async"))
			mntflags |= MNT_ASYNC;
		if (has_option(options, "update"))
			mntflags |= MNT_UPDATE;
	}

	return do_mount_netbsd(fstype, spec, node, mntflags, options);
}

/*
 * NetBSD mount(2) syscall:
 *   mount(fstype, dir, flags, data, len)
 *
 * For FFS, data is struct ufs_args.
 */
static int
do_mount_netbsd(const char *fstype, const char *spec, const char *node,
    int mntflags, const char *options)
{
	struct ufs_args args;
	int ret;

	/*
	 * ffs and its siblings (msdos, cd9660, ext2fs, …) all take
	 * struct ufs_args with the special device as fspec.
	 */
	(void)memset(&args, 0, sizeof(args));
	args.fspec = __UNCONST(spec);
	ret = mount(fstype, node, mntflags, &args, sizeof(args));

	if (ret == -1) {
		warn("mount %s on %s", spec, node);
		return 1;
	}

	if (verbose) {
		(void)printf("%s on %s type %s", spec, node, fstype);
		if (options != NULL)
			(void)printf(" (%s)", options);
		(void)putchar('\n');
	}

	return 0;
}

/*
 * List currently mounted filesystems.
 */
static int
list_mounts(void)
{
	struct statvfs *mntbuf;
	int count, i;

	count = getmntinfo(&mntbuf, MNT_NOWAIT);
	if (count == -1) {
		warn("getmntinfo");
		return 1;
	}

	for (i = 0; i < count; i++) {
		(void)printf("%s on %s type %s",
		    mntbuf[i].f_mntfromname,
		    mntbuf[i].f_mntonname,
		    mntbuf[i].f_fstypename);

		if (mntbuf[i].f_flag & MNT_RDONLY)
			(void)printf(" (ro");
		else
			(void)printf(" (rw");

		if (verbose) {
			if (mntbuf[i].f_flag & MNT_NOSUID)
				(void)printf(", nosuid");
			if (mntbuf[i].f_flag & MNT_NODEV)
				(void)printf(", nodev");
			if (mntbuf[i].f_flag & MNT_NOEXEC)
				(void)printf(", noexec");
		}

		(void)printf(")\n");
	}

	return 0;
}

/*
 * Mount all filesystems from fstab.
 */
static int
mount_all(int forceall, int flags)
{
	struct fstab *fs;
	int ret, error;

	ret = 0;
	setfsent();
	while ((fs = getfsent()) != NULL) {
		const char *spec, *node, *fstype, *opts;

		if (strcmp(fs->fs_type, FSTAB_RO) != 0 &&
		    strcmp(fs->fs_type, FSTAB_RW) != 0 &&
		    strcmp(fs->fs_type, FSTAB_RQ) != 0)
			continue;

		if (!forceall && has_option(fs->fs_mntops, "noauto"))
			continue;

		spec = fs->fs_spec;
		node = fs->fs_file;
		fstype = fs->fs_vfstype != NULL ? fs->fs_vfstype : "ffs";

		/* Check if already mounted. */
		{
			struct statvfs *mntbuf;
			int count, j, found = 0;

			count = getmntinfo(&mntbuf, MNT_NOWAIT);
			if (count == -1) {
				warn("getmntinfo");
				ret = 1;
				continue;
			}
			for (j = 0; j < count; j++) {
				if (strcmp(mntbuf[j].f_mntonname, node) == 0) {
					found = 1;
					break;
				}
			}
			if (found) {
				if (verbose)
					(void)printf("%s on %s: already "
					    "mounted\n", spec, node);
				continue;
			}
		}

		opts = fs->fs_mntops;
		error = do_mount(fstype, spec, node, flags, opts);
		if (error)
			ret = 1;
	}
	endfsent();

	return ret;
}

/*
 * Update (remount) an existing mount with new options.
 */
static int
update_mount(const char *name, int flags, const char *options)
{
	struct statvfs *mntbuf;
	int count, i;

	count = getmntinfo(&mntbuf, MNT_NOWAIT);
	if (count == -1) {
		warn("getmntinfo");
		return 1;
	}

	for (i = 0; i < count; i++) {
		if (strcmp(mntbuf[i].f_mntonname, name) == 0 ||
		    strcmp(mntbuf[i].f_mntfromname, name) == 0)
			break;
	}

	if (i >= count)
		errx(1, "%s: not currently mounted", name);

	return do_mount(mntbuf[i].f_fstypename,
	    mntbuf[i].f_mntfromname, mntbuf[i].f_mntonname,
	    flags | MNT_UPDATEFLAG, options);
}

/*
 * Check if a comma-separated option string contains a specific option
 * (exact token match; negated forms like "nodev" are queried as-is).
 */
static int
has_option(const char *options, const char *option)
{
	char	*optbuf, *opt, *saveptr;
	int	 found;

	if (options == NULL)
		return 0;

	optbuf = strdup(options);
	if (optbuf == NULL)
		return 0;

	found = 0;
	for (opt = strtok_r(optbuf, ",", &saveptr); opt != NULL;
	    opt = strtok_r(NULL, ",", &saveptr)) {
		if (strcmp(opt, option) == 0) {
			found = 1;
			break;
		}
	}
	free(optbuf);
	return found;
}

/*
 * Append an option to a comma-separated option string.
 */
static void
append_option(char **options, const char *opt)
{
	char *newopts;

	if (*options != NULL) {
		if (asprintf(&newopts, "%s,%s", *options, opt) == -1)
			err(1, NULL);
		free(*options);
		*options = newopts;
	} else {
		if ((*options = strdup(opt)) == NULL)
			err(1, NULL);
	}
}

/*
 * Print usage and exit.
 */
static void
usage(void)
{

	(void)fprintf(stderr,
	    "usage: mount [-Aadfruvw] [-o options] [-t fstype] "
	    "[special-node | node]\n"
	    "usage: mount -u [-o options] node\n");
	exit(1);
}

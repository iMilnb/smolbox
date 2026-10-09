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

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Tool entry points. */
extern int main_cp(int, char *[]);
extern int main_init(int, char **);
extern int main_ln(int, char *[]);
extern int main_ls(int, char *[]);
extern int main_mount(int, char *[]);
extern int main_rm(int, char *[]);
extern int main_sh(int, char *[]);
extern int main_sysctl(int, char *[]);

struct app {
	const char	*name;
	int		(*entry)(int, char *[]);
};

static const struct app apps[] = {
	{"cp",		main_cp},
	{"init",	main_init},
	{"ln",		main_ln},
	{"ls",		main_ls},
	{"mount",	main_mount},
	{"rm",		main_rm},
	{"sh",		main_sh},
	{"sysctl",	main_sysctl},
};

#define NAPP	(sizeof(apps) / sizeof(apps[0]))

static const char usage_msg[] =
"usage: smolbox <command> [args...]\n"
"\n"
"available commands:\n"
"  cp      minimal cp(1)\n"
"  init    minimal init(8)\n"
"  ln      minimal ln(1)\n"
"  ls      minimal ls(1)\n"
"  mount   minimal mount(8)\n"
"  rm      minimal rm(1)\n"
"  sh      minimal POSIX-ish shell\n"
"  sysctl  minimal sysctl(8)\n";

int
main(int argc, char *argv[])
{
	const char		*base;
	const struct app	*ap;
	size_t			 i;

	/*
	 * Determine which tool to run.
	 *
	 *   - Invoked via a symlink named after an applet (e.g. "init",
	 *     or a login shell "-sh"): use that name directly.
	 *   - Otherwise (invoked as "smolbox", or via an unrecognised
	 *     symlink): multi-call mode, the command is argv[1].
	 */
	if (argc < 1 || argv[0] == NULL) {
		fputs(usage_msg, stderr);
		return 1;
	}

	base = strrchr(argv[0], '/');
	base = base != NULL ? base + 1 : argv[0];
	while (*base == '-')		/* login-shell "-" prefix */
		base++;

	for (i = 0; i < NAPP; i++) {
		if (strcmp(apps[i].name, base) == 0) {
			ap = &apps[i];
			goto found;
		}
	}

	/* Not an applet name: fall back to "smolbox <cmd> [args...]". */
	if (argc < 2) {
		fputs(usage_msg, stderr);
		return 1;
	}
	base = argv[1];
	argc--;
	argv++;

	for (i = 0; i < NAPP; i++) {
		if (strcmp(apps[i].name, base) == 0) {
			ap = &apps[i];
			goto found;
		}
	}

	errx(1, "unknown command: %s", base);

found:
	return ap->entry(argc, argv);
}

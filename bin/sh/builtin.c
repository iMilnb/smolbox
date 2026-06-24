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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "defs.h"

extern int	 exit_status;
extern pid_t	 shell_pid;

static int	 builtin_cd(char *[]);
static int	 builtin_echo(char *[]);
static int	 builtin_exit(char *[]);
static int	 builtin_export(char *[]);
static int	 builtin_unset(char *[]);
static int	 builtin_set(char *[]);
static int	 builtin_printenv(char *[]);
static int	 builtin_true(char *[]);
static int	 builtin_false(char *[]);
static int	 builtin_test(char *[]);
static int	 builtin_type(char *[]);
static int	 builtin_hash(char *[]);
static int	 builtin_pwd(char *[]);
static int	 builtin_umask(char *[]);
static int	 builtin_wait(char *[]);

typedef int (*builtin_fn)(char *[]);

struct builtin {
	const char	*name;
	builtin_fn	func;
};

static const struct builtin builtins[] = {
	{ ":",	builtin_true },
	{ "cd",	builtin_cd },
	{ "echo",	builtin_echo },
	{ "exit",	builtin_exit },
	{ "export",	builtin_export },
	{ "false",	builtin_false },
	{ "hash",	builtin_hash },
	{ "printenv",	builtin_printenv },
	{ "pwd",	builtin_pwd },
	{ "set",	builtin_set },
	{ "test",	builtin_test },
	{ "true",	builtin_true },
	{ "type",	builtin_type },
	{ "umask",	builtin_umask },
	{ "unset",	builtin_unset },
	{ "wait",	builtin_wait },
	{ NULL,	NULL },
};

/*
 * Check if a command name is a builtin.
 */
int
is_builtin(const char *name)
{
	const struct builtin *bp;

	for (bp = builtins; bp->name != NULL; bp++)
		if (strcmp(bp->name, name) == 0)
			return 1;
	return 0;
}

/*
 * Run a builtin command.
 */
int
run_builtin(char *argv[])
{
	const struct builtin *bp;

	for (bp = builtins; bp->name != NULL; bp++) {
		if (strcmp(bp->name, argv[0]) == 0)
			return bp->func(argv);
	}
	return 127;
}

/*
 * Initialize builtins (currently a no-op, reserved for future use).
 */
void
builtin_init(void)
{
}

/*
 * cd: change directory.
 */
static int
builtin_cd(char *argv[])
{
	const char *dir;

	(void)argv;
	if (argv[1] != NULL)
		dir = argv[1];
	else if (var_get("HOME") != NULL)
		dir = var_get("HOME");
	else
		dir = ".";

	if (chdir(dir) == -1) {
		(void)fprintf(stderr, "sh: cd: %s: %s\n",
		    dir, strerror(errno));
		return 1;
	}
	return 0;
}

/*
 * echo: print arguments.
 */
static int
builtin_echo(char *argv[])
{
	int i, first = 1;

	for (i = 1; argv[i] != NULL; i++) {
		if (!first)
			(void)putchar(' ');
		(void)fputs(argv[i], stdout);
		first = 0;
	}
	(void)putchar('\n');
	return 0;
}

/*
 * exit: exit the shell.
 */
static int
builtin_exit(char *argv[])
{
	int status;

	if (argv[1] != NULL) {
		char *endp;

		status = (int)strtol(argv[1], &endp, 10);
		if (*endp != '\0')
			status = 1;
	} else {
		status = exit_status;
	}

	done(status);
	/* NOTREACHED */
	return status;
}

/*
 * export: mark variables for export.
 */
static int
builtin_export(char *argv[])
{
	int i;

	if (argv[1] == NULL) {
		/* Print all exported variables. */
		for (i = 0; argv[i] != NULL; i++)
			(void)fprintf(stdout, "export %s\n", argv[i]);
		return 0;
	}

	for (i = 1; argv[i] != NULL; i++) {
		char *eq = strchr(argv[i], '=');

		if (eq != NULL) {
			*eq = '\0';
			var_set(argv[i], eq + 1);
			var_export(argv[i]);
		} else {
			var_export(argv[i]);
		}
	}
	return 0;
}

/*
 * unset: remove a variable.
 */
static int
builtin_unset(char *argv[])
{
	int i;

	if (argv[1] == NULL)
		return 0;

	for (i = 1; argv[i] != NULL; i++)
		var_unset(argv[i]);

	return 0;
}

/*
 * set: display or set shell options/positional parameters.
 */
static int
builtin_set(char *argv[])
{
	int i;

	if (argv[1] == NULL) {
		/* Print all variables. */
		for (i = 0; i < 256; i++) {
			char name[4];
			char *val;

			(void)snprintf(name, sizeof(name), "%d", i);
			val = var_get(name);
			if (val != NULL)
				(void)printf("$%s=%s\n", name, val);
		}
		return 0;
	}

	/* Set positional parameters. */
	var_set_positional(0, argv + 1);
	return 0;
}

/*
 * printenv: print environment variables.
 */
static int
builtin_printenv(char *argv[])
{
	int i;

	if (argv[1] == NULL) {
		/* Print all variables. */
		for (i = 0; i < 256; i++) {
			char name[4];
			char *val;

			(void)snprintf(name, sizeof(name), "%d", i);
			val = var_get(name);
			if (val != NULL)
				(void)printf("%s=%s\n", name, val);
		}
		return 0;
	}

	for (i = 1; argv[i] != NULL; i++) {
		char *val;

		val = var_get(argv[i]);
		if (val != NULL)
			(void)printf("%s\n", val);
		else
			return 1;
	}
	return 0;
}

/*
 * true: do nothing, successfully.
 */
static int
builtin_true(char *argv[])
{
	(void)argv;
	return 0;
}

/*
 * false: do nothing, unsuccessfully.
 */
static int
builtin_false(char *argv[])
{
	(void)argv;
	return 1;
}

/*
 * test: evaluate an expression.
 */
static int
builtin_test(char *argv[])
{
	if (argv[1] == NULL)
		return 1;

	if (strcmp(argv[1], "-z") == 0 && argv[2] != NULL)
		return argv[2][0] == '\0' ? 0 : 1;

	if (strcmp(argv[1], "-n") == 0 && argv[2] != NULL)
		return argv[2][0] != '\0' ? 0 : 1;

	if (strcmp(argv[1], "-f") == 0 && argv[2] != NULL)
		return access(argv[2], F_OK) == 0 ? 0 : 1;

	if (strcmp(argv[1], "-d") == 0 && argv[2] != NULL) {
		struct stat sb;

		return stat(argv[2], &sb) == 0 &&
		    (sb.st_mode & S_IFDIR) ? 0 : 1;
	}

	if (strcmp(argv[1], "-e") == 0 && argv[2] != NULL)
		return access(argv[2], F_OK) == 0 ? 0 : 1;

	if (strcmp(argv[1], "=") == 0 && argv[3] == NULL)
		return strcmp(argv[2], argv[3]) == 0 ? 0 : 1;

	if (strcmp(argv[1], "!=") == 0 && argv[3] == NULL)
		return strcmp(argv[2], argv[3]) != 0 ? 0 : 1;

	/* Single argument: true if non-empty. */
	return argv[1][0] != '\0' ? 0 : 1;
}

/*
 * type: display command type.
 */
static int
builtin_type(char *argv[])
{
	int i;

	for (i = 1; argv[i] != NULL; i++) {
		if (is_builtin(argv[i]))
			(void)printf("%s is a shell builtin\n", argv[i]);
		else if (access(argv[i], X_OK) == 0)
			(void)printf("%s is %s\n", argv[i], argv[i]);
		else
			(void)printf("%s not found\n", argv[i]);
	}
	return 0;
}

/*
 * hash: remember command locations (stub).
 */
static int
builtin_hash(char *argv[])
{
	(void)argv;
	return 0;
}

/*
 * pwd: print working directory.
 */
static int
builtin_pwd(char *argv[])
{
	char buf[PATH_MAX];

	(void)argv;
	if (getcwd(buf, sizeof(buf)) != NULL)
		(void)puts(buf);
	else
		(void)fprintf(stderr, "sh: pwd: %s\n",
		    strerror(errno));
	return 0;
}

/*
 * umask: display or set file mode mask.
 */
static int
builtin_umask(char *argv[])
{
	mode_t mask;

	if (argv[1] != NULL) {
		char *endp;

		mask = (mode_t)strtol(argv[1], &endp, 8);
		if (*endp != '\0')
			return 1;
		mask = umask(mask);
	} else {
		mask = umask(0);
		umask(mask);
	}
	(void)printf("%04o\n", mask);
	return 0;
}

/*
 * wait: wait for child processes.
 */
static int
builtin_wait(char *argv[])
{
	int status;
	pid_t pid;

	(void)argv;
	pid = waitpid(-1, &status, 0);
	if (pid == -1)
		return 1;
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return 128 + WTERMSIG(status);
}

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
#include <sys/wait.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "defs.h"

#define	PROMPT		"$ "
#define	PS2_PROMPT	"> "
#define	MAX_LINE	4096

int			exit_status = 0;
pid_t			shell_pid;
bool			interactive = false;
bool			executing_script = false;
const char		*shell_name = "sh";

static volatile sig_atomic_t	child_status = 0;
static volatile sig_atomic_t	interrupted = 0;
bool				xtrace = false;
static bool			ignoreeof = false;
static bool			verbose = false;

/* Global lexer instance used by read_eval_loop and execute_script. */
struct lexer		lexer;

static void	 handle_signal(int);
static void	 handle_child(int);
static void	 usage(void);
static int	 execute_script(const char *);
static int	 read_eval_loop(void);
static char	*read_line(const char *);
static void	 print_prompt(void);

/*
 * Main entry point.
 */
int
main_sh(int argc, char *argv[])
{
	struct sigaction	sa;
	sigset_t		mask;
	int			c, flags;

	shell_pid = getpid();
	interactive = isatty(STDIN_FILENO) && isatty(STDERR_FILENO);

	/*
	 * Set up signal handlers.
	 */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	sa.sa_handler = handle_signal;
	(void)sigaction(SIGINT, &sa, NULL);
	(void)sigaction(SIGQUIT, &sa, NULL);

	sa.sa_handler = SIG_IGN;
	(void)sigaction(SIGTSTP, &sa, NULL);
	(void)sigaction(SIGTTIN, &sa, NULL);
	(void)sigaction(SIGTTOU, &sa, NULL);

	sa.sa_handler = handle_child;
	sa.sa_flags |= SA_NOCLDSTOP;
	(void)sigaction(SIGCHLD, &sa, NULL);

	/* Block SIGCHLD during parsing/execution. */
	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);
	(void)sigprocmask(SIG_SETMASK, &mask, NULL);

	/*
	 * Initialize builtins and variables.
	 */
	builtin_init();

	flags = 0;
	while ((c = getopt(argc, argv, "+:c:imnv")) != -1)
		switch (c) {
		case 'c':
			/* Command string mode. */
			var_init();
			var_set("0", shell_name);
			return execute_script(optarg);
		case 'i':
			interactive = true;
			break;
		case 'm':
			/* Job control (reserved for future). */
			break;
		case 'n':
			/* No-exec mode: parse only. */
			flags |= 1;
			break;
		case 'v':
			verbose = true;
			break;
		case 'x':
			xtrace = true;
			break;
		case '?':
			usage();
			/* NOTREACHED */
		default:
			break;
		}

	argc -= optind;
	argv += optind;

	/*
	 * Initialize variables from environment.
	 */
	var_init();
	var_set("0", shell_name);

	/*
	 * If arguments remain, treat first as a script.
	 */
	if (argc > 0) {
		var_set_positional(argc, argv);
		return execute_script(argv[0]);
	}

	/*
	 * Interactive shell: source .profile if present and stdin is a tty.
	 */
	if (interactive && isatty(STDIN_FILENO)) {
		const char *home;
		char profile[PATH_MAX];

		if ((home = var_get("HOME")) == NULL)
			home = ".";
		(void)snprintf(profile, sizeof(profile), "%s/.profile", home);
		if (access(profile, F_OK) == 0)
			(void)execute_script(profile);
	}

	return read_eval_loop();
}

/*
 * Read-eval-print loop for interactive mode.
 */
static int
read_eval_loop(void)
{
	struct cmd	*tree;
	bool		cont = true;
	int		eofcount = 0;

	while (cont) {
		char *line;

		line = read_line(PROMPT);
		if (line == NULL) {
			if (interactive && ignoreeof) {
				if (eofcount++ == 0)
					(void)fprintf(stderr,
					    "Use Ctrl-D on an empty line to exit.\n");
				continue;
			}
			break;
		}
		eofcount = 0;

		if (verbose && *line != '\0') {
			(void)fprintf(stderr, "%s\n", line);
		}

		lexer_init(&lexer, line);
		lexer_next(&lexer);

		tree = parse_command(&lexer, NULL);
		free(line);

		if (tree != NULL) {
			exit_status = execute(tree);
			cmd_free(tree);
		}
		(void)fflush(stdout);
		(void)fflush(stderr);
	}

	var_cleanup();
	return exit_status;
}

/*
 * Execute a script file.
 */
static int
execute_script(const char *path)
{
	int		fd;
	struct cmd	*tree;
	off_t		size;
	ssize_t		n;
	char		*buf;
	int		status;

	if ((fd = open(path, O_RDONLY)) == -1) {
		(void)fprintf(stderr, "%s: %s: %s\n",
		    shell_name, path, strerror(errno));
		return 1;
	}

	size = lseek(fd, 0, SEEK_END);
	if (size == -1 || lseek(fd, 0, SEEK_SET) == -1) {
		(void)close(fd);
		return 1;
	}

	buf = malloc((size_t)size + 1);
	if (buf == NULL) {
		(void)close(fd);
		(void)fprintf(stderr, "%s: %s\n",
		    shell_name, strerror(errno));
		return 1;
	}

	n = read(fd, buf, (size_t)size);
	(void)close(fd);
	if (n == -1) {
		free(buf);
		return 1;
	}
	buf[n] = '\0';

	executing_script = true;
	var_set("0", executing_script ? path : shell_name);

	lexer_init(&lexer, buf);
	lexer_next(&lexer);

	tree = parse_command(&lexer, NULL);
	free(buf);

	status = 0;
	if (tree != NULL) {
		status = execute(tree);
		cmd_free(tree);
	}

	executing_script = false;
	return status;
}

/*
 * Read a line from stdin, handling multi-line input.
 */
static char *
read_line(const char *prompt)
{
	static char		line[MAX_LINE];
	static char		buf[MAX_LINE];
	size_t			pos = 0;
	const char		*p;

	if (interactive)
		(void)fputs(prompt, stderr);

	if (fgets(line, sizeof(line), stdin) == NULL)
		return NULL;

	/* Strip trailing newline. */
	size_t len = strlen(line);
	if (len > 0 && line[len - 1] == '\n')
		line[len - 1] = '\0';

	/*
	 * Check for line continuation (trailing backslash).
	 */
	while (line[len - 1] == '\\') {
		line[len - 1] = '\0';
		len--;
		if (fgets(buf, sizeof(buf), stdin) == NULL)
			break;
		pos = strlen(line);
		len = strlen(buf);
		if (len > 0 && buf[len - 1] == '\n')
			buf[len - 1] = '\0';
		if (pos + strlen(buf) >= sizeof(line) - 1)
			break;
		(void)strlcat(line, buf, sizeof(line));
	}

	/*
	 * Check for unclosed quotes or subshells (multi-line).
	 */
	for (p = line; *p != '\0'; p++) {
		if (*p == '\'' || *p == '"') {
			char quote = *p;
			p++;
			while (*p != '\0' && *p != quote) {
				if (*p == '\\')
					p++;
				p++;
			}
			if (*p == '\0') {
				/* Unclosed quote: read more lines. */
				print_prompt();
				if (fgets(buf, sizeof(buf), stdin) == NULL)
					break;
				len = strlen(buf);
				if (len > 0 && buf[len - 1] == '\n')
					buf[--len] = '\0';
				(void)strlcat(line, " ", sizeof(line));
				(void)strlcat(line, buf, sizeof(line));
				p = line + strlen(line) - len - 1;
				continue;
			}
		}
	}

	return strdup(line);
}

/*
 * Print the secondary prompt.
 */
static void
print_prompt(void)
{

	if (interactive)
		(void)fputs(PS2_PROMPT, stderr);
}

/*
 * Handle SIGINT and SIGQUIT.
 */
static void
handle_signal(int sig)
{

	if (interactive) {
		(void)write(STDERR_FILENO, "\n", 1);
		print_prompt();
	}
	interrupted = 1;
	if (sig == SIGINT)
		child_status = SIGINT;
	else
		child_status = SIGQUIT;
}

/*
 * Handle SIGCHLD - reap zombie children.
 */
static void
handle_child(int sig)
{
	(void)sig;
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;
}

/*
 * Exit the shell.
 */
void
done(int status)
{

	exit_status = status;
	var_cleanup();
	_exit(status);
}

/*
 * Print usage message.
 */
static void
usage(void)
{

	(void)fprintf(stderr, "usage: %s [-c command] [file]\n", shell_name);
	done(1);
}

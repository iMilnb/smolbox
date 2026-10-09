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
#include <unistd.h>

#include "defs.h"

static int	 exec_simple(struct cmd *);
static int	 exec_pipe(struct cmd *);
static int	 do_exec_argv(char **);
static int	 exec_subshell(struct cmd *);
static int	 exec_background(struct cmd *);
static pid_t	 fork_child(void);
static int	 wait_child(pid_t);
static char	*find_command(const char *);
static void	 apply_redirects(struct redirect *);
static void	 here_doc_expand(struct redirect *);
static char	**build_argv(struct cmd *);
static void	 free_argv(char **);
static int	 collect_stages(struct cmd *, struct cmd **, int);

/*
 * Execute a command tree.
 */
int
execute(struct cmd *tree)
{
	int status;

	if (tree == NULL)
		return 0;

	switch (tree->type) {
	case N_CMD:
		status = exec_simple(tree);
		break;
	case N_PIPE:
		status = exec_pipe(tree);
		break;
	case N_LIST:
		(void)execute(tree->left);
		status = execute(tree->right);
		break;
	case N_AND:
		if (execute(tree->left) != 0)
			return exit_status;
		status = execute(tree->right);
		break;
	case N_OR:
		if (execute(tree->left) == 0)
			return exit_status;
		status = execute(tree->right);
		break;
	case N_IF:
		if (execute(tree->left) == 0)
			status = execute(tree->right);
		else if (tree->else_cmd != NULL)
			status = execute(tree->else_cmd);
		else
			status = 0;
		break;
	case N_WHILE:
		while (execute(tree->left) == 0) {
			if (execute(tree->right) != 0)
				break;
		}
		status = exit_status;
		break;
	case N_FOR:
		status = exec_for(tree);
		break;
	case N_SUBSHELL:
		status = exec_subshell(tree);
		break;
	case N_GROUP:
		status = execute(tree->left);
		break;
	case N_BACKGROUND:
		status = exec_background(tree);
		break;
	default:
		status = 0;
		break;
	}

	return status;
}

/*
 * Expand a command's words: strip quotes, expand variables, apply leading
 * assignments to the current shell, and glob.  Returns a newly allocated
 * NULL-terminated argv, or NULL if nothing remains to execute (e.g. the
 * command consisted only of assignments).
 */
static char **
build_argv(struct cmd *cmd)
{
	char **out;
	int i, n = 0, cap = 0;

	for (i = 0; cmd->argv[i] != NULL; i++)
		cap++;
	out = calloc((size_t)(cap + 1), sizeof(char *));
	if (out == NULL)
		return NULL;

	for (i = 0; cmd->argv[i] != NULL; i++) {
		char *word = expand(cmd->argv[i]);
		char *eq;

		if (word == NULL)
			continue;

		/* Assignment: name=value, only before the command word. */
		eq = strchr(word, '=');
		if (n == 0 && eq != NULL && eq != word &&
		    (isalpha((unsigned char)word[0]) || word[0] == '_')) {
			int ok = 1, k;

			for (k = 0; k < (int)(eq - word); k++)
				if (!isalnum((unsigned char)word[k]) &&
				    word[k] != '_')
					ok = 0;
			if (ok) {
				*eq = '\0';
				var_set(word, eq + 1);
				free(word);
				continue;
			}
		}

		/* Pathname expansion. */
		{
			int gc = 0;
			char **g = glob_expand(word, &gc);

			if (g != NULL) {
				int j;

				for (j = 0; g[j] != NULL; j++)
					out[n++] = g[j];
				free(g);	/* free array only */
				free(word);
				continue;
			}
		}
		out[n++] = word;
	}
	out[n] = NULL;

	if (n == 0) {
		free(out);
		return NULL;
	}
	return out;
}

/*
 * Free an argv array built by build_argv().
 */
static void
free_argv(char **argv)
{
	int i;

	if (argv == NULL)
		return;
	for (i = 0; argv[i] != NULL; i++)
		free(argv[i]);
	free(argv);
}

/*
 * Execute a simple command (with possible redirections).
 */
static int
exec_simple(struct cmd *cmd)
{
	char **argv;
	int status;

	if (cmd->argv == NULL || cmd->argv[0] == NULL)
		return 0;

	argv = build_argv(cmd);
	if (argv == NULL) {
		exit_status = 0;
		return 0;		/* assignments only, or empty */
	}

	if (xtrace)
		(void)fprintf(stderr, "+ %s\n", argv[0]);

	/*
	 * Builtins run in the current shell so that cd, variable and
	 * redirection side effects persist; the standard fds are saved and
	 * restored so a redirection on a builtin does not leak out.
	 */
	if (is_builtin(argv[0])) {
		int si = dup(STDIN_FILENO);
		int so = dup(STDOUT_FILENO);

		apply_redirects(cmd->redirects);
		status = run_builtin(argv);
		/* Flush before restoring the fds so buffered output from the
		 * builtin lands on the redirected fd, not the terminal. */
		(void)fflush(stdout);
		(void)fflush(stderr);
		if (si >= 0) {
			(void)dup2(si, STDIN_FILENO);
			(void)close(si);
		}
		if (so >= 0) {
			(void)dup2(so, STDOUT_FILENO);
			(void)close(so);
		}
		free_argv(argv);
		exit_status = status;
		return status;
	}

	/* External command: fork, redirect in the child, exec. */
	{
		pid_t pid = fork_child();

		if (pid == -1) {
			(void)fprintf(stderr, "sh: fork: %s\n",
			    strerror(errno));
			free_argv(argv);
			exit_status = 1;
			return 1;
		}
		if (pid == 0) {
			apply_redirects(cmd->redirects);
			status = do_exec_argv(argv);
			_exit(status);
		}
		status = wait_child(pid);
	}
	free_argv(argv);
	exit_status = status;
	return status;
}

/*
 * Collect the stages of a left-nested pipeline into out[], in order.
 */
static int
collect_stages(struct cmd *node, struct cmd **out, int n)
{

	if (node == NULL)
		return n;
	if (node->type == N_PIPE) {
		n = collect_stages(node->left, out, n);
		return collect_stages(node->right, out, n);
	}
	if (n < 64)
		out[n++] = node;
	return n;
}

/*
 * Execute a pipeline: fork every stage, wire them together with pipes,
 * and return the exit status of the last stage.
 */
static int
exec_pipe(struct cmd *cmd)
{
	struct cmd *stages[64];
	pid_t pids[64];
	int n, i, status = 0;
	int prev_fd = -1;

	n = collect_stages(cmd, stages, 0);
	if (n == 0)
		return 0;

	for (i = 0; i < n; i++) {
		int pfd[2];
		int use_pipe = (i < n - 1);

		if (use_pipe && pipe(pfd) == -1) {
			(void)fprintf(stderr, "sh: pipe: %s\n",
			    strerror(errno));
			return 1;
		}

		pids[i] = fork_child();
		if (pids[i] == -1) {
			(void)fprintf(stderr, "sh: fork: %s\n",
			    strerror(errno));
			return 1;
		}
		if (pids[i] == 0) {
			char **av;
			int st;

			if (prev_fd != -1) {
				(void)dup2(prev_fd, STDIN_FILENO);
				(void)close(prev_fd);
			}
			if (use_pipe) {
				(void)dup2(pfd[1], STDOUT_FILENO);
				(void)close(pfd[0]);
				(void)close(pfd[1]);
			}
			apply_redirects(stages[i]->redirects);
			av = build_argv(stages[i]);
			if (av == NULL)
				_exit(0);
			if (is_builtin(av[0])) {
				st = run_builtin(av);
				(void)fflush(stdout);
				(void)fflush(stderr);
				_exit(st);
			}
			_exit(do_exec_argv(av));
		}

		/* Parent. */
		if (prev_fd != -1)
			(void)close(prev_fd);
		if (use_pipe) {
			(void)close(pfd[1]);
			prev_fd = pfd[0];
		} else {
			prev_fd = -1;
		}
	}
	if (prev_fd != -1)
		(void)close(prev_fd);

	for (i = 0; i < n; i++)
		status = wait_child(pids[i]);

	exit_status = status;
	return status;
}

/*
 * Actually exec an external command.
 */
static int
do_exec_argv(char **argv)
{
	char	*path;
	char	**env;

	if (argv == NULL || argv[0] == NULL)
		return 127;

	path = find_command(argv[0]);
	if (path == NULL) {
		(void)fprintf(stderr, "sh: %s: not found\n", argv[0]);
		return 127;
	}

	env = var_to_env();
	(void)execve(path, argv, env);

	/* exec failed: distinguish permission from not-found. */
	if (errno == EACCES) {
		(void)fprintf(stderr, "sh: %s: permission denied\n", argv[0]);
		free(path);
		return 126;
	}
	(void)fprintf(stderr, "sh: %s: %s\n", argv[0], strerror(errno));
	free(path);
	return 127;
}

/*
 * Execute a for loop.
 */
int
exec_for(struct cmd *cmd)
{
	int		i, nwords = 0, status;
	char		*words[256];

	if (cmd->var == NULL)
		return 0;

	if (cmd->words == NULL) {
		/* Iterate the positional parameters. */
		int count = var_positional_count();

		for (i = 1; i <= count && nwords < 255; i++)
			words[nwords++] = var_positional(i);
	} else {
		/* Expand the explicit word list at execution time. */
		for (i = 0; cmd->words[i] != NULL && nwords < 255; i++)
			words[nwords++] = expand(cmd->words[i]);
	}
	words[nwords] = NULL;

	status = 0;
	for (i = 0; i < nwords; i++) {
		var_set(cmd->var, words[i]);
		status = execute(cmd->right);
		free(words[i]);
		if (status != 0)
			break;
	}

	return status;
}

/*
 * Execute a subshell.
 */
static int
exec_subshell(struct cmd *cmd)
{
	pid_t pid;
	int status;

	pid = fork_child();
	if (pid == 0) {
		status = execute(cmd->left);
		(void)fflush(stdout);
		(void)fflush(stderr);
		_exit(status);
	}
	status = wait_child(pid);
	return status;
}

/*
 * Execute a command in the background.
 */
static int
exec_background(struct cmd *cmd)
{
	pid_t pid;

	pid = fork_child();
	if (pid == 0) {
		int status;

		status = execute(cmd->left);
		(void)fflush(stdout);
		(void)fflush(stderr);
		_exit(status);
	}
	/* Parent does not wait. */
	(void)fprintf(stderr, "  %ld\n", (long)pid);
	return 0;
}

/*
 * Fork a child process, resetting signals.
 */
static pid_t
fork_child(void)
{
	pid_t pid;

	pid = fork();
	if (pid == 0) {
		/* Child: reset signal handlers. */
		struct sigaction sa;

		sa.sa_handler = SIG_DFL;
		sa.sa_flags = 0;
		(void)sigemptyset(&sa.sa_mask);
		(void)sigaction(SIGINT, &sa, NULL);
		(void)sigaction(SIGQUIT, &sa, NULL);
		(void)sigaction(SIGCHLD, &sa, NULL);
	}

	return pid;
}

/*
 * Wait for a child process and return its exit status.
 */
static int
wait_child(pid_t pid)
{
	int status;

	if (pid == 0)
		return 0;

	while (waitpid(pid, &status, 0) == -1) {
		if (errno != EINTR)
			return 1;
	}

	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);
	return status;
}

/*
 * Find a command in PATH.
 */
static char *
find_command(const char *name)
{
	char	*path, *dir, *savedir;
	char	fullpath[PATH_MAX];

	/* If name contains '/', use it directly. */
	if (strchr(name, '/') != NULL)
		return strdup(name);

	/* var_get returns an owned copy; the fallback must be writable
	 * too, since strtok_r() modifies it in place. */
	path = var_get("PATH");
	if (path == NULL)
		path = strdup("/bin:/usr/bin");

	dir = strtok_r(path, ":", &savedir);
	while (dir != NULL) {
		(void)snprintf(fullpath, sizeof(fullpath),
		    "%s/%s", dir, name);
		if (access(fullpath, X_OK) == 0) {
			free(path);
			return strdup(fullpath);
		}
		dir = strtok_r(NULL, ":", &savedir);
	}

	free(path);
	return NULL;
}

/*
 * Apply I/O redirections.
 */
static void
apply_redirects(struct redirect *redir)
{
	struct redirect	*r;

	for (r = redir; r != NULL; r = r->next) {
		int fd, newfd;
		char *arg;

		arg = expand(r->arg);

		switch (r->type) {
		case REDIR_IN:
			fd = open(arg, O_RDONLY);
			if (fd >= 0)
				(void)dup2(fd, STDIN_FILENO);
			(void)close(fd);
			break;
		case REDIR_OUT:
			fd = open(arg, O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (fd >= 0)
				(void)dup2(fd, STDOUT_FILENO);
			(void)close(fd);
			break;
		case REDIR_APPEND:
			fd = open(arg, O_WRONLY | O_CREAT | O_APPEND, 0644);
			if (fd >= 0)
				(void)dup2(fd, STDOUT_FILENO);
			(void)close(fd);
			break;
		case REDIR_INOUT:
			fd = open(arg, O_RDWR);
			if (fd >= 0)
				(void)dup2(fd, STDIN_FILENO);
			(void)close(fd);
			break;
		case REDIR_DUP_IN:
			newfd = atoi(arg);
			if (newfd > 0)
				(void)dup2(newfd, STDIN_FILENO);
			break;
		case REDIR_DUP_OUT:
			newfd = atoi(arg);
			if (newfd > 0)
				(void)dup2(newfd, STDOUT_FILENO);
			break;
		case REDIR_HEREDOC:
			here_doc_expand(r);
			break;
		}

		free(arg);
	}
}

/*
 * Expand a here-document.
 */
static void
here_doc_expand(struct redirect *redir)
{
	int		pfd[2];

	(void)redir;
	if (pipe(pfd) == -1)
		return;

	switch (fork()) {
	case -1:
		(void)close(pfd[0]);
		(void)close(pfd[1]);
		return;
	case 0:
		/* Child: write heredoc to pipe. */
		(void)close(pfd[0]);
		/* Heredoc content would be read from stdin. */
		(void)close(pfd[1]);
		_exit(0);
	default:
		/* Parent: read from pipe. */
		(void)close(pfd[1]);
		(void)dup2(pfd[0], STDIN_FILENO);
		(void)close(pfd[0]);
		break;
	}
}

/*
 * Helper functions for parser to create composite nodes.
 */
struct cmd *
mklist(struct cmd *left, struct cmd *right)
{
	struct cmd *cmd;

	cmd = calloc(1, sizeof(*cmd));
	if (cmd != NULL) {
		cmd->type = N_LIST;
		cmd->left = left;
		cmd->right = right;
	}
	return cmd;
}

struct cmd *
mkand(struct cmd *left, struct cmd *right)
{
	struct cmd *cmd;

	cmd = calloc(1, sizeof(*cmd));
	if (cmd != NULL) {
		cmd->type = N_AND;
		cmd->left = left;
		cmd->right = right;
	}
	return cmd;
}

struct cmd *
mkor(struct cmd *left, struct cmd *right)
{
	struct cmd *cmd;

	cmd = calloc(1, sizeof(*cmd));
	if (cmd != NULL) {
		cmd->type = N_OR;
		cmd->left = left;
		cmd->right = right;
	}
	return cmd;
}

struct cmd *
mkpipe(struct cmd *left, struct cmd *right)
{
	struct cmd *cmd;

	cmd = calloc(1, sizeof(*cmd));
	if (cmd != NULL) {
		cmd->type = N_PIPE;
		cmd->left = left;
		cmd->right = right;
	}
	return cmd;
}

struct cmd *
mkbg(struct cmd *cmd)
{
	struct cmd *bg;

	bg = calloc(1, sizeof(*bg));
	if (bg != NULL) {
		bg->type = N_BACKGROUND;
		bg->left = cmd;
	}
	return bg;
}

/*
 * Free a redirect node.
 */
void
redirect_free(struct redirect *r)
{

	free(r->arg);
	free(r);
}

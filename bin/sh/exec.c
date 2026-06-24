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
static int	 exec_builtin_or_cmd(struct cmd *);
static int	 do_exec(struct cmd *);
static int	 exec_subshell(struct cmd *);
static int	 exec_background(struct cmd *);
static pid_t	 fork_child(void);
static int	 wait_child(pid_t);
static char	*find_command(const char *);
static void	 apply_redirects(struct redirect *);
static void	 here_doc_expand(struct redirect *);

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
 * Execute a simple command (with possible redirections).
 */
static int
exec_simple(struct cmd *cmd)
{
	int status;

	if (cmd->argv == NULL || cmd->argv[0] == NULL)
		return 0;

	if (xtrace)
		(void)fprintf(stderr, "+ %s\n", cmd->argv[0]);

	status = exec_builtin_or_cmd(cmd);
	return status;
}

/*
 * Execute a pipeline.
 */
static int
exec_pipe(struct cmd *cmd)
{
	struct cmd	*node;
	int			pipefd[2];
	pid_t		pid;
	int			status;

	node = cmd->left;

	/* First command in pipeline. */
	status = exec_builtin_or_cmd(node);
	node = node->right;

	while (node != NULL) {
		if (pipe(pipefd) == -1) {
			(void)fprintf(stderr, "sh: pipe: %s\n",
			    strerror(errno));
			return 1;
		}

		pid = fork_child();
		if (pid == 0) {
			/* Child: connect stdin to pipe. */
			(void)close(pipefd[1]);
			(void)dup2(pipefd[0], STDIN_FILENO);
			(void)close(pipefd[0]);
			status = exec_builtin_or_cmd(node);
			_exit(status);
		}

		/* Parent: wait for child. */
		status = wait_child(pid);
		node = node->right;
	}

	return status;
}

/*
 * Execute a command: builtin or external.
 */
static int
exec_builtin_or_cmd(struct cmd *cmd)
{
	int status;

	if (cmd->argv == NULL || cmd->argv[0] == NULL)
		return 0;

	/* Apply I/O redirections. */
	apply_redirects(cmd->redirects);

	/* Check for builtin. */
	if (is_builtin(cmd->argv[0]))
		return run_builtin(cmd->argv);

	/* External command: fork and exec. */
	pid_t pid = fork_child();
	switch (pid) {
	case -1:
		(void)fprintf(stderr, "sh: fork: %s\n",
		    strerror(errno));
		return 1;
	case 0:
		/* Child process. */
		status = do_exec(cmd);
		_exit(status);
	default:
		/* Parent: wait for child. */
		status = wait_child(pid);
		break;
	}

	return status;
}

/*
 * Actually exec an external command.
 */
static int
do_exec(struct cmd *cmd)
{
	char	*path;
	char	**env;

	if (cmd->argv == NULL || cmd->argv[0] == NULL)
		return 127;

	path = find_command(cmd->argv[0]);
	if (path == NULL) {
		(void)fprintf(stderr, "sh: %s: not found\n",
		    cmd->argv[0]);
		return 127;
	}

	env = var_to_env();
	(void)execve(path, cmd->argv, env);

	/* If exec fails, check if it's a directory or permission issue. */
	if (errno == EACCES) {
		(void)fprintf(stderr, "sh: %s: permission denied\n",
		    cmd->argv[0]);
		return 126;
	}
	(void)fprintf(stderr, "sh: %s: %s\n",
	    cmd->argv[0], strerror(errno));
	return 127;
}

/*
 * Execute a for loop.
 */
int
exec_for(struct cmd *cmd)
{
	int		i, status;
	char		*words[256];
	char		idx[4];

	if (cmd->words == NULL) {
		/* Use positional parameters. */
		words[0] = var_get("1");
		i = 1;
		while (words[i - 1] != NULL && i < 255) {
			(void)snprintf(idx, sizeof(idx), "%d", ++i);
			words[i - 1] = var_get(idx);
		}
	} else {
		/* Use explicit word list. */
		for (i = 0; cmd->words[i] != NULL && i < 255; i++)
			words[i] = cmd->words[i];
	}

	status = 0;
	for (i = 0; words[i] != NULL; i++) {
		var_set(cmd->var, words[i]);
		status = execute(cmd->right);
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
	char	*path, *dir, *savedir = NULL;
	char	fullpath[PATH_MAX];
	size_t	namelen;

	/* If name contains '/', use it directly. */
	if (strchr(name, '/') != NULL)
		return strdup(name);

	path = var_get("PATH");
	if (path == NULL)
		path = "/bin:/usr/bin";

	namelen = strlen(name);
	dir = strtok_r(path, ":", &savedir);

	while (dir != NULL) {
		(void)snprintf(fullpath, sizeof(fullpath),
		    "%s/%s", dir, name);
		if (access(fullpath, X_OK) == 0)
			return strdup(fullpath);
		dir = strtok_r(NULL, ":", &savedir);
	}

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
	pid_t		pid;

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

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defs.h"

/* Forward declarations. */
static struct cmd	*parse_pipeline(struct lexer *);
static struct cmd	*parse_command_word(struct lexer *);
static struct cmd	*parse_simple_or_control(struct lexer *);
static struct cmd	*parse_if(struct lexer *);
static struct cmd	*parse_while(struct lexer *);
static struct cmd	*parse_until(struct lexer *);
static struct cmd	*parse_for(struct lexer *);
static struct cmd	*parse_subshell(struct lexer *);
static struct cmd	*parse_group(struct lexer *);
static struct redirect	*parse_redirects(struct lexer *);
static char		*parse_redir_arg(struct lexer *);
static struct cmd	*make_cmd(enum node_type);
static void		 add_arg(struct cmd *, char *);

/* Set by the parser when a required keyword is missing. */
int parse_error = 0;

/*
 * Reserved words that terminate a command list (appear only in keyword
 * position, never as a command name in this shell).
 */
static int
is_reserved(const char *w)
{

	return strcmp(w, "then") == 0 || strcmp(w, "else") == 0 ||
	    strcmp(w, "elif") == 0 || strcmp(w, "fi") == 0 ||
	    strcmp(w, "do") == 0 || strcmp(w, "done") == 0;
}

/*
 * Consume command separators (';' and newlines).
 */
static void
skip_seps(struct lexer *lx)
{

	while (lx->cur.type == TOK_SEMI || lx->cur.type == TOK_NEWLINE)
		lexer_next(lx);
}

/*
 * Skip separators, then consume the expected keyword if present.
 * Returns 1 on match, 0 otherwise.
 */
static int
match_kw(struct lexer *lx, const char *kw)
{

	skip_seps(lx);
	if (lx->cur.type == TOK_WORD && lx->cur.word != NULL &&
	    strcmp(lx->cur.word, kw) == 0) {
		free(lx->cur.word);
		lx->cur.word = NULL;
		lexer_next(lx);
		return 1;
	}
	return 0;
}

/*
 * Parse a complete command from the lexer.
 */
struct cmd *
parse_command(struct lexer *lx, bool *background)
{
	struct cmd *tree;

	tree = parse_list(lx, TOK_EOF);
	if (background != NULL)
		*background = false;
	return tree;
}

/*
 * Parse a list of commands separated by ;, &&, ||, or &.
 */
struct cmd *
parse_list(struct lexer *lx, int terminator)
{
	struct cmd *left, *right;

	/* Skip leading separators. */
	skip_seps(lx);

	left = parse_pipeline(lx);
	if (left == NULL)
		return NULL;

	for (;;) {
		switch (lx->cur.type) {
		case TOK_NEWLINE:
		case TOK_SEMI:
			skip_seps(lx);
			if (lx->cur.type == terminator ||
			    lx->cur.type == TOK_EOF ||
			    (lx->cur.type == TOK_WORD && lx->cur.word != NULL &&
			    is_reserved(lx->cur.word)))
				return left;
			right = parse_pipeline(lx);
			if (right == NULL)
				return left;
			left = mklist(left, right);
			break;
		case TOK_AMPAMP:
			lx->cur.word = NULL;
			lexer_next(lx);
			right = parse_pipeline(lx);
			if (right == NULL)
				return left;
			left = mkand(left, right);
			break;
		case TOK_BARBAR:
			lx->cur.word = NULL;
			lexer_next(lx);
			right = parse_pipeline(lx);
			if (right == NULL)
				return left;
			left = mkor(left, right);
			break;
		case TOK_AMP:
			lx->cur.word = NULL;
			lexer_next(lx);
			left = mkbg(left);
			/* Fall through to check next token. */
			/* Intentional fallthrough */
		default:
			return left;
		}
	}
}

/*
 * Parse a pipeline: command [ '|' command ]...
 */
static struct cmd *
parse_pipeline(struct lexer *lx)
{
	struct cmd *left, *right;

	left = parse_command_word(lx);
	if (left == NULL)
		return NULL;

	while (lx->cur.type == TOK_PIPE) {
		lx->cur.word = NULL;
		lexer_next(lx);
		right = parse_command_word(lx);
		if (right == NULL)
			break;
		left = mkpipe(left, right);
	}

	return left;
}

/*
 * Parse a single command (with possible redirections).
 */
static struct cmd *
parse_command_word(struct lexer *lx)
{
	struct cmd *cmd;

	switch (lx->cur.type) {
	case TOK_WORD:
		if (lx->cur.word != NULL && is_reserved(lx->cur.word))
			return NULL;
		cmd = parse_simple_or_control(lx);
		break;
	case TOK_LPAREN:
		cmd = parse_subshell(lx);
		break;
	case TOK_LBRACE:
		cmd = parse_group(lx);
		break;
	case TOK_SEMI:
	case TOK_AMP:
	case TOK_AMPAMP:
	case TOK_BARBAR:
	case TOK_RPAREN:
	case TOK_RBRACE:
	case TOK_EOF:
		return NULL;
	default:
		/* Redirection with no command. */
		cmd = make_cmd(N_CMD);
		break;
	}

	if (cmd != NULL)
		cmd->redirects = parse_redirects(lx);

	return cmd;
}

/*
 * Parse a simple command or control structure (if, while, for).
 */
static struct cmd *
parse_simple_or_control(struct lexer *lx)
{
	struct cmd *cmd;

	if (lx->cur.type != TOK_WORD || lx->cur.word == NULL)
		return NULL;

	/* Check for control keywords. */
	if (strcmp(lx->cur.word, "if") == 0)
		return parse_if(lx);
	if (strcmp(lx->cur.word, "while") == 0)
		return parse_while(lx);
	if (strcmp(lx->cur.word, "until") == 0)
		return parse_until(lx);
	if (strcmp(lx->cur.word, "for") == 0)
		return parse_for(lx);

	/* Simple command. */
	cmd = make_cmd(N_CMD);
	cmd->argv = calloc(2, sizeof(char *));
	if (cmd->argv == NULL)
		return NULL;
	/* Store the raw word; expansion happens at execution time. */
	cmd->argv[0] = lx->cur.word;
	lx->cur.word = NULL;
	lexer_next(lx);

	/* Read additional arguments. */
	while (lx->cur.type == TOK_WORD) {
		add_arg(cmd, lx->cur.word);
		lx->cur.word = NULL;
		lexer_next(lx);
	}

	return cmd;
}

/*
 * Parse an if/then/else/fi construct.
 */
static struct cmd *
parse_if(struct lexer *lx)
{
	struct cmd *ifcmd;

	/* Consume "if". */
	free(lx->cur.word);
	lx->cur.word = NULL;
	lexer_next(lx);

	ifcmd = make_cmd(N_IF);
	ifcmd->left = parse_list(lx, TOK_SEMI);

	if (!match_kw(lx, "then"))
		parse_error = 1;

	ifcmd->right = parse_list(lx, TOK_SEMI);

	/* Optional "else". */
	skip_seps(lx);
	if (lx->cur.type == TOK_WORD && lx->cur.word != NULL &&
	    strcmp(lx->cur.word, "else") == 0) {
		free(lx->cur.word);
		lx->cur.word = NULL;
		lexer_next(lx);
		ifcmd->else_cmd = parse_list(lx, TOK_SEMI);
	}

	if (!match_kw(lx, "fi"))
		parse_error = 1;

	return ifcmd;
}

/*
 * Parse a while/do/done construct.
 */
static struct cmd *
parse_while(struct lexer *lx)
{
	struct cmd *whilecmd;

	/* Consume "while". */
	free(lx->cur.word);
	lx->cur.word = NULL;
	lexer_next(lx);

	whilecmd = make_cmd(N_WHILE);
	whilecmd->left = parse_list(lx, TOK_SEMI);

	if (!match_kw(lx, "do"))
		parse_error = 1;

	whilecmd->right = parse_list(lx, TOK_SEMI);

	if (!match_kw(lx, "done"))
		parse_error = 1;

	return whilecmd;
}

/*
 * Parse an until/do/done construct.
 */
static struct cmd *
parse_until(struct lexer *lx)
{
	struct cmd *untilcmd;

	/* Consume "until". */
	free(lx->cur.word);
	lx->cur.word = NULL;
	lexer_next(lx);

	untilcmd = make_cmd(N_WHILE);
	untilcmd->left = parse_list(lx, TOK_SEMI);

	if (!match_kw(lx, "do"))
		parse_error = 1;

	untilcmd->right = parse_list(lx, TOK_SEMI);

	if (!match_kw(lx, "done"))
		parse_error = 1;

	return untilcmd;
}

/*
 * Parse a for/var/in/words/do/done construct.
 */
static struct cmd *
parse_for(struct lexer *lx)
{
	struct cmd *forcmd;
	char **words = NULL;
	int nw = 0, cap = 0;

	/* Consume "for". */
	free(lx->cur.word);
	lx->cur.word = NULL;
	lexer_next(lx);

	forcmd = make_cmd(N_FOR);

	/* Read variable name. */
	if (lx->cur.type == TOK_WORD) {
		forcmd->var = lx->cur.word;
		lx->cur.word = NULL;
		lexer_next(lx);
	}

	/* Optional "in words". */
	skip_seps(lx);
	if (lx->cur.type == TOK_WORD && lx->cur.word != NULL &&
	    strcmp(lx->cur.word, "in") == 0) {
		free(lx->cur.word);
		lx->cur.word = NULL;
		lexer_next(lx);

		cap = 8;
		words = calloc((size_t)cap, sizeof(char *));
		while (lx->cur.type == TOK_WORD) {
			if (words == NULL) {
				free(lx->cur.word);
				lx->cur.word = NULL;
				lexer_next(lx);
				continue;
			}
			if (nw + 2 > cap) {
				char **tmp;

				cap *= 2;
				tmp = realloc(words,
				    (size_t)cap * sizeof(char *));
				if (tmp == NULL) {
					free(words);
					words = NULL;
					continue;
				}
				words = tmp;
			}
			/* Store raw word; expanded at execution time. */
			words[nw++] = lx->cur.word;
			lx->cur.word = NULL;
			words[nw] = NULL;
			lexer_next(lx);
		}
		forcmd->words = words;
	}

	if (!match_kw(lx, "do"))
		parse_error = 1;

	forcmd->right = parse_list(lx, TOK_SEMI);

	if (!match_kw(lx, "done"))
		parse_error = 1;

	return forcmd;
}

/*
 * Parse a subshell: ( command ).
 */
static struct cmd *
parse_subshell(struct lexer *lx)
{
	struct cmd *subshell;

	/* Consume "(". */
	lexer_next(lx);

	subshell = make_cmd(N_SUBSHELL);
	subshell->left = parse_list(lx, TOK_RPAREN);

	/* Consume ")". */
	if (lx->cur.type == TOK_RPAREN)
		lexer_next(lx);

	return subshell;
}

/*
 * Parse a group: { commands; }.
 */
static struct cmd *
parse_group(struct lexer *lx)
{
	struct cmd *group;

	/* Consume "{". */
	lexer_next(lx);

	group = make_cmd(N_GROUP);
	group->left = parse_list(lx, TOK_RBRACE);

	/* Consume "}". */
	if (lx->cur.type == TOK_RBRACE)
		lexer_next(lx);

	return group;
}

/*
 * Parse I/O redirections for a command.
 */
static struct redirect *
parse_redirects(struct lexer *lx)
{
	struct redirect *head = NULL, **tail = &head;

	for (;;) {
		struct redirect *r;

		switch (lx->cur.type) {
		case TOK_LT:
			lexer_next(lx);
			r = redirect_new(REDIR_IN,
			    parse_redir_arg(lx), 0);
			break;
		case TOK_GT:
			lexer_next(lx);
			r = redirect_new(REDIR_OUT,
			    parse_redir_arg(lx), 0);
			break;
		case TOK_GTGT:
			lexer_next(lx);
			r = redirect_new(REDIR_APPEND,
			    parse_redir_arg(lx), 0);
			break;
		case TOK_LTGT:
			lexer_next(lx);
			r = redirect_new(REDIR_INOUT,
			    parse_redir_arg(lx), 0);
			break;
		case TOK_LTAMP:
			lexer_next(lx);
			r = redirect_new(REDIR_DUP_IN,
			    parse_redir_arg(lx), 0);
			break;
		case TOK_GTAMP:
			lexer_next(lx);
			r = redirect_new(REDIR_DUP_OUT,
			    parse_redir_arg(lx), 0);
			break;
		default:
			return head;
		}

		if (r != NULL) {
			*tail = r;
			tail = &r->next;
		}
	}
}

/*
 * Parse a redirect argument (the word after <, >, etc.).
 */
static char *
parse_redir_arg(struct lexer *lx)
{

	if (lx->cur.type == TOK_WORD) {
		/* Store raw; apply_redirects() expands at execution time. */
		char *arg = lx->cur.word;

		lx->cur.word = NULL;
		lexer_next(lx);
		return arg != NULL ? arg : strdup("");
	}
	return strdup("");
}

/*
 * Create a new command node.
 */
static struct cmd *
make_cmd(enum node_type type)
{
	struct cmd *cmd;

	cmd = calloc(1, sizeof(*cmd));
	if (cmd != NULL)
		cmd->type = type;
	return cmd;
}

/*
 * Add an argument to a command's argv array.
 */
static void
add_arg(struct cmd *cmd, char *arg)
{
	int i;

	/* Find the end of the argv array. */
	for (i = 0; cmd->argv[i] != NULL; i++)
		;

	/* Reallocate to add the new argument. */
	cmd->argv = realloc(cmd->argv,
	    (size_t)(i + 2) * sizeof(char *));
	if (cmd->argv != NULL) {
		cmd->argv[i] = arg;
		cmd->argv[i + 1] = NULL;
	}
}

/*
 * Free a command tree recursively.
 */
void
cmd_free(struct cmd *cmd)
{
	struct redirect *r, *rnext;

	if (cmd == NULL)
		return;

	cmd_free(cmd->left);
	cmd_free(cmd->right);
	cmd_free(cmd->else_cmd);

	if (cmd->argv != NULL) {
		for (int i = 0; cmd->argv[i] != NULL; i++)
			free(cmd->argv[i]);
		free(cmd->argv);
	}

	free(cmd->var);
	free(cmd->here_word);

	if (cmd->words != NULL) {
		for (int i = 0; cmd->words[i] != NULL; i++)
			free(cmd->words[i]);
		free(cmd->words);
	}

	for (r = cmd->redirects; r != NULL; r = rnext) {
		rnext = r->next;
		redirect_free(r);
	}

	free(cmd);
}

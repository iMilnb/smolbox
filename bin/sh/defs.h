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

#ifndef _SH_DEFS_H_
#define _SH_DEFS_H_

#include <sys/types.h>
#include <sys/wait.h>

#include <stdbool.h>
#include <stddef.h>

/*
 * Command tree node types.
 */
enum node_type {
	N_CMD,		/* simple command */
	N_PIPE,		/* command | command */
	N_LIST,		/* command ; command */
	N_AND,		/* command && command */
	N_OR,		/* command || command */
	N_IF,		/* if cmd then cmd [else cmd] fi */
	N_WHILE,	/* while cmd do cmd done */
	N_FOR,		/* for var in words do cmd done */
	N_SUBSHELL,	/* ( command ) */
	N_BACKGROUND,	/* command & */
	N_GROUP,	/* { command; } */
};

/*
 * I/O redirection types.
 */
enum redirect_type {
	REDIR_IN,		/* <file */
	REDIR_OUT,		/* >file */
	REDIR_APPEND,		/* >>file */
	REDIR_INOUT,		/* <>file */
	REDIR_DUP_IN,		/* <&fd */
	REDIR_DUP_OUT,		/* >&fd */
	REDIR_HEREDOC,		/* <<word */
};

/*
 * I/O redirection node.
 */
struct redirect {
	enum redirect_type	type;
	char			*arg;		/* filename or heredoc delimiter */
	int			fd;		/* for dup redirects */
	struct redirect		*next;
};

/*
 * Command tree node.
 */
struct cmd {
	enum node_type		type;
	struct cmd		*left;		/* first/condition command */
	struct cmd		*right;		/* second/body command */
	struct cmd		*else_cmd;	/* else branch (N_IF) */
	char			**argv;		/* command arguments (N_CMD) */
	struct redirect		*redirects;	/* I/O redirections */
	char			*var;		/* for variable (N_FOR) */
	char			**words;		/* for loop words (N_FOR) */
	char			*here_word;	/* heredoc delimiter (internal) */
};

/*
 * Variable hash table entry.
 */
struct variable {
	char			*name;
	char			*value;
	bool			exported;
	struct variable		*next;
};

/*
 * Token types for the lexer.
 */
enum token_type {
	TOK_WORD,
	TOK_PIPE,		/* | */
	TOK_SEMI,		/* ; */
	TOK_AMPAMP,		/* && */
	TOK_AMP,		/* & */
	TOK_BARBAR,		/* || */
	TOK_LT,			/* < */
	TOK_GT,			/* > */
	TOK_LTGT,		/* <> */
	TOK_LTAMP,		/* <& */
	TOK_GTAMP,		/* >& */
	TOK_GTGT,		/* >> */
	TOK_LPAREN,		/* ( */
	TOK_RPAREN,		/* ) */
	TOK_LBRACE,		/* { */
	TOK_RBRACE,		/* } */
	TOK_EOF,
	TOK_NEWLINE,
};

struct token {
	enum token_type	type;
	char			*word;		/* value for TOK_WORD */
};

/*
 * Lexer state.
 */
struct lexer {
	char			*input;
	size_t			pos;
	size_t			len;
	struct token		cur;
};

/*
 * Global shell state.
 */
extern int			exit_status;
extern pid_t			shell_pid;
extern bool			interactive;
extern bool			executing_script;
extern bool			xtrace;
extern const char		*shell_name;
extern int			parse_error;	/* set by parser on syntax error */
extern bool			noexec;		/* -n: parse but do not execute */

/* ---- sh.h ---- */
void	 done(int);
void	 sh_loop(void);
int	 main_sh(int, char *[]);

/* ---- lexer.h ---- */
void	 lexer_init(struct lexer *, const char *);
void	 lexer_next(struct lexer *);
void	 lexer_free(struct lexer *);

/* ---- parser.h ---- */
struct cmd *parse_command(struct lexer *, bool *);
struct cmd *parse_list(struct lexer *, int);
void	 cmd_free(struct cmd *);

/* ---- exec.h ---- */
int	 execute(struct cmd *);
void	 redirect_setup(struct redirect *);
void	 redirect_cleanup(void);

/* ---- builtin.h ---- */
int	 is_builtin(const char *);
int	 run_builtin(char *[]);
void	 builtin_init(void);

/* ---- var.h ---- */
void	 var_init(void);
char	*var_get(const char *);
void	 var_set(const char *, const char *);
void	 var_unset(const char *);
void	 var_export(const char *);
void	 var_set_all_export(bool);
char	**var_to_env(void);
void	 var_free_env(char **);
void	 var_cleanup(void);
char	*expand(const char *);
char	**expand_args(char **);
void	 var_set_positional(int, char **);
int	 var_positional_count(void);
char	*var_positional(int);

/* ---- glob.h ---- */
char	**glob_expand(const char *, int *);
void	 glob_free(char **);

/* ---- redirect.h ---- */
struct redirect *redirect_new(enum redirect_type, char *, int);
void	 redirect_list_free(struct redirect *);
void	 redirect_apply(struct redirect *);
void	 redirect_restore(void);
void	 redirect_free(struct redirect *);

/* ---- exec.h (node constructors) ---- */
struct cmd *mklist(struct cmd *, struct cmd *);
struct cmd *mkand(struct cmd *, struct cmd *);
struct cmd *mkor(struct cmd *, struct cmd *);
struct cmd *mkpipe(struct cmd *, struct cmd *);
struct cmd *mkbg(struct cmd *);
int	 exec_for(struct cmd *);

#endif /* _SH_DEFS_H_ */

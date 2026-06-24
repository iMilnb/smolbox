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

static char	*alloc_token(struct lexer *, size_t);
static void	 skip_whitespace(struct lexer *);
static void	 read_word(struct lexer *);
static void	 read_operator(struct lexer *);

/*
 * Initialize the lexer with an input string.
 */
void
lexer_init(struct lexer *lx, const char *input)
{

	lx->input = (char *)input;
	lx->pos = 0;
	lx->len = strlen(input);
	lx->cur.type = TOK_EOF;
	lx->cur.word = NULL;
}

/*
 * Advance to the next token.
 */
void
lexer_next(struct lexer *lx)
{

	if (lx->cur.word != NULL) {
		free(lx->cur.word);
		lx->cur.word = NULL;
	}

	if (lx->pos >= lx->len) {
		lx->cur.type = TOK_EOF;
		return;
	}

	skip_whitespace(lx);

	if (lx->pos >= lx->len) {
		lx->cur.type = TOK_EOF;
		return;
	}

	switch (lx->input[lx->pos]) {
	case '#':
		/* Comment: skip to end of line. */
		while (lx->pos < lx->len && lx->input[lx->pos] != '\n')
			lx->pos++;
		if (lx->pos < lx->len)
			lx->pos++;
		lexer_next(lx);
		return;
	case '\'':
	case '"':
	case '\\':
	case '$':
	case '^':
		read_word(lx);
		break;
	case '|':
	case '&':
	case '<':
	case '>':
	case ';':
	case '(':
	case ')':
	case '{':
	case '}':
		read_operator(lx);
		break;
	case '\n':
		lx->pos++;
		lx->cur.type = TOK_NEWLINE;
		break;
	default:
		read_word(lx);
		break;
	}
}

/*
 * Free any allocated token data.
 */
void
lexer_free(struct lexer *lx)
{

	if (lx->cur.word != NULL) {
		free(lx->cur.word);
		lx->cur.word = NULL;
	}
}

/*
 * Allocate memory for a token substring.
 */
static char *
alloc_token(struct lexer *lx, size_t len)
{
	char *s;

	s = malloc(len + 1);
	if (s == NULL)
		return NULL;
	(void)strlcpy(s, lx->input + lx->pos - len, len + 1);
	return s;
}

/*
 * Skip whitespace characters.
 */
static void
skip_whitespace(struct lexer *lx)
{

	while (lx->pos < lx->len &&
	    (lx->input[lx->pos] == ' ' || lx->input[lx->pos] == '\t'))
		lx->pos++;
}

/*
 * Read a word token, handling quotes and escapes.
 */
static void
read_word(struct lexer *lx)
{
	size_t		start = lx->pos;
	char		quote = 0;

	while (lx->pos < lx->len) {
		char c = lx->input[lx->pos];

		switch (c) {
		case '\'':
			if (quote == '"') {
				lx->pos++;
				break;
			}
			if (quote == 0) {
				quote = '\'';
				lx->pos++;
				break;
			}
			/* End of single-quoted string. */
			quote = 0;
			lx->pos++;
			/* Continue reading rest of word. */
			continue;
		case '"':
			if (quote == '\'') {
				lx->pos++;
				break;
			}
			if (quote == 0) {
				quote = '"';
				lx->pos++;
				break;
			}
			/* End of double-quoted string. */
			quote = 0;
			lx->pos++;
			continue;
		case '\\':
			if (quote == '\'' || quote == '"') {
				lx->pos++;
				if (lx->pos < lx->len)
					lx->pos++;
				else
					lx->pos++;
				break;
			}
			/* Outside quotes: backslash escapes next char. */
			lx->pos += 2;
			break;
		case '$':
			lx->pos++;
			break;
		case ' ':
		case '\t':
			if (quote == 0)
				goto done;
			lx->pos++;
			break;
		case '|':
		case '&':
		case ';':
		case '<':
		case '>':
		case '\n':
			if (quote == 0)
				goto done;
			lx->pos++;
			break;
		case '(':
		case ')':
		case '{':
		case '}':
			if (quote == 0)
				goto done;
			lx->pos++;
			break;
		default:
			lx->pos++;
			break;
		}
	}

done:
	lx->cur.type = TOK_WORD;
	lx->cur.word = alloc_token(lx, lx->pos - start);
}

/*
 * Read an operator token.
 */
static void
read_operator(struct lexer *lx)
{
	char	c = lx->input[lx->pos];

	lx->cur.word = NULL;

	switch (c) {
	case '|':
		lx->pos++;
		if (lx->pos < lx->len && lx->input[lx->pos] == '|') {
			lx->pos++;
			lx->cur.type = TOK_BARBAR;
		} else {
			lx->cur.type = TOK_PIPE;
		}
		break;
	case '&':
		lx->pos++;
		if (lx->pos < lx->len && lx->input[lx->pos] == '&') {
			lx->pos++;
			lx->cur.type = TOK_AMPAMP;
		} else {
			lx->cur.type = TOK_AMP;
		}
		break;
	case '<':
		lx->pos++;
		if (lx->pos < lx->len && lx->input[lx->pos] == '&') {
			lx->pos++;
			lx->cur.type = TOK_LTAMP;
		} else if (lx->pos < lx->len && lx->input[lx->pos] == '>') {
			lx->pos++;
			lx->cur.type = TOK_LTGT;
		} else {
			lx->cur.type = TOK_LT;
		}
		break;
	case '>':
		lx->pos++;
		if (lx->pos < lx->len && lx->input[lx->pos] == '&') {
			lx->pos++;
			lx->cur.type = TOK_GTAMP;
		} else if (lx->pos < lx->len && lx->input[lx->pos] == '>') {
			lx->pos++;
			lx->cur.type = TOK_GTGT;
		} else {
			lx->cur.type = TOK_GT;
		}
		break;
	case ';':
		lx->pos++;
		lx->cur.type = TOK_SEMI;
		break;
	case '(':
		lx->pos++;
		lx->cur.type = TOK_LPAREN;
		break;
	case ')':
		lx->pos++;
		lx->cur.type = TOK_RPAREN;
		break;
	case '{':
		lx->pos++;
		lx->cur.type = TOK_LBRACE;
		break;
	case '}':
		lx->pos++;
		lx->cur.type = TOK_RBRACE;
		break;
	default:
		lx->pos++;
		lx->cur.type = TOK_WORD;
		lx->cur.word = malloc(2);
		if (lx->cur.word != NULL) {
			lx->cur.word[0] = c;
			lx->cur.word[1] = '\0';
		}
		break;
	}
}

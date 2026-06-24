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

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defs.h"

static int	 glob_match(const char *, const char *);
static int	 glob_has_meta(const char *);

/*
 * Expand a glob pattern, returning an array of matching filenames.
 * The count of matches is stored in *match_count.
 */
char **
glob_expand(const char *pattern, int *match_count)
{
	DIR			*dir;
	struct dirent	*de;
	char		**matches;
	int		 count = 0, cap = 16;
	const char	*basename;
	const char	*dirpath;

	*match_count = 0;

	if (!glob_has_meta(pattern))
		return NULL;

	/* Split pattern into directory and basename. */
	basename = strrchr(pattern, '/');
	if (basename != NULL) {
		dirpath = pattern;
		basename++;
	} else {
		dirpath = ".";
		basename = pattern;
	}

	matches = calloc((size_t)cap, sizeof(char *));
	if (matches == NULL)
		return NULL;

	dir = opendir(dirpath);
	if (dir == NULL)
		return NULL;

	while ((de = readdir(dir)) != NULL) {
		if (glob_match(basename, de->d_name)) {
			if (count >= cap) {
				cap *= 2;
				matches = realloc(matches,
				    (size_t)cap * sizeof(char *));
				if (matches == NULL) {
					(void)closedir(dir);
					return NULL;
				}
			}
			matches[count] = strdup(de->d_name);
			count++;
		}
	}
	(void)closedir(dir);

	if (count == 0)
		return NULL;

	matches[count] = NULL;
	*match_count = count;
	return matches;
}

/*
 * Free a glob result array.
 */
void
glob_free(char **matches)
{
	int i;

	if (matches == NULL)
		return;
	for (i = 0; matches[i] != NULL; i++)
		free(matches[i]);
	free(matches);
}

/*
 * Check if a pattern contains any metacharacters.
 */
static int
glob_has_meta(const char *pattern)
{
	const char *p;

	for (p = pattern; *p != '\0'; p++) {
		if (*p == '*' || *p == '?' || *p == '[')
			return 1;
	}
	return 0;
}

/*
 * Match a filename against a glob pattern.
 * Supports: *, ?, [abc], [a-z]
 */
static int
glob_match(const char *pattern, const char *name)
{
	const char *p, *n;

	for (p = pattern, n = name; *p != '\0'; p++, n++) {
		switch (*p) {
		case '*':
			/* Skip consecutive asterisks. */
			while (*p == '*')
				p++;
			if (*p == '\0')
				return 1;

			/* Try matching at each position. */
			for (n--; *n != '\0'; n++) {
				if (glob_match(p, n))
					return 1;
			}
			return 0;
		case '?':
			if (*n == '\0')
				return 0;
			break;
		case '[':
			{
				int found = 0;

				p++;
				while (*p != '\0' && *p != ']') {
					if (*p == *n) {
						found = 1;
						break;
					}
					p++;
				}
				if (!found || *p == '\0')
					return 0;
				p++; /* Skip ']'. */
				break;
			}
		default:
			if (*p != *n)
				return 0;
			break;
		}
	}

	return *n == '\0';
}

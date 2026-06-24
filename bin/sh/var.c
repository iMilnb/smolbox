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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defs.h"

#define	VAR_HASH_SIZE	64

extern int	 exit_status;
extern pid_t	 shell_pid;

static struct variable	*var_table[VAR_HASH_SIZE];
static char		*positional[256];
static int		 positional_count = 0;
static char		*var_env[1024];
static int		 var_env_count = 0;
static bool		 all_export = false;

static unsigned int	var_hash(const char *);
static struct variable	*var_find(const char *);
static void		 var_add(const char *, const char *);
static void		 var_rebuild_env(void);

/*
 * Initialize the variable table from the environment.
 */
void
var_init(void)
{
	char **ep;
	int i;

	for (i = 0; i < VAR_HASH_SIZE; i++)
		var_table[i] = NULL;

	for (i = 0; i < 256; i++)
		positional[i] = NULL;

	var_env_count = 0;

	/* Import environment variables. */
	extern char **environ;
	for (ep = environ; *ep != NULL; ep++) {
		char *eq, *name, *value;

		eq = strchr(*ep, '=');
		if (eq == NULL)
			continue;
		name = strndup(*ep, (size_t)(eq - *ep));
		value = eq + 1;
		var_add(name, value);
		free(name);
	}
}

/*
 * Get a variable's value.
 */
char *
var_get(const char *name)
{
	struct variable *vp;

	if (name == NULL)
		return NULL;

	/* Special variables. */
	if (name[0] == '?' && name[1] == '\0') {
		char buf[16];

		(void)snprintf(buf, sizeof(buf), "%d", exit_status);
		return strdup(buf);
	}
	if (name[0] == '$' && name[1] == '\0') {
		char buf[16];

		(void)snprintf(buf, sizeof(buf), "%ld", (long)shell_pid);
		return strdup(buf);
	}
	if (name[0] == '!' && name[1] == '\0')
		return strdup(0);

	/* Positional parameters. */
	if (name[0] >= '1' && name[0] <= '9' && name[1] == '\0') {
		int idx = name[0] - '1';

		return positional[idx] != NULL ?
		    positional[idx] : "";
	}
	if (name[0] == '@' && name[1] == '\0') {
		/* Return all positional parameters as one string. */
		char buf[4096];
		int i, first = 1;

		buf[0] = '\0';
		for (i = 0; i < positional_count && i < 256; i++) {
			if (positional[i] == NULL)
				break;
			if (!first)
				(void)strlcat(buf, " ", sizeof(buf));
			(void)strlcat(buf, positional[i], sizeof(buf));
			first = 0;
		}
		return strdup(buf);
	}
	if (name[0] == '#' && name[1] == '\0') {
		char buf[16];

		(void)snprintf(buf, sizeof(buf), "%d", positional_count);
		return strdup(buf);
	}

	/* Look up in hash table. */
	vp = var_find(name);
	if (vp != NULL && vp->value != NULL)
		return vp->value;

	return NULL;
}

/*
 * Set a variable.
 */
void
var_set(const char *name, const char *value)
{
	struct variable *vp;

	if (name == NULL)
		return;

	vp = var_find(name);
	if (vp != NULL) {
		free(vp->value);
		vp->value = value ? strdup(value) : NULL;
		var_rebuild_env();
		return;
	}

	var_add(name, value);
}

/*
 * Unset a variable.
 */
void
var_unset(const char *name)
{
	struct variable	*vp, **prev;
	unsigned int		bucket;

	bucket = var_hash(name);
	prev = &var_table[bucket];

	for (vp = var_table[bucket]; vp != NULL; vp = *prev) {
		if (strcmp(vp->name, name) == 0) {
			*prev = vp->next;
			free(vp->name);
			free(vp->value);
			free(vp);
			var_rebuild_env();
			return;
		}
		prev = &vp->next;
	}
}

/*
 * Mark a variable for export.
 */
void
var_export(const char *name)
{
	struct variable *vp;

	vp = var_find(name);
	if (vp != NULL) {
		vp->exported = true;
		var_rebuild_env();
	}
}

/*
 * Set all variables to be exported.
 */
void
var_set_all_export(bool flag)
{

	all_export = flag;
}

/*
 * Build the environment array for exec.
 */
char **
var_to_env(void)
{

	var_rebuild_env();
	return var_env;
}

/*
 * Free the environment array.
 */
void
var_free_env(char **env)
{
	int i;

	if (env == NULL)
		return;
	for (i = 0; env[i] != NULL; i++)
		free(env[i]);
	free(env);
}

/*
 * Clean up all variables.
 */
void
var_cleanup(void)
{
	struct variable *vp, *vnext;
	int i;

	for (i = 0; i < VAR_HASH_SIZE; i++) {
		for (vp = var_table[i]; vp != NULL; vp = vnext) {
			vnext = vp->next;
			free(vp->name);
			free(vp->value);
			free(vp);
		}
		var_table[i] = NULL;
	}

	for (i = 0; i < 256; i++)
		free(positional[i]);

	for (i = 0; i < var_env_count; i++)
		free(var_env[i]);
}

/*
 * Expand variable references in a string.
 * Handles: $var, ${var}, $?, $$, $!, $0-$9, $@, $#
 */
char *
expand(const char *input)
{
	char	*result;
	const char	*src;
	char	*dst;
	size_t		len;

	if (input == NULL)
		return strdup("");

	len = strlen(input);
	result = malloc(len * 4 + 1); /* Worst case expansion. */
	if (result == NULL)
		return NULL;

	src = input;
	dst = result;

	while (*src != '\0') {
		if (*src == '\\' && src[1] != '\0') {
			src++;
			*dst++ = *src++;
			continue;
		}
		if (*src != '$') {
			*dst++ = *src++;
			continue;
		}

		src++;
		if (*src == '{') {
			/* ${var} form. */
			char name[256];
			int i = 0;

			src++;
			while (*src != '\0' && *src != '}' && i < 255)
				name[i++] = *src++;
			name[i] = '\0';
			if (*src == '}')
				src++;

			char *val = var_get(name);
			if (val != NULL) {
				(void)strlcpy(dst, val, (size_t)(result + len * 4 + 1 - dst));
				dst += strlen(dst);
				free(val);
			}
			continue;
		}

		/* $var form (no braces). */
		if (*src == '\0') {
			*dst++ = '$';
			continue;
		}

		/* Read full variable name (alphanumeric + underscore). */
		{
			char	name[256];
			int		i = 0;

			while (*src != '\0' && i < 255 &&
			    (isalnum((unsigned char)*src) || *src == '_'))
				name[i++] = *src++;
			name[i] = '\0';

			if (i == 0) {
				*dst++ = '$';
				continue;
			}

			char *val = var_get(name);
			if (val != NULL) {
				(void)strlcpy(dst, val,
				    (size_t)(result + len * 4 + 1 - dst));
				dst += strlen(dst);
				free(val);
			} else {
				*dst++ = '$';
				(void)strlcpy(dst, name,
				    (size_t)(result + len * 4 + 1 - dst));
				dst += strlen(dst);
			}
			continue;
		}
	}

	*dst = '\0';
	return result;
}

/*
 * Expand all arguments in an array.
 */
char **
expand_args(char **argv)
{
	char **result;
	int i, count = 0;

	if (argv == NULL)
		return NULL;

	/* Count arguments. */
	for (i = 0; argv[i] != NULL; i++)
		count++;

	result = calloc((size_t)(count + 1), sizeof(char *));
	if (result == NULL)
		return NULL;

	for (i = 0; i < count; i++)
		result[i] = expand(argv[i]);

	return result;
}

/*
 * Set positional parameters from an argument array.
 */
void
var_set_positional(int argc, char **argv)
{
	int i;

	for (i = 0; i < 256; i++)
		free(positional[i]);
	positional_count = 0;

	for (i = 0; argc > 0 && i < 255; i++) {
		positional[i] = strdup(*argv);
		argv++;
		argc--;
		positional_count++;
	}
	positional[i] = NULL;
}

/*
 * Compute hash for a variable name.
 */
static unsigned int
var_hash(const char *name)
{
	unsigned int hash = 5381;
	int c;

	while ((c = *name++) != '\0')
		hash = ((hash << 5) + hash) + (unsigned int)c;

	return hash % VAR_HASH_SIZE;
}

/*
 * Find a variable in the hash table.
 */
static struct variable *
var_find(const char *name)
{
	struct variable *vp;
	unsigned int bucket;

	bucket = var_hash(name);
	for (vp = var_table[bucket]; vp != NULL; vp = vp->next)
		if (strcmp(vp->name, name) == 0)
			return vp;
	return NULL;
}

/*
 * Add a new variable to the hash table.
 */
static void
var_add(const char *name, const char *value)
{
	struct variable *vp;
	unsigned int bucket;

	vp = calloc(1, sizeof(*vp));
	if (vp == NULL)
		return;

	vp->name = strdup(name);
	vp->value = value ? strdup(value) : NULL;
	vp->exported = all_export;

	bucket = var_hash(name);
	vp->next = var_table[bucket];
	var_table[bucket] = vp;

	var_rebuild_env();
}

/*
 * Rebuild the environment array.
 */
static void
var_rebuild_env(void)
{
	struct variable *vp, *vnext;
	int i, idx;

	for (i = 0; i < var_env_count; i++)
		free(var_env[i]);
	var_env_count = 0;

	idx = 0;
	for (i = 0; i < VAR_HASH_SIZE && idx < 1022; i++) {
		for (vp = var_table[i]; vp != NULL && idx < 1022; vp = vnext) {
			vnext = vp->next;
			if (vp->value != NULL) {
				char *env;

				env = malloc(strlen(vp->name) +
				    strlen(vp->value) + 2);
				if (env != NULL) {
					(void)snprintf(env,
					    strlen(vp->name) + strlen(vp->value) + 2,
					    "%s=%s", vp->name, vp->value);
					var_env[idx++] = env;
				}
			}
		}
	}
	var_env[idx] = NULL;
	var_env_count = idx;
}

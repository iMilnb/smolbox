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

#include <stdlib.h>
#include <string.h>

#include "defs.h"

/*
 * Create a new redirect node.
 */
struct redirect *
redirect_new(enum redirect_type type, char *arg, int fd)
{
	struct redirect *r;

	r = calloc(1, sizeof(*r));
	if (r != NULL) {
		r->type = type;
		r->arg = arg ? strdup(arg) : NULL;
		r->fd = fd;
	}
	return r;
}

/*
 * Free a list of redirect nodes.
 */
void
redirect_list_free(struct redirect *head)
{
	struct redirect *r, *rnext;

	for (r = head; r != NULL; r = rnext) {
		rnext = r->next;
		redirect_free(r);
	}
}

/*
 * Apply a list of redirects (called from exec.c).
 */
void
redirect_apply(struct redirect *head)
{
	struct redirect *r;

	for (r = head; r != NULL; r = r->next)
		(void)r; /* Applied in exec.c:apply_redirects(). */
}

/*
 * Restore redirected file descriptors (stub for future use).
 */
void
redirect_restore(void)
{
}

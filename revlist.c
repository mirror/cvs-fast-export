/*
 *  Copyright © 2006 Keith Packard <keithp@keithp.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 */

#include "cvs.h"

/*
 * A revision list is the history for an entire RCS/CVS repository.
 * These are utility functions used by both the analysis phase and
 * the DAG merge code.
 */

rev_ref *
rev_list_add_head(head_list *rl, cvs_commit *commit, 
		  const char *name, const int degree)
/* decorate a commit list with a named head reference */
{
    rev_ref	*r;
    rev_ref	**list = &rl->heads;

    while (*list)
	list = &(*list)->next;
    r = xcalloc(1, sizeof(rev_ref), "adding head reference");
    r->commit = commit;
    r->ref_name = name;
    r->next = *list;
    r->degree = degree;
    *list = r;
    return r;
}

void
rev_list_set_tail(head_list *rl)
/* set tail bits so we can walk through each commit in a revlist exactly once */
{
    rev_ref	*head;
    cvs_commit	*c;

    /* 
     * Set tail bit true where traversal should stop in order to avoid
     * multiple visits to the same commit.
     */ 
    for (head = rl->heads; head; head = head->next) {
	flag tail = true;
	/* set tail on each previously visited head reference */
	if (head->commit && head->commit->refcount > 0) {
	    head->tail = tail;
	    tail = false;
	}
	for (c = head->commit; c; c = c->parent) {
	    /* set tail on the child of the first join commit on this branch */
	    if (tail && c->parent && c->refcount < c->parent->refcount) {
		c->tail = true;
		tail = false;
	    }
	    if (c->refcount++ >= MAX_BRANCHCOUNT_T)
		fatal_error("too many branches, widen branchcount_t");
	}
    }
}

#ifdef __UNUSED__
bool
rev_list_validate(head_list *rl)
/* entry point - validate the output */
{
    rev_ref	*h;
    cvs_commit	*c;
    for (h = rl->heads; h; h = h->next) {
	if (h->tail)
	    continue;
	for (c = h->commit; c && c->parent; c = c->parent) {
	    if (c->tail)
		break;
	    if (time_compare(c->date, c->parent->date) < 0)
		return false;
	}
    }

    return true;
}
#endif

/* end */

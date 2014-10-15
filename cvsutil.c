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
#include <assert.h>

node_t *
cvs_find_version(const cvs_file *cvs, const cvs_number *number)
/* find the file version associated with the specified CVS release number */
{
    cvs_version *cv;
    cvs_version	*nv = NULL;

    for (cv = cvs->gen.versions; cv; cv = cv->next) {
	if (cvs_same_branch(number, &cv->number) &&
	    cvs_number_compare(&cv->number, number) > 0 &&
	    (!nv || cvs_number_compare(&nv->number, &cv->number) > 0))
	    nv = cv;
    }
    return nv ? nv->node : NULL;
}

static void
cvs_symbol_free(cvs_symbol *symbol)
/* discard a symbol and its storage */
{
    cvs_symbol	*s;

    while ((s = symbol)) {
	symbol = s->next;
	free(s);
    }
}

static void
cvs_branch_free(cvs_branch *branch)
/* discard a branch and its storage */
{
    cvs_branch	*b;

    while ((b = branch)) {
	branch = b->next;
	free(b);
    }
}

static void
cvs_version_free(cvs_version *version)
/* discard a version and its storage */
{
    cvs_version	*v;

    while ((v = version)) {
	version = v->next;
	cvs_branch_free(v->branches);
	free(v);
    }
}

static void
cvs_patch_free(cvs_patch *patch)
/* discard a patch and its storage */
{
    cvs_patch	*v;

    while ((v = patch)) {
	patch = v->next;
	free(v);
    }
}

void
generator_free(generator_t *gen)
{
    cvs_version_free(gen->versions);
    cvs_patch_free(gen->patches);
    clean_hash(&gen->nodehash);
}

void
cvs_file_free(cvs_file *cvs)
/* discard a file object and its storage */
{
    cvs_symbol_free(cvs->symbols);
    rbtree_free(cvs->symbols_by_name);
    free(cvs);
}

/* end */

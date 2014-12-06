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

/*
 * Dump functions for graphing and debug instrumentation.
 */

#include "cvs.h"
#include <unistd.h>
#ifdef ORDERDEBUG
#include "revdir.h"
#endif /*ORDERDEBUG */

void
dump_number_file(FILE *f, const char *name, const cvs_number *number)
/* dump a filename/CVS-version pair to a specified file pointer */
{
    char buf[BUFSIZ];

    fputs(stringify_revision(name, " ", number, buf, sizeof buf), f);
}

void
dump_number(const char *name, const cvs_number *number)
/* dump a filename/CVS-version pair to standard output */
{
    dump_number_file(stdout, name, number);
}

#ifdef ORDERDEBUG
void
dump_git_commit(const git_commit *c, FILE *fp)
/* dump all delta/revision pairs associated with a gitspace commit */
{
    cvs_commit	*cc;
    revdir_iter *it = revdir_iter_alloc(&c->revdir);
    while((cc = revdir_iter_next(it))) {
	dump_number_file(fp, cc->master->name, cc->number);
	printf(" ");
    }
    fputs("\n", fp);
}
#endif /* ORDERDEBUG */

/* end */

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
 *
 * Warning, some of the unused code mayy have bitrotted 
 * when cvs_commit and git_commit were separated.
 */

#include "cvs.h"
#include <unistd.h>

void
dump_number_file(FILE *f, const char *name, cvs_number *number)
/* dump a filename/CVS-version pair to a specified file pointer */
{
    char buf[BUFSIZ];

    fputs(stringify_revision(name, " ", number, buf, sizeof buf), f);
}

void
dump_number(const char *name, cvs_number *number)
/* dump a filename/CVS-version pair to standard output */
{
    dump_number_file(stdout, name, number);
}

#ifdef ORDERDEBUG
void
dump_git_commit(git_commit *c, FILE *fp)
/* dump all delta/revision pairs associated with a gitspace commit */
{
    rev_file	*f;
    int		i, j;

    for (i = 0; i < c->ndirs; i++) {
	rev_dir	*dir = c->dirs[i];
	
	for (j = 0; j < dir->nfiles; j++) {
	    f = dir->files[j];
	    dump_number_file(fp, f->name, &f->number);
	    printf(" ");
	}
    }
    fputs("\n", fp);
}
#endif /* ORDERDEBUG */

/* end */

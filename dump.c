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
dump_number_file(FILE *f, char *name, cvs_number *number)
/* dump a filename/CVS-version pair to a specified file pointer */
{
    fputs(stringify_revision(name, " ", number), f);
}

void
dump_number(char *name, cvs_number *number)
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

#ifdef __UNUSED__
void
dump_rev_head(rev_ref *h, FILE *fp)
/* dump all gitspace commits associated with the specified head */
{
    git_commit	*c;
    for (c = (git_commit *)h->commit; c; c = c->parent) {
	dump_git_commit(c, fp);
	if (c->tail)
	    break;
    }
}

void
dump_rev_list(rev_list *rl, FILE *fp)
/* dump an entire revision list */
{
    rev_ref	*h;

    for (h = rl->heads; h; h = h->next) {
	if (h->tail)
	    continue;
	dump_rev_head(h, fp);
    }
}

void
dump_rev_tree(rev_list *rl, FILE *fp)
{
    rev_ref	*h;
    rev_ref	*oh;
    cvs_commit	*c, *p;
    int		tail;

    printf("rev_list {\n");

    for (h = rl->heads; h; h = h->next) {
	if (h->tail)
	    continue;
	for (oh = rl->heads; oh; oh = oh->next) {
	    if (h->commit == oh->commit)
		fprintf(fp, "%s:\n", oh->name);
	}
	fprintf(fp, "\t{\n");
	tail = h->tail;
	for (c = h->commit; c; c = p) {
	    fprintf(fp, "\t\t%p ", c);
	    dump_log(stdout, c->log);
	    if (tail) {
		fprintf(fp, "\n\t\t...\n");
		break;
	    }
	    fprintf(fp, " {\n");
	    
	    p = c->parent;
#if 0
	    if (p && c->nfiles > 16) {
		rev_file	*ef, *pf;
		int		ei, pi;
		ei = pi = 0;
		while (ei < c->nfiles && pi < p->nfiles) {
		    ef = c->files[ei];
		    pf = p->files[pi];
		    if (ef != pf) {
			if (rev_file_later(ef, pf)) {
			    fprintf(fp, "+ ");
			    dump_number_file(stdout, ef->name, &ef->number);
			    ei++;
			} else {
			    fprintf(fp, "- ");
			    dump_number_file(stdout, pf->name, &pf->number);
			    pi++;
			}
			fprintf(fp, "\n");
		    } else {
			ei++;
			pi++;
		    }
		}
		while (ei < c->nfiles) {
		    ef = c->files[ei];
		    fprintf(fp, "+ ");
		    dump_number_file(stdout, ef->name, &ef->number);
		    ei++;
		    fprintf(fp, "\n");
		}
		while (pi < p->nfiles) {
		    pf = p->files[pi];
		    fprintf(fp, "- ");
		    dump_number_file(stdout, pf->name, &pf->number);
		    pi++;
		    fprintf(fp, "\n");
		}
	    } else {
		for (i = 0; i < c->nfiles; i++) {
		    fprintf(fp, "\t\t\t");
		    dump_number(c->files[i]->name, &c->files[i]->number);
		    fprintf(fp, "\n");
		}
	    }
#endif
	    fprintf(fp, "\t\t}\n");
	    tail = c->tail;
	}
	fprintf(fp, "\t}\n");
    }
    fprintf(fp, "}\n");
}
#endif /* __UNUSED__ */

/* end */

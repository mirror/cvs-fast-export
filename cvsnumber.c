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

#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))

bool
cvs_is_head(cvs_number *n)
/* is a specified CVS revision a branch head? */
{
    assert(n->c <= CVS_MAX_DEPTH); 
    return(n->c > 2 && (n->c & 1) == 0 && n->n[n->c-2] == 0);
}

bool
cvs_same_branch(cvs_number *a, cvs_number *b)
/* are two specified CVS revisions on the same branch? */
{
    cvs_number	t;
    int		i;
    int		n;

    if (a->c & 1) {
	t = *a;
	t.n[t.c++] = 0;
	return cvs_same_branch(&t, b);
    }
    if (b->c & 1) {
	t = *b;
	t.n[t.c++] = 0;
	return cvs_same_branch(a, &t);
    }
    if (a->c != b->c)
	return false;
    /*
     * Everything on x.y is trunk
     */
    if (a->c == 2)
	return true;
    n = a->c;
    for (i = 0; i < n - 1; i++) {
	int		an, bn;
	an = a->n[i];
	bn = b->n[i];
	/*
	 * deal with n.m.0.p branch numbering
	 */
	if (i == n - 2) {
	    if (an == 0) an = a->n[i+1];
	    if (bn == 0) bn = b->n[i+1];
	}
	if (an != bn)
	    return false;
    }
    return true;
}

int
cvs_number_compare(cvs_number *a, cvs_number *b)
/* total ordering for CVS revision numbers */
{
    int n = min(a->c, b->c);
    int i;

    for (i = 0; i < n; i++) {
	if (a->n[i] < b->n[i])
	    return -1;
	if (a->n[i] > b->n[i])
	    return 1;
    }
    if (a->c < b->c)
	return -1;
    if (a->c > b->c)
	return 1;
    return 0;
}

int
cvs_number_degree(cvs_number *n)
/* what is the degree of branchiness of the specified revision? */
{
    cvs_number	four;

    if (n->c < 4)
	return n->c;
    four = *n;
    four.c = 4;
    /*
     * Place vendor branch between trunk and other branches
     */
    if (cvs_is_vendor(&four))
	return n->c - 1;
    return n->c;
}

bool
cvs_is_trunk(cvs_number *number)
/* does the specified CVS release number describe a trunk revision? */
{
    return number->c == 2;
}

/*
 * Import branches are of the form 1.1.x where x is odd
 */
bool
cvs_is_vendor(cvs_number *number)
/* is the specified CVS release number on a vendor branch? */
{
    if (number->c != 4) return 0;
    if (number->n[0] != 1)
	return false;
    if (number->n[1] != 1)
	return false;
    if ((number->n[2] & 1) != 1)
	return false;
    return true;
}

char *
cvs_number_string(cvs_number *n, char *str, size_t maxlen)
/* return the human-readable representation of a CVS release number */
{
    char    r[CVS_MAX_DIGITS + 1];
    int	    i;

    str[0] = '\0';
    for (i = 0; i < n->c; i++) {
	snprintf(r, 10, "%d", n->n[i]);
	if (i > 0)
	    strcat(str, ".");
	if (strlen(str) + strlen(r) < maxlen -1)
	    strcat(str, r);
	else
	    fatal_error("revision string too long");
	
    }
    return str;
}

char *
stringify_revision(char *name, char *sep, cvs_number *number)
/* stringify a revision number */
{
    static char result[BUFSIZ];

    if (name != NULL)
    {
	if (strlen(name) >= sizeof(result) - strlen(sep) - 1)
	    fatal_error("filename too long");
	strncpy(result, name, sizeof(result) - strlen(sep) - 1);
	strcat(result, sep);
    }

    if (number)
	cvs_number_string(number,
			  result + strlen(result),
			  sizeof(result) - strlen(result));

    return result;
}

/* end */

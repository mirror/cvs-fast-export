/*
 *  Copyright © 2006 Keith Packard <keithp@keithp.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as 
 *  published by the Free Software Foundation.
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
 * Pack a collection of files into a space-efficient representation in
 * which directories are coalesced.
 */

#include "cvs.h"
#include "hash.h"
#include "revdir.h"

/*
 * Compare/order filenames, such that files in subdirectories
 * sort earlier than files in the parent
 *
 * Also sorts in the same order that git fast-export does
 * As it says, 'Handle files below a directory first, in case they are
 * all deleted and the directory changes to a file or symlink.'
 * Because this doesn't have to handle renames, just sort lexicographically
 *
 *    a/x < b/y < a < b
 */
int
path_deep_compare(const void *a, const void *b)
{
    const char *af = (const char *)a;
    const char *bf = (const char *)b;
    const char *aslash;
    const char *bslash;
    int compar;

    /* short circuit */
    if (af == bf) 
        return 0;

    compar = strcmp(af, bf);

    /*
     * strcmp() will suffice, except for this case:
     *
     *   p/p/b/x/x < p/p/a
     *
     * In the comments below,
     *   ? is a string without slashes
     *  ?? is a string that may contain slashes
     */

    if (compar == 0)
        return 0;		/* ?? = ?? */

    aslash = strrchr(af, '/');
    bslash = strrchr(bf, '/');

    if (!aslash && !bslash)
	return compar;		/*    ? ~ ?    */
    if (!aslash) return +1;	/*    ? > ??/? */
    if (!bslash) return -1;     /* ??/? < ?    */


    /*
     * If the final slashes are at the same position, then either
     * both paths are leaves of the same directory, or they
     * are totally different paths. Both cases are satisfied by
     * normal lexicographic sorting:
     */
    if (aslash - af == bslash - bf)
	return compar;		/* ??/? ~ ??/? */


    /*
     * Must find the case where the two paths share a common
     * prefix (p/p).
     */
    if (aslash - af < bslash - bf) {
	if (bf[aslash - af] == '/' && memcmp(af, bf, aslash - af) == 0) {
	    return +1;		/* p/p/??? > p/p/???/? */
	}
    } else {
	if (af[bslash - bf] == '/' && memcmp(af, bf, bslash - bf) == 0) {
	    return -1;		/* ???/???/? ~ ???/??? */
	}
    }
    return compar;
}

int
compare_cvs_commit(const void *a, const void *b)
{
    cvs_commit **ap = (cvs_commit **)a;
    cvs_commit **bp = (cvs_commit **)b;
    const char *af = (*ap)->master->name;
    const char *bf = (*bp)->master->name;

#ifdef ORDERDEBUG
    warn("Comparing %s with %s\n", af, bf);
#endif /* ORDERDEBUG */

    return path_deep_compare(af, bf);
}

static bool
dir_is_ancestor(const master_dir *child, const master_dir *ancestor)
{
   while ((child = child->parent))
    if (child == ancestor) {
        return true;
    }
   return false;
 }


#ifdef TREEPACK
#include "treepack.c"
#else
#include "dirpack.c"
#endif


// end


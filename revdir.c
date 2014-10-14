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

#define REV_DIR_HASH	288361

typedef struct _rev_dir_hash {
    struct _rev_dir_hash    *next;
    unsigned long	    hash;
    rev_dir		    dir;
} rev_dir_hash;

static rev_dir_hash	*buckets[REV_DIR_HASH];

static 
unsigned long hash_files(rev_file **files, const int nfiles)
/* hash a file list so we can recognize it cheaply */
{
    unsigned long   h = 0;
    int		    i;

    for (i = 0; i < nfiles; i++)
	h = ((h << 1) | (h >> (sizeof(h) * 8 - 1))) ^ (unsigned long) files[i];
    return h;
}

static rev_dir *
rev_pack_dir(rev_file **files, const int nfiles)
/* pack a collection of file revisions for space efficiency */
{
    unsigned long   hash = hash_files(files, nfiles);
    rev_dir_hash    **bucket = &buckets[hash % REV_DIR_HASH];
    rev_dir_hash    *h;

    /* avoid packing a file list if we've done it before */ 
    for (h = *bucket; h; h = h->next) {
	if (h->hash == hash && h->dir.nfiles == nfiles &&
	    !memcmp(files, h->dir.files, nfiles * sizeof(rev_file *)))
	{
	    return &h->dir;
	}
    }
    h = xmalloc(sizeof(rev_dir_hash) + nfiles * sizeof(rev_file *),
		 __func__);
    h->next = *bucket;
    *bucket = h;
    h->hash = hash;
    h->dir.nfiles = nfiles;
    memcpy(h->dir.files, files, nfiles * sizeof(rev_file *));
    return &h->dir;
}

/*
 * Compare/order filenames, such that files in subdirectories
 * sort earlier than files in the parent
 *
 *    a/x < b/y < a < b
 */
int path_deep_compare(const void *a, const void *b)
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

static int compare_rev_file(const void *a, const void *b)
{
    rev_file **ap = (rev_file **)a;
    rev_file **bp = (rev_file **)b;
    const char *af = (*ap)->master->name;
    const char *bf = (*bp)->master->name;

#ifdef ORDERDEBUG
    fprintf(stderr, "Comparing %s with %s\n", af, bf);
#endif /* ORDERDEBUG */

    return path_deep_compare(af, bf);
}


static int	    sds = 0;
static rev_dir **rds = NULL;

static void
rds_put(const int index, rev_dir *rd)
/* puts an entry into the rds buffer, growing if needed */
{
    if (sds == 0) {
	sds = 128;
	rds = xmalloc(sds * sizeof(rev_dir *), __func__);
    } else {
	while (sds <= index) {
	    sds *= 2;
	}
	rds = xrealloc(rds, sds * sizeof(rev_dir *), __func__);
    }
    rds[index] = rd;
}

/* entry points begin here */

void
rev_free_dirs(void)
{
    unsigned long   hash;

    for (hash = 0; hash < REV_DIR_HASH; hash++) {
	rev_dir_hash    **bucket = &buckets[hash];
	rev_dir_hash	*h;

	while ((h = *bucket)) {
	    *bucket = h->next;
	    free(h);
	}
    }
    if (rds) {
	free(rds);
	rds = NULL;
	sds = 0;
    }
}

rev_dir **
rev_pack_files(rev_file **files, int nfiles, int *ndr)
{
    const char *dir = 0;
    char    *slash;
    int	    dirlen = 0;
    int	    i;
    int	    start = 0;
    int	    nds = 0;
    rev_dir *rd;
    
#ifdef ORDERDEBUG
    fputs("Packing:\n", stderr);
    {
	rev_file **s;

	for (s = files; s < files + nfiles; s++)
	    fprintf(stderr, "rev_file: %s\n", (*s)->master->name);
    }
#endif /* ORDERDEBUG */

    /*
     * The purpose of this sort is to rearrange the files in
     * directory-path order so we get the longest possible
     * runs of common directory prefixes, and thus maximum 
     * space-saving effect out of the next step.  This reduces
     * working-set size at the expense of the sort runtime.
     */
    qsort(files, nfiles, sizeof(rev_file *), compare_rev_file);

    /* pull out directories */
    for (i = 0; i < nfiles; i++) {
	if (!dir || strncmp(files[i]->master->name, dir, dirlen) != 0)
	{
	    if (i > start) {
		rd = rev_pack_dir(files + start, i - start);
		rds_put(nds++, rd);
	    }
	    start = i;
	    dir = files[i]->master->name;
	    slash = strrchr(dir, '/');
	    if (slash)
		dirlen = slash - dir;
	    else
		dirlen = 0;
	}
    }
    if (dir) {
        rd = rev_pack_dir(files + start, nfiles - start);
        rds_put(nds++, rd);
    }
    
    *ndr = nds;
    return rds;
}

// end

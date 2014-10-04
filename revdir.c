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
unsigned long hash_files(rev_file **files, int nfiles)
/* hash a file list so we can recognize it cheaply */
{
    unsigned long   h = 0;
    int		    i;

    for (i = 0; i < nfiles; i++)
	h = ((h << 1) | (h >> (sizeof(h) * 8 - 1))) ^ (unsigned long) files[i];
    return h;
}

static rev_dir *
rev_pack_dir(rev_file **files, int nfiles)
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
 * Compare/order two filenames, such that
 * - files in the same directory are sorted lexicographically
 * - files in a subdirectory sort earlier than the files in the parent
 *   For example, the following are sorted in this way:
 *        x/
 *        x/a
 *        x/b
 *        y/y/a
 *        y/a
 *        a
 *        b
 *        x      note that x/ sorts before x
 *        xx
 *        z
 */
static int compare_rev_file(const void *a, const void *b)
{
    rev_file **ap = (rev_file **)a;
    rev_file **bp = (rev_file **)b;
    const char *af = (*ap)->file_name;
    const char *bf = (*bp)->file_name;
    unsigned pos;
    const char *aslash;
    const char *bslash;

#ifdef ORDERDEBUG
    fprintf(stderr, "Comparing %s with %s\n", af, bf);
#endif /* ORDERDEBUG */

    /* advance pointers over common directory prefixes "foo/" */
    pos = 0;
    while (af[pos] && bf[pos] && af[pos] == bf[pos]) {
        if (af[pos] == '/') {
	    af += pos + 1;
	    bf += pos + 1;
	    pos = 0;
	} else {
	    ++pos;
	}
    }
    /* handle equal case */
    if (!af[pos] && !bf[pos])
        return 0;
    /* test both to see if they are leaves */
    aslash = strchr(af + pos, '/');
    bslash = strchr(bf + pos, '/');
    /* things in subdirs sort earlier than direct leaves */
    if (aslash && !bslash)
        return -1;
    if (!aslash && bslash)
        return +1;
    /* otherwise plain lexicographic sort */
    return (int)af[pos] - (int)bf[pos];
}


static int	    sds = 0;
static rev_dir **rds = NULL;

/* Puts an entry into the rds buffer, growing if needed */
static void
rds_put(int index, rev_dir *rd)
{
	if (sds == 0) {
		sds = 128;
		rds = xmalloc(sds * sizeof *rds, __func__);
	} else {
		while (sds <= index) {
			sds *= 2;
		}
		rds = xrealloc(rds, sds * sizeof *rds, __func__);
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
    char    *dir = 0;
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
	    fprintf(stderr, "rev_file: %s\n", (*s)->file_name);
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
	if (!dir || strncmp(files[i]->file_name, dir, dirlen) != 0)
	{
	    if (i > start) {
		rd = rev_pack_dir(files + start, i - start);
		rds_put(nds++, rd);
	    }
	    start = i;
	    dir = files[i]->file_name;
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

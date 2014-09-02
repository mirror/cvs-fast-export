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

#include "cvs.h"

static int
compare_names(const void *a, const void *b)
{
    const rev_file	*af = a, *bf = b;

#ifdef ORDERDEBUG
    fprintf(stderr, "compare_names(%s, %s) = %d\n", af->name, bf->name, strcmp(af->name, bf->name));
#endif /* ORDERDEBUG */
    return strcmp(af->name, bf->name);
}

#define REV_DIR_HASH	288361

typedef struct _rev_dir_hash {
    struct _rev_dir_hash    *next;
    unsigned long	    hash;
    rev_dir		    dir;
} rev_dir_hash;

static rev_dir_hash	*buckets[REV_DIR_HASH];

static 
unsigned long hash_files(rev_file **files, int nfiles)
{
    unsigned long   h = 0;
    int		    i;

    for (i = 0; i < nfiles; i++)
	h = ((h << 1) | (h >> (sizeof(h) * 8 - 1))) ^ (unsigned long) files[i];
    return h;
}

static int	total_dirs = 0;

/*
 * Take a collection of file revisions and pack them together
 */
static rev_dir *
rev_pack_dir(rev_file **files, int nfiles)
{
    unsigned long   hash = hash_files(files, nfiles);
    rev_dir_hash    **bucket = &buckets[hash % REV_DIR_HASH];
    rev_dir_hash    *h;

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
    total_dirs++;
    return &h->dir;
}

static int	    sds = 0;
static rev_dir **rds = NULL;

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
    
    if (!rds)
	rds = xmalloc((sds = 16) * sizeof(rev_dir *), __func__);
 
#ifdef ORDERDEBUG
    fputs("Packing:\n", stderr);
    {
	rev_file **s;

	for (s = files; s < files + nfiles; s++)
	    fprintf(stderr, "rev_file: %s\n", (*s)->name);
    }
#endif /* ORDERDEBUG */


    /* order by name */
    qsort(files, nfiles, sizeof(rev_file *), compare_names);

    /* pull out directories */
    for (i = 0; i < nfiles; i++) {
	if (!dir || strncmp(files[i]->name, dir, dirlen) != 0)
	{
	    if (i > start) {
		rd = rev_pack_dir(files + start, i - start);
		if (nds == sds) {
		    rds = xrealloc(rds, (sds *= 2) * sizeof(rev_dir *),
		    		__func__);
		    if (rds == NULL) {
			free(rds);
			exit(1);
		    }
		}
		rds[nds++] = rd;
	    }
	    start = i;
	    dir = files[i]->name;
	    slash = strrchr(dir, '/');
	    if (slash)
		dirlen = slash - dir;
	    else
		dirlen = 0;
	}
    }
    rd = rev_pack_dir(files + start, nfiles - start);
    if (nds == sds) {
	/* coverity[sizecheck] Coverity has a bug here */
	rds = xrealloc(rds, (sds *= 2) * sizeof(rev_dir *), __func__);
    }
    rds[nds++] = rd;
    
    *ndr = nds;
    return rds;
}

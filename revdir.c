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

struct _file_list {
    /* a directory containing a collection of file states */
    serial_t   nfiles;
    cvs_commit *files[0];
};


#define REV_DIR_HASH	288361

typedef struct _file_list_hash {
    struct _file_list_hash *next;
    hash_t	           hash;
    file_list		   fl;
} file_list_hash;

static file_list_hash	*buckets[REV_DIR_HASH];

static hash_t
hash_files(cvs_commit **files, const int nfiles)
/* hash a file list so we can recognize it cheaply */
{
    hash_t h = 0;
    size_t i;
    /* Combine existing hashes rather than computing new ones */
    for (i = 0; i < nfiles; i++)
	h = HASH_COMBINE(h, files[i]->hash);

    return h;
}

static file_list *
pack_file_list(cvs_commit **files, const int nfiles)
/* pack a collection of file revisions for space efficiency */
{
    hash_t         hash = hash_files(files, nfiles);
    file_list_hash **bucket = &buckets[hash % REV_DIR_HASH];
    file_list_hash *h;

    /* avoid packing a file list if we've done it before */ 
    for (h = *bucket; h; h = h->next) {
	if (h->hash == hash && h->fl.nfiles == nfiles &&
	    !memcmp(files, h->fl.files, nfiles * sizeof(cvs_commit *)))
	{
	    return &h->fl;
	}
    }
    h = xmalloc(sizeof(file_list_hash) + nfiles * sizeof(cvs_commit *),
		 __func__);
    h->next = *bucket;
    *bucket = h;
    h->hash = hash;
    h->fl.nfiles = nfiles;
    memcpy(h->fl.files, files, nfiles * sizeof(cvs_commit *));
    return &h->fl;
}

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

static int compare_cvs_commit(const void *a, const void *b)
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

static size_t    sdirs = 0;
static file_list **dirs = NULL;

static void
fl_put(const size_t index, file_list *fl)
/* puts an entry into the dirs buffer, growing if needed */
{
    if (sdirs == 0) {
	sdirs = 128;
	dirs = xmalloc(sdirs * sizeof(file_list *), __func__);
    } else if (sdirs <= index) {
	do {
	    sdirs *= 2;
	} while (sdirs <= index);
	dirs = xrealloc(dirs, sdirs * sizeof(revdir *), __func__);
    }
    dirs[index] = fl;
}

/* entry points begin here */

void
rev_free_dirs(void)
{
    unsigned long   hash;

    for (hash = 0; hash < REV_DIR_HASH; hash++) {
	file_list_hash  **bucket = &buckets[hash];
	file_list_hash	*h;

	while ((h = *bucket)) {
	    *bucket = h->next;
	    free(h);
	}
    }
    if (dirs) {
	free(dirs);
	dirs = NULL;
	sdirs = 0;
    }
}
/* Tuned to netbsd-pkgsrc which creates 94245 hash entries*/
#define DIR_COMP_BUCKETS 98317


typedef struct _dir_comp_bucket {
    struct _dir_comp_bucket *next;
    const master_dir        *child;
    const master_dir        *ancestor;
    bool                    is_ancestor;
} dir_comp_bucket;

static dir_comp_bucket *dir_comp_buckets[DIR_COMP_BUCKETS];

static bool
dir_is_ancestor(const master_dir *child, const master_dir *ancestor)
/* 
 * Test whether a directory is an ancestor of another, memoize the result
 * netbsd-pkgsrc calls this 7 billion times for 95,000 unique inputs
 */
{
    hash_t          hash;
    HASH_MIX_SEED(hash, child->prehash,  ancestor);
    dir_comp_bucket **head = &dir_comp_buckets[hash % DIR_COMP_BUCKETS];
    dir_comp_bucket *b;

    while ((b = *head)) {
	if (b->child == child && b->ancestor == ancestor)
	    return b->is_ancestor;
	head = &(b->next);
    }

    b = xmalloc(sizeof(dir_comp_bucket), __func__);
    b->next = NULL;
    b->child = child;
    b->ancestor = ancestor;
    b->is_ancestor = (strncmp(child->name, ancestor->name, ancestor->len) == 0);
    *head = b;
    return b->is_ancestor;
}

/* An iterator structure over the sorted files in a rev_dir */
struct _revdir_iter {
    file_list * const *dir;
    file_list * const *dirmax;
    cvs_commit **file;
    cvs_commit **filemax;
} file_iter;

/* Iterator interface */
cvs_commit *
revdir_iter_next(revdir_iter *it) {
    if (it->dir == it->dirmax)
        return NULL;
again:
    if (it->file != it->filemax)
	return *it->file++;
    ++it->dir;
    if (it->dir == it->dirmax)
        return NULL;
    it->file = (*it->dir)->files;
    it->filemax = it->file + (*it->dir)->nfiles;
    goto again;
}

cvs_commit *
revdir_iter_next_dir(revdir_iter *it) {
 again:
    ++it->dir;
    if (it->dir >= it->dirmax)
	return NULL;
    it->file = (*it->dir)->files;
    it->filemax = it->file + (*it->dir)->nfiles;
    if (it->file != it->filemax)
	return *it->file++;
    goto again;
}

void
revdir_iter_start(revdir_iter *it, const revdir *revdir) {
    it->dir = revdir->dirs;
    it->dirmax = revdir->dirs + revdir->ndirs;
    if (it->dir != it->dirmax) {
        it->file = (*it->dir)->files;
        it->filemax = it->file + (*it->dir)->nfiles;
    } else {
        it->file = it->filemax = NULL;
    }
}

bool
revdir_iter_same_dir(const revdir_iter *it1, const revdir_iter *it2)
{
    return *it1->dir == *it2->dir;
}

revdir_iter *
revdir_iter_alloc(const revdir *revdir)
{
    revdir_iter *it = xmalloc(sizeof(revdir_iter), __func__);
    revdir_iter_start(it, revdir);
    return it;
}

void
revdir_pack_files(cvs_commit **files, const size_t nfiles, revdir *revdir)
{
    const master_dir *dir = NULL, *curdir = NULL;
    size_t           i, start = 0;
    unsigned short   ndirs = 0;
    file_list        *fl;
    
#ifdef ORDERDEBUG
    fputs("Packing:\n", stderr);
    {
	cvs_commit **s;

	for (s = files; s < files + nfiles; s++)
	    fprintf(stderr, "cvs_commit: %s\n", (*s)->master->name);
    }
#endif /* ORDERDEBUG */

    /*
     * The purpose of this sort is to rearrange the files in
     * directory-path order so we get the longest possible
     * runs of common directory prefixes, and thus maximum 
     * space-saving effect out of the next step.  This reduces
     * working-set size at the expense of the sort runtime.
     * Sorting the masters at the input stage causes
     * them to come out in the right order here, without
     * multiple additional sorts
     */
#ifndef FILESORT
    qsort(files, nfiles, sizeof(cvs_commit *), compare_cvs_commit);
#endif /* !FILESORT */

    /* pull out directories */
    for (i = 0; i < nfiles; i++) {
	/* avoid strncmp as much as possible */
	if (curdir != files[i]->master->dir) {
	    if (!dir || !dir_is_ancestor(files[i]->master->dir, dir)) {
		if (i > start) {
		    fl = pack_file_list(files + start, i - start);
		    fl_put(ndirs++, fl);
		}
		start = i;
		dir = files[i]->master->dir;
	    }
	    curdir = files[i]->master->dir;
	}
    }
    if (dir) {
        fl = pack_file_list(files + start, nfiles - start);
        fl_put(ndirs++, fl);
    }
    
    revdir->dirs = xmalloc(ndirs * sizeof(file_list *), "rev_dir");
    revdir->ndirs = ndirs;
    memcpy(revdir->dirs, dirs, ndirs * sizeof(file_list *));
}

size_t
revdir_sizeof(void)
{
    return sizeof(revdir);
}


// end

/* 
 * Memoized sort implementation.
 *
 * Use the fact that we sort the same paths over and over again
 * approach is to keep a sorted hash of paths we've seen.
 * and use this aid sorting future path lists
 */
#include "cvs.h"
#include "uthash.h"

typedef struct commit_path {
    const char *path;
    /* whenever we're asked to sort, we bump an id counter
     * this field tracks whether an item was in the input list
     */
    size_t id;
    /* pointer into the input array
     */
    cvs_commit *commit;
    UT_hash_handle hh;
} commit_path_t;


static int 
compare_commit_path(void *a, void *b)
{
    commit_path_t *ap = (commit_path_t *)a;
    commit_path_t *bp = (commit_path_t *)b;
    const char *af = ap->path;
    const char *bf = bp->path;
    return path_deep_compare(af, bf);
}

/*
 * Sort an array of cvs_commits using memoization.
 * Input list must have no duplicates. This code 
 * remembers previous sort order, and uses
 * this to speed up similar sorts.
 */
void 
memo_sort(cvs_commit **files, const int nfiles)
{
    // putting it here means we can't free it
    static commit_path_t *hash_table = NULL;
    static size_t id = 0;
    cvs_commit **s;
    commit_path_t *item;
    int needs_sort = 0;

    /* Add any new paths to the hash */
    for(s = files; s < files + nfiles; s++) {
	HASH_FIND_STR(hash_table, (*s)->master->name, item);
	if (item == NULL) {
	    /* obvious slab alert */
	    item = xmalloc(sizeof(*item), __func__);
	    item->path = (*s)->master->name;
	    HASH_ADD_STR(hash_table, path, item);
	    needs_sort = 1;
	} 
	item->id = id;
	item->commit = (*s);
    }
    /* Only sort if we added a new path in this invocation */
    if (needs_sort) {
	HASH_SORT(hash_table, compare_commit_path);
    }

    /* sort input array */
    for (s = files, item = hash_table; item != NULL; item = item->hh.next) 
	if (item->id == id)
	    *(s++) = item->commit;
    

    id++;
} 

/* end */

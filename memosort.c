/* 
 * Memoized sort implementation.
 *
 * Use the fact that we sort the same paths over and over again
 * We read the paths we find during input, and then keep a sorted
 * list of them with a hash index basked on path
 * As paths are interned, we can use the pointers as keys
 * This lets us sort file list with one hash lookup and one write
 * per list entry
 */
#include "cvs.h"
#include "uthash.h"
#ifdef THREADS
#include <pthread.h>
#endif /* THREADS */
typedef struct commit_path {
    const char *path;
    /*
     * Whenever we're asked to sort, we bump an id counter
     * this field tracks whether an item was in the input list.
     */
    serial_t id;
    /* Pointer into the input array */
    cvs_commit *commit;
    UT_hash_handle hh;
} commit_path_t;

/* Collect paths here as we read them during the input phase. */
static const char **path_list = NULL;
static size_t path_count = 0;
/*
 * After the input  phase, we change to a sorted array of commit_path
 * with a hash table for fast lookup by path.
 */
static commit_path_t *commit_table = NULL;
static commit_path_t *sorted_commits = NULL;
#ifdef THREADS
static pthread_mutex_t path_list_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif /* THREADS */
static int
compare_path (const void *a, const void *b)
{
  const char *ap = *(const char **)a;
  const char *bp = *(const char **)b;
  return path_deep_compare(ap, bp);
}

/* If we hook in early enough, we only consider each path once. */
void
collect_path(const char* path)
{
  static int alloc = 0;
#ifdef THREADS
  if (threads > 1)
    pthread_mutex_lock(&path_list_mutex);
#endif /* THREADS */

  if (path_count >= alloc) {
    alloc += 1024;
    path_list = xrealloc(path_list, alloc * sizeof(const char *), __func__);
  }
  path_list[path_count++] = path;
#ifdef THREADS
  if (threads > 1)
    pthread_mutex_unlock(&path_list_mutex);
#endif /* THREADS */
}

/*
 * Once we have all paths, we can turn it into a hash table of
 * path to commit_path_t ordered by path_deep_compare.
 * What's more we can allocate the underlying storage in
 * one block for good locality.
 */
void
presort_paths(void)
{
  size_t i;
  commit_path_t *cp;
  qsort(path_list, path_count, sizeof(const char *), compare_path);
  sorted_commits = xmalloc(path_count * sizeof(commit_path_t), __func__);

  for (i = 0, cp = sorted_commits; i < path_count; i++, cp++) {
    cp->path = path_list[i];
    cp->id = 0;
    /* Paths are interned, so we can use the pointer as a key. */
    HASH_ADD_PTR(commit_table, path, cp);
  }
  free(path_list);
}
/*
 *  Sort an array of cvs_commits using memoization.
 * Input list must have no duplicates. This code
 * remembers previous sort order, and uses
 * this to speed up similar sorts.
 */
void 
memo_sort(cvs_commit **files, const int nfiles)
{
    static serial_t id = 1;
    cvs_commit **s;
    commit_path_t *item;

    /* Find paths in the ordered commits using the hash. */
    for(s = files; s < files + nfiles; s++) {
        HASH_FIND_PTR(commit_table, &(*s)->master->name, item);
	item->id = id;
	item->commit = (*s);
    }
    /* Sort input array. */
    for (s = files, item = sorted_commits; item < sorted_commits + path_count; item++)
	if (item->id == id)
	    *(s++) = item->commit;
    
    id++;
}

/* not called yet, but could be after the merge common branches stage */
void
free_memo_table(void) {
  free(sorted_commits);
  HASH_CLEAR(hh, commit_table);
}

/* end */

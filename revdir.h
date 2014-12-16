#ifndef _REVDIR_H_
#define _REVDIR_H_

#include "cvs.h"

#define MAX_DIR_DEPTH 64

/* struct revdir is defined in cvs.h so we can take advantage of struct packing */
typedef struct _revdir_iter revdir_iter;

/* pack a list of files into a revdir, reusing sequences we've seen before */
void
revdir_pack_files(const cvs_commit **files, const size_t nfiles, revdir *revdir);

/* count the number of files in a revdir */
serial_t
revdir_nfiles(const revdir *revdir);

/* Create a revdir a file at a time */
void
revdir_pack_alloc(const size_t max_size);

void
revdir_pack_init(void);

void
revdir_pack_add(const cvs_commit *file, const master_dir *dir);

void
revdir_pack_end(revdir *revdir);

void
revdir_pack_free(void);

/* allocate an iterator to use with a revdir */
revdir_iter *
revdir_iter_alloc(const revdir *revdir);

/* set an iterator to the start of a revdir */
void
revdir_iter_start(revdir_iter *iter, const revdir *revdir);

/* get the next item from a revdir */
cvs_commit *
revdir_iter_next(revdir_iter *it);

/* skip a "dir" in a revdir */
cvs_commit *
revdir_iter_next_dir(revdir_iter *it);

/* are two revdirs pointing to the same "dir" */
bool
revdir_iter_same_dir(const revdir_iter *it1, const revdir_iter *it2);

void
revdir_free_bufs(void);

void
revdir_free(void);

int
compare_cvs_commit(const void *a, const void *b);

/* useful if you're reusing an iterator with different revdirs */
#define REVDIR_ITER_START(iter, revdir) \
    if (!(iter))				    \
	iter = revdir_iter_alloc((revdir));	    \
    else					    \
	revdir_iter_start((iter), (revdir))

#endif /* _REVDIR_H_ */

/*
 * Implementation of revdir.h that saves a lot more space than the original dirpack scheme.
 * Designed to be included in revdir.c.
 */

#define REV_DIR_HASH	786433
/* Names are getting confusing. Externally we call things a revdir, where really it's
 * just a list of revisions.
 * Internally in treepack, we store as a directory of revisions, which each level having 
 * files and subdirectories. A good name for this would appear to be revdir...
 */
struct _rev_pack {
    /* a directory containing a collection of subdirs and a collection of file revisions */
    hash_t     hash;
    serial_t   ndirs;
    serial_t   nfiles;
    rev_pack   **dirs;
    cvs_commit **files;
};

typedef struct _rev_pack_hash {
    struct _rev_pack_hash *next;
    rev_pack	          dir;
} rev_pack_hash;

static rev_pack_hash	*buckets[REV_DIR_HASH];

static rev_pack *
rev_pack_dir(rev_pack * const * dirs, const size_t ndirs,
	     cvs_commit * const * files, const size_t nfiles,
	     const hash_t hash)
{
    rev_pack_hash **bucket = &buckets[hash % REV_DIR_HASH];
    rev_pack_hash *h;

    /* avoid packing a file list if we've done it before */ 
    for (h = *bucket; h; h = h->next) {
	if (h->dir.hash == hash &&
	    h->dir.nfiles == nfiles && h->dir.ndirs == ndirs &&
	    !memcmp(dirs, h->dir.dirs, ndirs * sizeof(rev_pack *)) &&
	    !memcmp(files, h->dir.files, nfiles * sizeof(cvs_commit *)))
	{
	    return &h->dir;
	}
    }
    h = xmalloc(sizeof(rev_pack_hash), __func__);
    h->next = *bucket;
    *bucket = h;
    h->dir.hash = hash;
    h->dir.ndirs = ndirs;
    h->dir.dirs = xmalloc(ndirs * sizeof(rev_pack *), __func__);
    memcpy(h->dir.dirs, dirs, ndirs * sizeof(rev_pack *));
    h->dir.nfiles = nfiles;
    h->dir.files = xmalloc(nfiles * sizeof(cvs_commit *), __func__);
    memcpy(h->dir.files, files, nfiles * sizeof(cvs_commit *));
    return &h->dir;
}

/* Post order tree traversal iterator. */
typedef struct _dir_pos {
    rev_pack *parent;
    rev_pack **dir;
    rev_pack **dirmax;
} dir_pos;

struct _revdir_iter {
    cvs_commit     **file;
    cvs_commit     **filemax;
    size_t         dirpos; // current dir is dirstack[dirpos]
    dir_pos        dirstack[MAX_DIR_DEPTH];
};

bool
revdir_iter_same_dir(const revdir_iter *it1, const revdir_iter *it2)
/* Are two file iterators pointing to the same revdir? */
{
    return it1->dirstack[it1->dirpos].dir == it2->dirstack[it2->dirpos].dir;
}

cvs_commit *
revdir_iter_next(revdir_iter *it) {
 again:
    if (it->file != it->filemax)
	return *it->file++;
    // end of stack
    if (!it->dirpos)
	return NULL;
    
    // pop the stack
    dir_pos *d = &it->dirstack[--it->dirpos];
    if (++d->dir != d->dirmax) {
	// does new dir have subdirs?
	rev_pack *dir = *d->dir;
	while (1) {
	    d = &it->dirstack[++it->dirpos];
	    d->parent = dir;
	    d->dir = dir->dirs;
	    d->dirmax = dir->dirs + dir->ndirs;
	    if (dir->ndirs > 0)
		dir = dir->dirs[0];
	    else
		break;
	}
	it->file = dir->files;
	it->filemax = dir->files + dir->nfiles;
    } else {
	// all subdirs done, now do files in this dir
	rev_pack *dir = d->parent;
	it->file = dir->files;
	it->filemax = dir->files + dir->nfiles;
    }
    goto again;
}

cvs_commit *
revdir_iter_next_dir(revdir_iter *it)
/* skip the current directory */
{
    while(1) {
	if (!it->dirpos)
	    return NULL;

	dir_pos *d = &it->dirstack[--it->dirpos];
	if (++d->dir != d->dirmax) {
	    rev_pack *dir = *d->dir;
	    while (1) {
		d = &it->dirstack[++it->dirpos];
		d->parent = dir;
		d->dir = dir->dirs;
		d->dirmax = dir->dirs + dir->ndirs;
		if (dir->ndirs > 0)
		    dir = dir->dirs[0];
		else
		    break;
	    }
	    it->file = dir->files;
	    it->filemax = dir->files + dir->nfiles;
	} else {
	    rev_pack *dir = d->parent;
	    it->file = dir->files;
	    it->filemax = dir->files + dir->nfiles;
	}
	if (it->file != it->filemax)
	    return *it->file++;
    }
}

void
revdir_iter_start(revdir_iter *it, const revdir *revdir) 
/* post order traversal of rev_dir tree */
{
    rev_pack *dir = revdir->revpack;
    it->dirpos = -1;
    while (1) {
	dir_pos *d = &it->dirstack[++it->dirpos];
	d->parent = dir;
	d->dir = dir->dirs;
	d->dirmax = dir->dirs + dir->ndirs;
	if (dir->ndirs > 0)
	    dir = dir->dirs[0];
	else
	    break;
    } 
    it->file = dir->files;
    it->filemax = dir->files + dir->nfiles;
}

revdir_iter *
revdir_iter_alloc(const revdir *revdir)
{
    revdir_iter *it = xmalloc(sizeof(revdir_iter), __func__);
    revdir_iter_start(it, revdir);
    return it;
}


static const master_dir *
first_subdir (const master_dir *child, const master_dir *ancestor)
/* in ancestor/d1/d2/child, return d1 */
{
    while (child->parent != ancestor)
	child = child->parent;
    return child;
}

/* buffers for recursive directory gathering */
static rev_pack **dir_bufs[MAX_DIR_DEPTH];
static size_t dir_buf_size[MAX_DIR_DEPTH];

static rev_pack *
tree_pack_dir(cvs_commit * const * const files, const size_t nfiles,
	      // start is altered by recursive calls
	      size_t *start, const master_dir * const dir, size_t depth)
/* Pack a directory of revisions recursively.
 * Each subdirectory level is interned,
 * so we have good chance of reuse between commits
 */
{
    size_t     ndirs = 0;
    cvs_commit *f    = files[*start];
    HASH_INIT(hash);
    if (depth >= MAX_DIR_DEPTH)
	fatal_error("Directories nested too deep, increase MAX_DIR_DEPTH");

    /* pack subdirectories */
    while (*start < nfiles && // we still have files
	   // we're not directly in the directory we're packing
	   f->master->dir != dir &&
           // we're still somewhere under the directory we're packing
	   dir_is_ancestor(f->master->dir, dir)) {
	if (dir_buf_size[depth] == ndirs) {
	    if (dir_buf_size[depth] == 0)
		dir_buf_size[depth] = 16;
	    else
		dir_buf_size[depth] *= 2;

	    dir_bufs[depth] = xrealloc(dir_bufs[depth], dir_buf_size[depth] * sizeof(rev_pack *), __func__);
	}
	dir_bufs[depth][ndirs] = tree_pack_dir(files, nfiles, start,
					       first_subdir(f->master->dir, dir),
					       depth + 1);

	hash = HASH_COMBINE(hash, dir_bufs[depth][ndirs]->hash);
	ndirs++;

	f = files[*start];
    }
    HASH_MIX(hash, ndirs);
    /* pack files that are direct children of this directory */
    size_t i = *start;
    while (i < nfiles && f->master->dir == dir) {
	hash = HASH_COMBINE(hash, files[i]->hash);
	f = files[++i];
    }

    rev_pack *ret = rev_pack_dir(dir_bufs[depth], ndirs, files + *start, i - *start, hash);
    *start = i;
    return ret;
}

void
revdir_pack_files(cvs_commit **files, const size_t nfiles, revdir *revdir)
{
    size_t start = 0;
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
    revdir->revpack = tree_pack_dir(files, nfiles, &start, root_dir, 0);
}

void
revdir_free(void)
{
    size_t i;
    for (i = 0; i < REV_DIR_HASH; i++) {
	rev_pack_hash **bucket = &buckets[i];
	rev_pack_hash *h;

	while ((h = *bucket)) {
	    *bucket = h->next;
	    free(h);
	}
    }
}

void
revdir_free_bufs(void)
{
    size_t i;
    for (i = 0; i < MAX_DIR_DEPTH; i++) {
	free(dir_bufs[i]);
	dir_bufs[i] = NULL;
	dir_buf_size[i] = 0;
    }
}

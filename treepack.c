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

typedef struct _pack_frame {
    const master_dir    *dir;
    const rev_pack      **dirs;
    hash_t              hash;
    unsigned short      ndirs;
    unsigned short      sdirs;
} pack_frame;

/* variables used by streaming pack interface */
static serial_t         sfiles = 0;
static serial_t         nfiles = 0;
static const cvs_commit **files = NULL;
static pack_frame       *frame;
static pack_frame       frames[MAX_DIR_DEPTH];

static const rev_pack *
rev_pack_dir(void)
{
    rev_pack_hash **bucket = &buckets[frame->hash % REV_DIR_HASH];
    rev_pack_hash *h;

    /* avoid packing a file list if we've done it before */ 
    for (h = *bucket; h; h = h->next) {
	if (h->dir.hash == frame->hash &&
	    h->dir.nfiles == nfiles && h->dir.ndirs == frame->ndirs &&
	    !memcmp(frame->dirs, h->dir.dirs, frame->ndirs * sizeof(rev_pack *)) &&
	    !memcmp(files, h->dir.files, nfiles * sizeof(cvs_commit *)))
	{
	    return &h->dir;
	}
    }
    h = xmalloc(sizeof(rev_pack_hash), __func__);
    h->next = *bucket;
    *bucket = h;
    h->dir.hash = frame->hash;
    h->dir.ndirs = frame->ndirs;
    h->dir.dirs = xmalloc(frame->ndirs * sizeof(rev_pack *), __func__);
    memcpy(h->dir.dirs, frame->dirs, frame->ndirs * sizeof(rev_pack *));
    h->dir.nfiles = nfiles;
    h->dir.files = xmalloc(nfiles * sizeof(cvs_commit *), __func__);
    memcpy(h->dir.files, files, nfiles * sizeof(cvs_commit *));
    return &h->dir;
}

/* Post order tree traversal iterator. */
typedef struct _dir_pos {
    const rev_pack *parent;
    rev_pack       **dir;
    rev_pack       **dirmax;
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
    while (1) {
	if (it->file != it->filemax)
	    return *it->file++;
	// end of stack
	if (!it->dirpos)
	    return NULL;

	// pop the stack
	dir_pos *d = &it->dirstack[--it->dirpos];
	if (++d->dir != d->dirmax) {
	    // does new dir have subdirs?
	    const rev_pack *dir = *d->dir;
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
	    const rev_pack *dir = d->parent;
	    it->file = dir->files;
	    it->filemax = dir->files + dir->nfiles;
	}
    }
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
	    const rev_pack *dir = *d->dir;
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
	    const rev_pack *dir = d->parent;
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
    const rev_pack *dir = revdir->revpack;
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

void
revdir_pack_alloc(const size_t max_size)
{
    if (!files) {
	files = xmalloc(max_size * sizeof(cvs_commit *), __func__);
	sfiles = max_size;
    } else if (sfiles < max_size) {
	files = xrealloc(files, max_size * sizeof(cvs_commit *), __func__);
	sfiles = max_size;
    }
}

void
revdir_pack_init(void)
{
    frame = frames;
    nfiles = 0;
    frames[0].dir = root_dir;
    frames[0].ndirs = 0;
    frames[0].hash = hash_init();
}

static void
push_rev_pack(const rev_pack * const r)
/* Store a revpack in the recursive gathering area */
{
    unsigned short *s = &frame->sdirs;
    if (*s == frame->ndirs) {
	if (!*s)
	    *s = 16;
	else
	    *s *= 2;
	frame->dirs = xrealloc(frame->dirs, *s * sizeof(rev_pack *), __func__);
    }
    frame->dirs[frame->ndirs++] = r;
}

void
revdir_pack_add(const cvs_commit *file, const master_dir *dir)
{
    while (1) {
	if (frame->dir == dir) {
	    /* If you are using STREAMDIR, TREEPACK then this is the hottest inner
	     * loop in the application. Avoid dereferencing file
             */
	    files[nfiles++] = file;
	    /* Proper FNV1a is a byte at a time, but this is effective
	     * with the amount of data we're typically mixing into the hash
             * and very lightweight
	     */
 	    frame->hash = (frame->hash ^ (uintptr_t)file) * 16777619U;
	    return;
	}
	if (dir_is_ancestor(dir, frame->dir)) {
	    if (frame - frames == MAX_DIR_DEPTH)
		fatal_error("Directories nested too deep, increase MAX_DIR_DEPTH\n");
	    
	    const master_dir *parent = frame++->dir;
	    frame->dir = first_subdir(dir, parent);
	    frame->ndirs = 0;
	    frame->hash = hash_init();
	    continue;
	}
	
	const rev_pack * const r = rev_pack_dir();
	nfiles = 0;
	frame--;
	frame->hash = HASH_COMBINE(frame->hash, r->hash);
	push_rev_pack(r);
    }
}

void
revdir_pack_end(revdir *revdir)
{
    const rev_pack * r = NULL;
    while (1) {
	r = rev_pack_dir();
	if (frame == frames)
	    break;
	
	nfiles = 0;
	frame--;
	frame->hash = HASH_COMBINE(frame->hash, r->hash);
	push_rev_pack(r);
    }
    revdir->revpack = r;
}

void
revdir_pack_free(void)
{
    free(files);
    files = NULL;
    nfiles = 0;
    sfiles = 0;
}

static serial_t
revpack_nfiles(const rev_pack *revpack)
{
    serial_t c = 0, i;

    for (i = 0; i < revpack->ndirs; i++)
	c += revpack_nfiles(revpack->dirs[i]);
    return c + revpack->nfiles;

}

serial_t
revdir_nfiles(const revdir *revdir)
{
    return revpack_nfiles(revdir->revpack);
}

void
revdir_pack_files(const cvs_commit **files, const size_t nfiles, revdir *revdir)
{
    size_t i;
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
    revdir_pack_alloc(nfiles);
    revdir_pack_init();
    for (i = 0; i < nfiles; i++)
	revdir_pack_add(files[i], files[i]->dir);
	
    revdir_pack_end(revdir);
    revdir_pack_free();
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
	    free(h->dir.dirs);
	    free(h->dir.files);
	    free(h);
	}
    }
}

void
revdir_free_bufs(void)
{
    size_t i;
    for (i = 0; i < MAX_DIR_DEPTH; i++) {
	free(frames[i].dirs);
	frames[i].dir = NULL;
	frames[i].sdirs = 0;
    }
}

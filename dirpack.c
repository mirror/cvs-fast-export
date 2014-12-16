/* Simpler method of packing a file list.
 * Designed to be included in revdir.c
 */

#define REV_DIR_HASH	393241

struct _file_list {
    /* a directory containing a collection of file states */
    serial_t   nfiles;
    cvs_commit *files[0];
};

typedef struct _file_list_hash {
    struct _file_list_hash *next;
    hash_t	           hash;
    file_list		   fl;
} file_list_hash;

static file_list_hash	*buckets[REV_DIR_HASH];

static hash_t
hash_files(const cvs_commit *const * const files, const int nfiles)
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
pack_file_list(const cvs_commit * const * const files, const int nfiles)
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

void
revdir_free(void)
{
    size_t i;
    for (i = 0; i < REV_DIR_HASH; i++) {
	file_list_hash  **bucket = &buckets[i];
	file_list_hash	*h;

	while ((h = *bucket)) {
	    *bucket = h->next;
	    free(h);
	}
    }
}

void
revdir_free_bufs(void)
{
    if (dirs) {
	free(dirs);
	dirs = NULL;
	sdirs = 0;
    }
}

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

serial_t
revdir_nfiles(const revdir *revdir)
{
    serial_t c = 0, i;
    for (i = 0; i < revdir->ndirs; i++)
	c += revdir->dirs[i]->nfiles;
    return c;
}

static serial_t         nfiles = 0;
static serial_t         sfiles = 0;
static const cvs_commit **files = NULL;
static const master_dir *dir;
static const master_dir *curdir;
static unsigned short   ndirs;

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
    nfiles = 0;
    curdir = NULL;
    dir = NULL;
    ndirs = 0;
}

void
revdir_pack_add(const cvs_commit *file, const master_dir *in_dir)
{
    if (curdir != in_dir) {
	if (!dir_is_ancestor(in_dir, dir)) {
	    if (nfiles > 0) {
		file_list *fl = pack_file_list(files, nfiles);
		fl_put(ndirs++, fl);
		nfiles = 0;
	    }
	    dir = in_dir;
	}
	curdir = in_dir;
    }
    files[nfiles++] = file;
}

void
revdir_pack_end(revdir *revdir)
{
    if (dir) {
	file_list *fl = pack_file_list(files, nfiles);
	fl_put(ndirs++, fl);
    }
    revdir->dirs = xmalloc(ndirs * sizeof(file_list *), __func__);
    revdir->ndirs = ndirs;
    memcpy(revdir->dirs, dirs, ndirs * sizeof(file_list *));
}

void
revdir_pack_free()
{
    free(files);
    files = NULL;
    nfiles = 0;
    sfiles = 0;
}

void
revdir_pack_files(const cvs_commit ** files, 
		  const size_t nfiles, revdir *revdir)
{
    size_t           start = 0, i;
    const master_dir *curdir = NULL, *dir = NULL;
    file_list        *fl;
    unsigned short   ndirs = 0;
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
	if (curdir != files[i]->dir) {
	    if (!dir_is_ancestor(files[i]->dir, dir)) {
		if (i > start) {
		    fl = pack_file_list(files + start, i - start);
		    fl_put(ndirs++, fl);
		    start = i;
		}
		dir = files[i]->dir;
	    }
	    curdir = files[i]->dir;
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

/*
 *  Copyright © 2006 Keith Packard <keithp@keithp.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
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
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef THREADS
#include <pthread.h>
#endif /* THREADS */

#include "cvs.h"
#include "gram.h"
#include "lex.h"

/*
 * CVS master analysis.  Grinds out a cvs_repo list represnting
 * the entire CVS history of a collection.
 */

typedef struct _rev_filename {
    struct _rev_filename	*next;
    const char			*file;
} rev_filename;

typedef struct _rev_file {
    const char *name;
    const char *rectified;
} rev_file;
/*
 * Ugh...least painful way to make some stuff that isn't thread-local
 * visible.
 */
static volatile int load_current_file;
static rev_filename   *fn_head = NULL, **fn_tail = &fn_head, *fn;
/* Slabs to be sorted in path_deep_compare order */
static rev_file *sorted_files;
static cvs_master *cvs_masters;
static rev_master *rev_masters;
static volatile size_t fn_i = 0, fn_n;
static volatile cvstime_t skew_vulnerable;
static volatile unsigned int total_revisions;
static volatile generator_t *generators;
static volatile int err;

static int total_files, striplen;
static int verbose;

#ifdef THREADS
static pthread_mutex_t revlist_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t enqueue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t *workers;
#endif /* THREADS */

typedef struct _analysis {
    cvstime_t skew_vulnerable;
    unsigned int total_revisions;
    generator_t generator;
} analysis_t;

static cvs_master *
sort_cvs_masters(cvs_master *list);

static void
debug_cvs_masters(cvs_master *list);

static char *
rectify_name(const char *raw, char *rectified, size_t rectlen)
/* from master name to the name humans thought of the file by */
{
    unsigned len;
    const char *s, *snext;
    char *p;

    p = rectified;
    s = raw + striplen;
    while (*s) {
	for (snext = s; *snext; snext++)
	    if (*snext == '/') {
	        ++snext;
		/* assert(*snext != '\0'); */
	        break;
	    }
	len = snext - s;
	/* special processing for final components */
	if (*snext == '\0') {
	    /* trim trailing ,v */
	    if (len > 2 && s[len - 2] == ',' && s[len - 1] == 'v')
	        len -= 2;
	} else { /* s[len-1] == '/' */
	    /* drop some path components */
	    if (len == sizeof "Attic" && memcmp(s, "Attic/", len) == 0)
	        goto skip;
	    if (len == sizeof "RCS" && memcmp(s, "RCS/", len) == 0)
		goto skip;
	}
	/* copy the path component */
	if (p + len >= rectified + rectlen)
	    fatal_error("File name %s\n too long\n", raw);
	memcpy(p, s, len);
	p += len;
    skip:
	s = snext;
    }
    *p = '\0';
    len = p - rectified;

    return rectified;
}

static const char*
atom_rectify_name(const char *raw)
{
    char rectified[PATH_MAX];
    return atom(rectify_name(raw, rectified, sizeof(rectified)));
}

static void
rev_list_file(rev_file *file, analysis_t *out, cvs_master *cm, rev_master *rm) 
{
    struct stat	buf;
    yyscan_t scanner;
    FILE *in;
    cvs_file *cvs;

    in = fopen(file->name, "r");
    if (!in) {
	perror(file->name);
	++err;
	return;
    }
    if (stat(file->name, &buf) == -1) {
	fatal_system_error("%s", file->name);
    }

    cvs = xcalloc(1, sizeof(cvs_file), __func__);
    cvs->gen.master_name = file->name;
    cvs->gen.expand = EXPANDUNSPEC;
    cvs->export_name = file->rectified;
    cvs->mode = buf.st_mode;
    cvs->verbose = verbose;

    yylex_init(&scanner);
    yyset_in(in, scanner);
    yyparse(scanner, cvs);
    yylex_destroy(scanner);

    fclose(in);
    cvs_master_digest(cvs, cm, rm);
    out->total_revisions = cvs->nversions;
    out->skew_vulnerable = cvs->skew_vulnerable;
    out->generator = cvs->gen;
    cvs_file_free(cvs);
}

static int
strcommonendingwith(const char *a, const char *b, char endc)
/* return the length of the common prefix of strings a and b ending with endc */
{
    int c = 0;
    int d = 0;
    while (*a == *b) {
	if (!*a) {
	    break;
 	}
	a++;
	b++;
	c++;
	if (*a == endc) {
	    d = c + 1;
	}
    }
    return d;
}

#ifdef FILESORT
/*
 * Sort list of cvs masters in path_deep_compare order of output name.
 * Causes commits to come out in correct pack order.
 * Also causes operations to come out in correct fileop_sort order.
 * Note some output names are different to input names.
 * e.g. .cvsignore becomes .gitignore
 *
 * Based on merge sort algorithm described at
 * http://www.chiark.greenend.org.uk/~sgtatham/algorithms/listsort.html
 * and obviously similar to the the cvs_master_sort_heads function.
 */
static cvs_master *
sort_cvs_masters(cvs_master *list)
{
    cvs_master *p, *q, *elt;
    unsigned int k = 1, merge_count, ps, qs;

    if (!list)
	return NULL;

    do {
	cvs_master *tail = NULL;
	p = list;
	list = tail = NULL;
	merge_count = 0;
	while (p) {
	    // skip q k places past p
	    merge_count++;
	    for (q = p, ps = 0; ps < k && q; ps++, q = q->next);
	    qs = k;
	    while (ps > 0 || (qs > 0 && q)) {
		if (ps == 0 || (qs != 0 && q &&
				path_deep_compare(
				  p->heads->commit->master->fileop_name,
                                  q->heads->commit->master->fileop_name
                                ) > 0)) {
		    elt = q;
		    q = q->next;
		    qs--;
		} else {
		    elt = p;
		    p = p->next;
		    ps--;
		}
		if (tail)
		    tail->next = elt;
		else
		    list = elt;
		tail = elt;
	    }
	    p = q;
	}
	tail->next = NULL;
	k += k;
    } while (merge_count > 1);
    return list;
}
#endif /* FILESORT */

static void *worker(void *arg)
/* consume masters off the queue */
{
    analysis_t out = {0, 0};
    size_t     i;
    for (;;)
    {
	//const char *filename;

	/* pop a master off the queue, terminating if none left */
#ifdef THREADS
	if (threads > 1)
	    pthread_mutex_lock(&enqueue_mutex);
#endif /* THREADS */
	i = fn_i++;
#ifdef THREADS
	if (threads > 1)
	    pthread_mutex_unlock(&enqueue_mutex);
#endif /* THREADS */
	if (i >= fn_n)
	    return(NULL);

	/* process it */
	rev_list_file(&sorted_files[i], &out, &cvs_masters[i], &rev_masters[i]);

	/* pass it to the next stage */
#ifdef THREADS
	if (threads > 1)
	    pthread_mutex_lock(&revlist_mutex);
#endif /* THREADS */
	generators[i] = out.generator;
	progress_jump(++load_current_file);
	total_revisions += out.total_revisions;
	if (out.skew_vulnerable > skew_vulnerable)
	    skew_vulnerable = out.skew_vulnerable;
#ifdef THREADS
	if (threads > 1)
	    pthread_mutex_unlock(&revlist_mutex);
#endif /* THREADS */
    }
}

static int 
file_compare(const void *f1, const void *f2)
{
    rev_file r1 = *(rev_file *)f1;
    rev_file r2 = *(rev_file *)f2;
    return path_deep_compare(r1.rectified, r2.rectified);
}

void analyze_masters(int argc, char *argv[], 
			  import_options_t *analyzer, 
			  forest_t *forest)
/* main entry point; collect and parse CVS masters */
{
    char	    name[PATH_MAX];
    const char      *last = NULL;
    char	    *file;
    size_t	    i, j = 1;
    int		    c;
#ifdef THREADS
    pthread_attr_t  attr;

    /* Initialize and reinforce default thread non-detached attribute */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
#endif /* THREADS */

    striplen = analyzer->striplen;

    forest->textsize = forest->filecount = 0;
    progress_begin("Reading file list...", NO_MAX);
    for (;;)
    {
	struct stat stb;
	int l;
	if (argc < 2) {
	    /* coverity[tainted_data] Safe, never handed to exec */
	    if (fgets(name, sizeof(name), stdin) == NULL)
		break;
	    l = strlen(name);
	    if (name[l-1] == '\n')
		name[l-1] = '\0';
	    file = name;
	} else {
	    file = argv[j++];
	    if (!file)
		break;
	}

	if (stat(file, &stb) != 0)
	    continue;
	else if (S_ISDIR(stb.st_mode) != 0)
	    continue;
	else if (!analyzer->promiscuous)
	{
	    char *end = file + strlen(file);
	    if (end - file < 2 || end[-1] != 'v' || end[-2] != ',')
		continue;
	    if (strstr(file, "CVSROOT") != NULL)
		continue;
	}
	forest->textsize += stb.st_size;

	fn = xcalloc(1, sizeof(rev_filename), "filename gathering");
	*fn_tail = fn;
	fn_tail = (rev_filename **)&fn->next;
	if (striplen > 0 && last != NULL) {
	    c = strcommonendingwith(file, last, '/');
	    if (c < striplen)
		striplen = c;
	} else if (striplen < 0) {
	    striplen = 0;
	    for (i = 0; i < strlen(file); i++)
		if (file[i] == '/')
		    striplen = i + 1;
	}
	fn->file = atom(file);
	last = fn->file;
	total_files++;
	if (progress && total_files % 100 == 0)
	    progress_jump(total_files);
    }
    forest->filecount = total_files;

    generators = xcalloc(sizeof(generator_t), total_files, "Generators");
    sorted_files = xmalloc(sizeof(rev_file) * total_files, "sorted_files");
    cvs_masters = xcalloc(total_files, sizeof(cvs_master), "cvs_masters");
    rev_masters = xmalloc(sizeof(rev_master) * total_files, "rev_masters");
    fn_n = total_files;
    i = 0;
    rev_filename *tn;
    for (fn = fn_head; fn; fn = tn) {
	tn = fn->next;
	sorted_files[i].name = fn->file;
	sorted_files[i++].rectified = atom_rectify_name(fn->file);
	free(fn);
    }
    qsort(sorted_files, total_files, sizeof(rev_file), file_compare);
	
    progress_end("done, %.3fKB in %d files",
		 (forest->textsize/1024.0), forest->filecount);

    /* things that must be visible to inner functions */
    load_current_file = 0;
    verbose = analyzer->verbose;

    /*
     * Analyze the files for CVS revision structure.
     *
     * The result of this analysis is a rev_list, each element of
     * which corresponds to a CVS master and points at a list of named
     * CVS branch heads (rev_refs), each one of which points at a list
     * of CVS commit structures (cvs_commit).
     */
#ifdef THREADS
    if (threads > 1)
	snprintf(name, sizeof(name), 
		 "Analyzing masters with %d threads...", threads);
    else
#endif /* THREADS */
	strcpy(name, "Analyzing masters...");
    progress_begin(name, total_files);
#ifdef THREADS
    if (threads > 1)
    {
	int i;

	workers = (pthread_t *)xcalloc(threads, sizeof(pthread_t), __func__);
	for (i = 0; i < threads; i++)
	    pthread_create(&workers[i], &attr, worker, NULL);

        /* Wait for all the threads to die off. */
	for (i = 0; i < threads; i++)
          pthread_join(workers[i], NULL);
        
	pthread_mutex_destroy(&enqueue_mutex);
	pthread_mutex_destroy(&revlist_mutex);
    }
    else
#endif /* THREADS */
	worker(NULL);
#ifdef FILESORT
    //    head = sort_cvs_masters((cvs_master *) head);
#endif /*FILESORT */
    progress_end("done, %d revisions", total_revisions);
    free(sorted_files);
    /* Later we'll teach everything to use the array */
    cvs_master *cm = NULL;
    for (i = total_files; i-- > 0;) {
	cvs_masters[i].next = cm;
	cm = &cvs_masters[i];
    }
    forest->errcount = err;
    forest->total_revisions = total_revisions;
    forest->skew_vulnerable = skew_vulnerable;
    forest->head = (rev_list*)cvs_masters;
    forest->generators = (generator_t *)generators;
}

/* end */

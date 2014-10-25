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
    volatile struct _rev_filename	*next;
    const char			*file;
} rev_filename;

/*
 * Ugh...least painful way to make some stuff that isn't thread-local
 * visible.
 */
static volatile int load_current_file;
static volatile rev_filename   *fn_head = NULL, **fn_tail = &fn_head, *fn;
static volatile cvs_master *head = NULL, **tail = &head;
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

static char *rectify_name(const char *raw, char *rectified, size_t rectlen)
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

static cvs_master *
rev_list_file(const char *name, analysis_t *out) 
{
    cvs_master	*cm;
    struct stat	buf;
    yyscan_t scanner;
    FILE *in;
    cvs_file *cvs;
    char rectified[PATH_MAX];

    in = fopen(name, "r");
    if (!in) {
	perror(name);
	++err;
	return NULL;
    }
    if (stat(name, &buf) == -1) {
	fatal_system_error("%s", name);
    }

    cvs = xcalloc(1, sizeof(cvs_file), __func__);
    cvs->gen.master_name = name;
    cvs->gen.expand = EXPANDUNSPEC;
    cvs->export_name = atom(rectify_name(name, rectified, sizeof(rectified)));
#ifdef MEMOSORT
    collect_path(cvs->export_name);
#endif /*MEMOSORT */
    cvs->mode = buf.st_mode;
    cvs->verbose = verbose;

    yylex_init(&scanner);
    yyset_in(in, scanner);
    yyparse(scanner, cvs);
    yylex_destroy(scanner);

    fclose(in);
    cm = cvs_master_digest(cvs);
    out->total_revisions = cvs->nversions;
    out->skew_vulnerable = cvs->skew_vulnerable;
    out->generator = cvs->gen;
    cvs_file_free(cvs);
    return cm;
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

static void *worker(void *arg)
/* consume masters off the queue */
{
    cvs_master *cm;
    analysis_t out = {0, 0};
    bool keepgoing = true;

    for (;;)
    {
	const char *filename;

	/* pop a master off the queue, terminating if none left */
#ifdef THREADS
	if (threads > 1)
	    pthread_mutex_lock(&enqueue_mutex);
#endif /* THREADS */
	if (fn_head == NULL)
	    keepgoing = false;
	else
	{
	    fn = fn_head;
	    fn_head = fn_head->next;
	    filename = fn->file;
	    free((rev_filename *)fn);
	}
#ifdef THREADS
	if (threads > 1)
	    pthread_mutex_unlock(&enqueue_mutex);
#endif /* THREADS */
	if (!keepgoing)
	    return(NULL);

	/* process it */
	cm = rev_list_file(filename, &out);

	/* pass it to the next stage */
#ifdef THREADS
	if (threads > 1)
	    pthread_mutex_lock(&revlist_mutex);
#endif /* THREADS */
	generators[load_current_file] = out.generator;
	progress_jump(++load_current_file);
	*tail = cm;
	total_revisions += out.total_revisions;
	if (out.skew_vulnerable > skew_vulnerable)
	    skew_vulnerable = out.skew_vulnerable;
	tail = (volatile cvs_master **)&cm->next;
#ifdef THREADS
	if (threads > 1)
	    pthread_mutex_unlock(&revlist_mutex);
#endif /* THREADS */
    }
}

void analyze_masters(int argc, char *argv[], 
			  import_options_t *analyzer, 
			  forest_t *forest)
/* main entry point; collect and parse CVS masters */
{
    char	    name[PATH_MAX];
    const char      *last = NULL;
    char	    *file;
    int		    j = 1;
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

	if (argc < 2) {
	    int l;
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
	}
	forest->textsize += stb.st_size;

	fn = xcalloc(1, sizeof(rev_filename), "filename gathering");
	*fn_tail = fn;
	fn_tail = (volatile rev_filename **)&fn->next;
	if (striplen > 0 && last != NULL) {
	    c = strcommonendingwith(file, last, '/');
	    if (c < striplen)
		striplen = c;
	} else if (striplen < 0) {
	    size_t i;

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

    generators = xcalloc(sizeof(generator_t), total_files, __func__);

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
    if (threads > 1)
	snprintf(name, sizeof(name), 
		 "Analyzing masters with %d threads...", threads);
    else
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
#ifdef MEMOSORT
    presort_paths();
#endif /*MEMOSORT */
    progress_end("done, %d revisions", total_revisions);

    forest->errcount = err;
    forest->total_revisions = total_revisions;
    forest->skew_vulnerable = skew_vulnerable;
    forest->head = (rev_list *)head;
    forest->generators = (generator_t *)generators;
}

/* end */

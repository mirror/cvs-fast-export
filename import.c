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
 * CVS master analysis.  Grinds out a revlist structure represnting
 * the entire CVS history of a collection.
 */

#define DEBUG_THREAD

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
static volatile rev_list *head = NULL, **tail = &head;
static volatile cvstime_t skew_vulnerable;
static volatile unsigned int total_revisions;
static volatile int err;

static int total_files;
static bool generate, enable_keyword_expansion, verbose;

#ifdef THREADS
static pthread_mutex_t progress_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t wakeup_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wakeup_cond;
#endif /* THREADS */

typedef struct _analysis {
    cvstime_t skew_vulnerable;
    unsigned int total_revisions;
} analysis_t;

static rev_list *
rev_list_file(const char *name, analysis_t *out) 
{
    rev_list	*rl;
    struct stat	buf;
    yyscan_t scanner;
    FILE *in;
    cvs_file *cvs;

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
    cvs->master_name = name;
    cvs->mode = buf.st_mode;

    yylex_init(&scanner);
    yyset_in(in, scanner);
    yyparse(scanner, cvs);
    yylex_destroy(scanner);

    fclose(in);
    rl = rev_list_cvs(cvs);
    if (generate)
	generate_files(cvs, enable_keyword_expansion, export_blob);
    out->total_revisions = cvs->total_revisions;
    out->skew_vulnerable = cvs->skew_vulnerable;
    cvs_file_free(cvs);
    return rl;
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

#ifdef THREADS
/*
 * A simple multithread scheduler to avoid stalling on I/O waits.
 *
 * Without threading, analysis of all CVS masters is stalled during
 * I/O waits.  The biggest non-IO bottleneck in the code is assembling
 * deltas into blobs. This lends itself to parallelization because at
 * this stage of analysis the mater files are all separate universes
 * (and will remain that way until branch merging).
 *
 * Instead of running the analyses seqentially, the threaded version
 * repeatedly tries to send each master to the worker pool until it
 * succeeds.  If all slots in the pool have active threads, it waits
 * for some worker thread to finish to retry the dispatch loop.
 *
 * After all files have been dispatched, any remainihg worker threads
 * are joined, so execution of thwe main program waits until thery're
 * all done.
 *
 * Beware of setting the worker pool size too high, the program's working set
 * can get large due to mapping the delta sequences from entire 
 * and potentially very large master files into memory.
 */

static pthread_mutex_t revlist_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t enqueue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t *workers;

#ifdef DEBUG_THREAD
static pthread_mutex_t announce_mutex = PTHREAD_MUTEX_INITIALIZER;

static void thread_announce(char const *format,...)
{
    if (verbose) {
	va_list args;

	pthread_mutex_lock(&announce_mutex);
	fprintf(stderr, "threading: ");
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	pthread_mutex_unlock(&announce_mutex);
    }
}
#else
static void thread_announce(char const *format,...)
{
}
#endif /* DEBUG_THREAD */

static void *thread_monitor(void *arg)
/* run forever popping master off the queue and analyzing them */
{
    rev_list *rl;
    pthread_t *slot = (pthread_t *)arg;
    analysis_t out;
    bool keepgoing = true;

    for (;;)
    {
	const char *filename;

	/* pop a master off the queue, terminating if none left */
	pthread_mutex_unlock(&enqueue_mutex);
	if (fn_head == NULL)
	    keepgoing = false;
	else
	{
	    fn = fn_head;
	    fn_head = fn_head->next;
	    filename = fn->file;
	    free((rev_filename *)fn);
	}
	pthread_mutex_unlock(&enqueue_mutex);
	if (!keepgoing)
	    pthread_exit(NULL);

	/* process it */
	thread_announce("slot %ld: %s begins\n", 
			slot - workers, filename);
	rl = rev_list_file(filename, &out);
	pthread_mutex_lock(&progress_mutex);
	progress_jump(load_current_file+1);
	pthread_mutex_unlock(&progress_mutex);

	/* pass it to the next stage */
	pthread_mutex_lock(&revlist_mutex);
	*tail = rl;
	total_revisions += out.total_revisions;
	if (out.skew_vulnerable > skew_vulnerable)
	    skew_vulnerable = out.skew_vulnerable;
	tail = (volatile rev_list **)&rl->next;
	pthread_mutex_unlock(&revlist_mutex);
	thread_announce("slot %ld: %s done (%d of %d)\n", 
			slot - workers, filename,
			load_current_file+1, total_files);

	/* signal a completion to the main thread  */
	pthread_mutex_lock(&wakeup_mutex);
	++load_current_file;
	pthread_cond_signal(&wakeup_cond);
	pthread_mutex_unlock(&wakeup_mutex);
    }
}
#endif /* THREADS */

rev_list *analyze_masters(int argc, char *argv[], 
			  const bool promiscuous,
			  const bool arg_enable_keyword_expansion, 
			  const bool arg_generate,
			  const bool arg_verbose,
			  stats_t *stats)
/* main entry point; collect and parse CVS masters */
{
    char	    name[10240];
    const char      *last = NULL;
    char	    *file;
    int		    j = 1;
    int		    c;
    analysis_t      out;
#ifdef THREADS
    pthread_attr_t  attr;
    int i;

    pthread_cond_init (&wakeup_cond, NULL);
    /* Initialize and set thread detached attribute */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
#endif /* THREADS */

    stats->textsize = stats->filecount = 0;
    progress_begin("Reading list of files...", NO_MAX);
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
	else if (!promiscuous)
	{
	    char *end = file + strlen(file);
	    if (end - file < 2 || end[-1] != 'v' || end[-2] != ',')
		continue;
	}
	stats->textsize += stb.st_size;

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
    stats->filecount = total_files;

    progress_end("done, %ldKB in %d files", 
		 (long)(stats->textsize/1024), stats->filecount);

    /* things that must be visible to inner functions */
    load_current_file = 0;
    enable_keyword_expansion = arg_enable_keyword_expansion;
    generate = arg_generate;
    verbose = arg_verbose;
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
    {
	progress_begin("Analyzing masters with %d threads...", 
		       total_files);
	workers = (pthread_t *)xcalloc(threads, sizeof(pthread_t), __func__);
	for (i = 0; i < threads; i++) {
	    pthread_create(&workers[i], &attr, 
			   thread_monitor, (void *)&workers[i]);
	}
	
	pthread_mutex_lock(&wakeup_mutex);
	while (load_current_file < total_files)
	    pthread_cond_wait(&wakeup_cond, &wakeup_mutex);
	pthread_mutex_unlock(&wakeup_mutex);

	pthread_mutex_destroy(&wakeup_mutex);
	pthread_mutex_destroy(&enqueue_mutex);
	pthread_mutex_destroy(&revlist_mutex);
    }
    else
#endif /* THREADS */
    {
	progress_begin("Analyzing masters...", total_files);
	while (fn_head) {
	    rev_list *rl;
	    fn = fn_head;
	    fn_head = fn_head->next;
	    if (verbose)
		announce("processing %s\n", fn->file);
	    rl = rev_list_file(fn->file, &out);
	    progress_jump(++load_current_file);
	    *tail = rl;
	    tail = (volatile rev_list **)&rl->next;
	    total_revisions += out.total_revisions;
	    if (out.skew_vulnerable > skew_vulnerable)
		    skew_vulnerable = out.skew_vulnerable;
	    free((void *)fn);
	}
    }
    progress_end("done, %d total revisions", total_revisions);

    stats->errcount = err;
    stats->total_revisions = total_revisions;
    stats->skew_vulnerable = skew_vulnerable;

    return (rev_list *)head;
}

/* end */

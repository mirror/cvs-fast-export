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

/*
 * Ugh...least painful way to make some stuff that isn't thread-local
 * visible.
 */
static int total_files, load_current_file;
static bool generate, enable_keyword_expansion, verbose;
static rev_list	*head = NULL, **tail = &head;
static int err;

#ifdef THREADS
static pthread_mutex_t progress_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif /* THREADS */

static rev_list *
rev_list_file(const char *name) 
{
    rev_list	*rl;
    struct stat	buf;
    yyscan_t scanner;
    cvs_file	*this_file;

    yylex_init(&scanner);
    /* coverity[leaked_storage] */
    yyset_in(fopen(name, "r"), scanner);
    if (!yyget_in(scanner)) {
	perror(name);
	++err;
	yylex_destroy(scanner);
	return NULL;
    }
    //yyset_lineno(0, scanner);
    this_file = xcalloc(1, sizeof(cvs_file), __func__);
    this_file->master_name = name;
    if (yyget_in(scanner) != NULL)
	assert(fstat(fileno(yyget_in(scanner)), &buf) == 0);
    this_file->mode = buf.st_mode;
    yyparse(scanner, this_file);
    fclose(yyget_in(scanner));
    yylex_destroy(scanner);
    rl = rev_list_cvs(this_file);
    if (generate)
	generate_files(this_file, enable_keyword_expansion, export_blob);
   
    cvs_file_free(this_file);
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

typedef struct _rev_filename {
    struct _rev_filename	*next;
    const char			*file;
} rev_filename;

#define PROGRESS_LEN	20

static void load_status(const char *name, int load_total_files, bool complete)
{
    int	spot = load_current_file * PROGRESS_LEN / load_total_files;
    int	    s;
    int	    l;

    l = strlen(name);
    if (l > 35) name += l - 35;

#ifdef THREADS
    pthread_mutex_lock(&progress_mutex);
#endif /* THREADS */
    if (complete)
	fprintf(STATUS, "\rDone: %35.35s ", name);
    else
	fprintf(STATUS, "\rLoad: %35.35s ", name);
    for (s = 0; s < PROGRESS_LEN + 1; s++)
	putc(s == spot ? '*' : '.', STATUS);
    fprintf(STATUS, " %5d of %5d ", load_current_file, load_total_files);
    fflush(STATUS);
#ifdef THREADS
    pthread_mutex_unlock(&progress_mutex);
#endif /* THREADS */
#ifdef DEBUG_THREAD
    if (verbose)
	announce("Status report complete\n");
#endif /* DEBUG_THREAD */
}

static void load_status_next(void)
{
    fprintf(STATUS, "\n");
    fflush(STATUS);
}

#if defined(THREADS)
/*
 * A simple multithread scheduler to avoid stalling on I/O waits.
 *
 * Without threading, analysis of all CVS masters is stalled during
 * I/O waits.  The biggest non-IO bottleneck in the code is assembling
 * deltas into blobs. This lends itself to parallelization because at
 * this stage of analysis the mater files are all separate universes
 * (and will remain that way until branch merging).
 *
 * This scheduler works with a fixed-sized worker thread pool.  In each
 * work cycle, it first tries to find an unused pool slot to assign
 * the next master in the list to.  If it does, it starts a thread 
 * analyzing that master and then immediately goes back to try
 * to schedule another.
 *
 * When the thread pool is full, the scheduler waits for some thread
 * to signal that it has completed before going back around the loop.
 * Thus no busy-waiting is required; a new masters is dispatched 
 * exactly as soon as a thread slot becomes available.
 */

#define THREAD_POOL_SIZE	128

#define DEBUG_THREAD

struct threadslot {
    pthread_t	    pt;
    pthread_mutex_t mutex;
    bool	    active;
    const char	    *filename;
};

static volatile int unprocessed;
static pthread_mutex_t scheduler_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t any_thread_finished = PTHREAD_COND_INITIALIZER;

static void *thread_monitor(void *arg)
/* do a master analysis, to be run inside a thread */
{
    rev_list *rl;
    struct threadslot *ctrl = (struct threadslot *)arg;
#ifdef DEBUG_THREAD
    if (verbose)
	announce("monitor processing of %s begins\n", ctrl->filename);
#endif /* DEBUG_THREAD */
    if (progress)
	load_status(ctrl->filename + striplen, total_files, false);
    rl = rev_list_file(ctrl->filename);
    ++load_current_file;
#ifdef DEBUG_THREAD
    if (verbose)
	announce("monitor processing of %s complete\n", ctrl->filename);
#endif /* DEBUG_THREAD */
    if (progress)
	load_status(ctrl->filename + striplen, total_files, true);
#ifdef DEBUG_THREAD
    if (verbose)
	announce("Waiting on threadslot mutex\n");
#endif /* DEBUG_THREAD */
    pthread_mutex_lock(&ctrl->mutex);
#ifdef DEBUG_THREAD
    if (verbose)
	announce("Acquired threadslot mutex\n");
#endif /* DEBUG_THREAD */
    --unprocessed;
    *tail = rl;
    tail = &rl->next;
    ctrl->active = false;
    pthread_mutex_unlock(&ctrl->mutex);
    pthread_cond_signal(&any_thread_finished);
#ifdef DEBUG_THREAD
    if (verbose)
	announce("Wakeup signal shipped\n");
#endif /* DEBUG_THREAD */
    pthread_exit(NULL);
}

static void threaded_dispatch(rev_filename *fn_head)
/* control threaded processing of a master file list */
{
    struct threadslot threadslots[THREAD_POOL_SIZE];
    rev_filename *fn;
    int i; 

    for (i = 0; i < THREAD_POOL_SIZE; i++) {
	threadslots[i].active = false;
	pthread_mutex_init(&threadslots[i].mutex, NULL);
    }
    unprocessed = total_files;
    do {
    schedule_another:
#ifdef DEBUG_THREAD
	if (verbose)
	    announce("About to schedule a master\n");
#endif /* DEBUG_THREAD */
	/* if un-dispatched masters remain, dispatch the next one */
	if (fn_head != NULL) {
	    for (i = 0; i < THREAD_POOL_SIZE; i++) {
		if (!threadslots[i].active) {
#ifdef DEBUG_THREAD
		    if (verbose)
			announce("Found slot %d\n", i);
#endif /* DEBUG_THREAD */
		    int retval = pthread_create(&threadslots[i].pt, 
						NULL, thread_monitor, 
						(void *)&threadslots[i]);
		    if (retval == 0) {
			fn = fn_head;
			fn_head = fn_head->next;
			pthread_mutex_lock(&threadslots[i].mutex);
			threadslots[i].active = true;
			threadslots[i].filename = fn->file;
			if (verbose)
			    announce("processing of %s scheduled\n", fn->file);
			pthread_mutex_unlock(&threadslots[i].mutex);
			free(fn);
			goto schedule_another;
		    } else {
			fprintf(STATUS, "Analysis thread creation failed!\n");
			exit(0);
		    }
		}
	    }

#ifdef DEBUG_THREAD
	    if (verbose)
		announce("Waiting on wakeup\n");
#endif /* DEBUG_THREAD */
	    /* wait for any one of the threads to terminate */
	    pthread_mutex_lock(&scheduler_mutex);
	    pthread_cond_wait(&any_thread_finished, &scheduler_mutex);
#ifdef DEBUG_THREAD
	    if (verbose)
		announce("Wakeup signal received\n");
#endif /* DEBUG_THREAD */
	}
    } while (unprocessed > 0);

    pthread_mutex_destroy(&progress_mutex);
    pthread_mutex_destroy(&scheduler_mutex);
    for (i = 0; i < THREAD_POOL_SIZE; i++)
	pthread_mutex_destroy(&threadslots[i].mutex);
    pthread_cond_destroy(&any_thread_finished);
}
#endif /* THREADS */

rev_list *analyze_masters(int argc, char *argv[], 
			  const bool promiscuous,
			  const bool arg_enable_keyword_expansion, 
			  const bool arg_generate,
			  const bool arg_verbose,
			  int *filecount, int *err)
/* main entry point; collect and parse CVS masters */
{
    rev_filename    *fn_head = NULL, **fn_tail = &fn_head, *fn;
    char	    name[10240];
    const char      *last = NULL;
    char	    *file;
    off_t	    textsize = 0;
    int		    j = 1;
    int		    c;

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
	else
	    textsize += stb.st_size;

	fn = xcalloc(1, sizeof(rev_filename), "filename gathering");
	*fn_tail = fn;
	fn_tail = &fn->next;
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
    progress_end("done, %ldKB in %d files", (long)(textsize/1024), total_files);
    *filecount = total_files;

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
#if defined(THREADS) && defined(__FUTURE__)
    threaded_dispatch(fn_head);
#else
    while (fn_head) {
	rev_list *rl;
	fn = fn_head;
	fn_head = fn_head->next;
	++load_current_file;
	if (verbose)
	    announce("processing %s\n", fn->file);
	if (progress)
	    load_status(fn->file + striplen, *filecount, false);
	rl = rev_list_file(fn->file);
	if (progress)
	    load_status(fn->file + striplen, *filecount, true);
	*tail = rl;
	tail = &rl->next;
	free(fn);
    }
#endif /* THREADS */
    if (progress)
	load_status_next();
    return head;
}

/* end */

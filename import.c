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
static int total_files;
static volatile int load_current_file;
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
 * Instead of running the analyses seqentially, the threaded version
 * repeatedly tries to send each master to the worker pool until it
 * succeeds.  If all slots in the pool have active threads, it retries
 * (implicitly waiting for some worker thread to finish) until it can
 * dispatch.

 * After all files have been dispatched, any remainihg worker threads
 * are joined, so execaution of thwe main program waits until thery're
 * all done.
 *
 * Beware of setting the worker pool size too high, the program's working set
 * can get large due to mapping entire delta sequences into memory.
 */

#define THREAD_POOL_SIZE	8

#define DEBUG_THREAD

struct threadslot {
    pthread_t	    thread;
    pthread_mutex_t mutex;
    const char	    *filename;
};

static pthread_mutex_t revlist_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct threadslot threadslots[THREAD_POOL_SIZE];

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
/* do a master analysis, to be run inside a thread */
{
    rev_list *rl;
    struct threadslot *slot = (struct threadslot *)arg;
    thread_announce("slot %ld: %s begins\n", 
		    slot - threadslots, slot->filename);
    if (progress)
	load_status(slot->filename + striplen, total_files, false);
    rl = rev_list_file(slot->filename);
    if (progress)
	load_status(slot->filename + striplen, total_files, true);
    pthread_mutex_lock(&revlist_mutex);
    ++load_current_file;
    *tail = rl;
    tail = &rl->next;
    pthread_mutex_unlock(&revlist_mutex);
    pthread_mutex_unlock(&slot->mutex);
    thread_announce("slot %ld: %s done (%d of %d)\n", 
		    slot - threadslots, slot->filename,
		    load_current_file, total_files);
    pthread_exit(NULL);
}
#endif /* THREADS */

rev_list *analyze_masters(int argc, char *argv[], 
			  const bool promiscuous,
			  const bool arg_enable_keyword_expansion, 
			  const bool arg_generate,
			  const bool arg_verbose,
			  const int threads,
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
#ifdef THREADS
    int i;

    for (i = 0; i < THREAD_POOL_SIZE; i++) {
	pthread_mutex_init(&threadslots[i].mutex, NULL);
    }
#endif /* THREADS */

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
    while (fn_head) {
	fn = fn_head;
	fn_head = fn_head->next;
#if defined(THREADS)
	if (threads > 1) {
	    for (;;) {
		for (i = 0; i < THREAD_POOL_SIZE; i++) {
		    if (pthread_mutex_trylock(&threadslots[i].mutex) == 0) {
			threadslots[i].filename = fn->file;
			j = pthread_create(&threadslots[i].thread, 
					   NULL, thread_monitor, 
					   (void *)&threadslots[i]);
			if (j == 0) {
			    goto dispatched;
			} else {
			    thread_announce("Analysis thread creation failed!\n");
			    exit(0);
			}
		    }
		}
	    }
	dispatched:
	    ;
	}
	else
#endif /* THREADS */
	{
	    rev_list *rl;
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
	}
	free(fn);
    }
    if (progress)
	load_status_next();

#ifdef THREADS
    /* wait on all threads still running before continuing */
    for (i = 0; i < THREAD_POOL_SIZE; i++)
	pthread_join(threadslots[i].thread, NULL);
    pthread_mutex_destroy(&revlist_mutex);
    for (i = 0; i < THREAD_POOL_SIZE; i++)
	pthread_mutex_destroy(&threadslots[i].mutex);
#endif /* THREADS */

    return head;
}

/* end */

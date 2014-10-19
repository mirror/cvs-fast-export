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
#include "cvs.h"
#include <unistd.h>
#include <getopt.h>
#include <regex.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/resource.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif /* __GLIBC__ */

/* options */
int commit_time_window = 300;
bool progress = false;

static import_options_t import_options = {
    .striplen = -1,
};

static int get_int_substr(const char * str, const regmatch_t * p)
{
    char buff[256];
    if (p->rm_so == -1)
	    return 0;
    if (p->rm_eo - p->rm_so >= sizeof(buff))
	    return 0;
    memcpy(buff, str + p->rm_so, p->rm_eo - p->rm_so);
    buff[p->rm_eo - p->rm_so] = 0;
    return atoi(buff);
}

static time_t mktime_utc(const struct tm * tm, const char* tzbuf)
{
    /* coverity[tainted_string_return_content] */
    char * old_tz = getenv("TZ");
    time_t ret;

    setenv("TZ", tzbuf, 1);

    tzset();

    ret = mktime((struct tm *)tm);

    if (old_tz)
	setenv("TZ", old_tz, 1);
    else
	unsetenv("TZ");

    tzset();

    return ret;
}

static time_t convert_date(const char *dte)
/* accept a date in anything close to RFC3339 form */
{
    static regex_t date_re;
    static bool init_re;

#define MAX_MATCH 16
    size_t nmatch = MAX_MATCH;
    regmatch_t match[MAX_MATCH];

    if (!init_re)
    {
	if (regcomp(&date_re, "([0-9]{4})[-/]([0-9]{2})[-/]([0-9]{2})[ T]([0-9]{2}):([0-9]{2}):([0-9]{2})( [-+][0-9]{4})?", REG_EXTENDED)) 
	    fatal_error("date regex compilation error\n");
	init_re = true;
    }

    if (regexec(&date_re, dte, nmatch, match, 0) == 0)
    {
	regmatch_t * pm = match;
	struct tm tm = {0};
	char tzbuf[32];
	int offseth, offsetm;

	/* first regmatch_t is match location of entire re */
	pm++;

	tm.tm_year = get_int_substr(dte, pm++);
	tm.tm_mon  = get_int_substr(dte, pm++);
	tm.tm_mday = get_int_substr(dte, pm++);
	tm.tm_hour = get_int_substr(dte, pm++);
	tm.tm_min  = get_int_substr(dte, pm++);
	tm.tm_sec  = get_int_substr(dte, pm++);
	offseth    = -get_int_substr(dte, pm++);

	offsetm = offseth % 100;
	if (offsetm < 0)
	    offsetm *= -1;
	offseth /= 100;
	snprintf(tzbuf, sizeof(tzbuf), "UTC%+d:%d", offseth, offsetm);

	tm.tm_year -= 1900;
	tm.tm_mon--;

	return mktime_utc(&tm, tzbuf);
    }
    else
    {
	return atoi(dte);
    }
}

static void print_sizes(void)
{
    printf("sizeof(char *)        = %zu\n", sizeof(char *));
    printf("sizeof(long)          = %zu\n", sizeof(long));
    printf("sizeof(int)           = %zu\n", sizeof(int));
    printf("sizeof(short)         = %zu\n", sizeof(short));
    printf("sizeof(mode_t)        = %zu\n", sizeof(mode_t));
    printf("sizeof(cvstime_t)     = %zu\n", sizeof(cvstime_t));
    printf("sizeof(time_t)        = %zu\n", sizeof(time_t));
    printf("sizeof(cvs_number)    = %zu\n", sizeof(cvs_number));
    printf("sizeof(node_t)        = %zu\n", sizeof(node_t));
    printf("sizeof(cvs_symbol)    = %zu\n", sizeof(cvs_symbol));
    printf("sizeof(cvs_branch)    = %zu\n", sizeof(cvs_branch));
    printf("sizeof(cvs_version)   = %zu\n", sizeof(cvs_version));
    printf("sizeof(cvs_patch)     = %zu\n", sizeof(cvs_patch));
    printf("sizeof(nodehash_t)    = %zu\n", sizeof(nodehash_t));
    printf("sizeof(editbuffer_t)  = %zu\n", sizeof(editbuffer_t));
    printf("sizeof(cvs_file)      = %zu\n", sizeof(cvs_file));
    printf("sizeof(rev_master)    = %zu\n", sizeof(rev_master));
    printf("sizeof(rev_file)      = %zu\n", sizeof(rev_file));
    printf("sizeof(rev_dir)       = %zu\n", sizeof(rev_dir));
    printf("sizeof(bloom_t)       = %zu\n", sizeof(bloom_t));
    printf("sizeof(cvs_commit)    = %zu\n", sizeof(cvs_commit));
    printf("sizeof(git_commit)    = %zu\n", sizeof(git_commit));
    printf("sizeof(rev_ref)       = %zu\n", sizeof(rev_ref));
    printf("sizeof(rev_list)      = %zu\n", sizeof(rev_list));
    printf("sizeof(rev_file_list) = %zu\n", sizeof(rev_file_list));
    printf("sizeof(rev_diff)      = %zu\n", sizeof(rev_diff));
    printf("sizeof(cvs_author)    = %zu\n", sizeof(cvs_author));
    printf("sizeof(chunk_t)       = %zu\n", sizeof(chunk_t));
    printf("sizeof(Tag)           = %zu\n", sizeof(tag_t));
}

int
main(int argc, char **argv)
{
    typedef enum _execution_mode {
	ExecuteExport, ExecuteGraph, ExecuteAuthors,
    } execution_mode;

    rev_list	    *premerge;
    execution_mode  exec_mode = ExecuteExport;
    forest_t        forest;
    export_options_t export_options = {
	.branch_prefix = "refs/heads/",
    };
    export_stats_t	export_stats;

#if defined(__GLIBC__)
    /* 
     * The default sbrk call grabs memory from the OS in 128kb chunks
     * for malloc. As we use up memory in prodigous amounts it is
     * better to grab it in bigger chunks, and make less calls to
     * sbrk. 16MB seems to be a decent value.
     */
    mallopt(M_TOP_PAD,16*1024*1024); /* grab memory in 16MB chunks */
#endif /* __GLIBC__ */
    clock_gettime(CLOCK_REALTIME, &export_options.start_time);
    memset(&export_stats, '\0', sizeof(export_stats_t));

    /* force times using mktime to be interpreted in UTC */
    setenv("TZ", "UTC", 1);

    while (1) {
	static const struct option options[] = {
	    { "help",		    0, 0, 'h' },
	    { "version",	    0, 0, 'V' },
	    { "verbose",	    0, 0, 'v' },
	    { "commit-time-window", 1, 0, 'w' },
	    { "authormap",          1, 0, 'A' },
	    { "authorlist",         1, 0, 'a' },
	    { "revision-map",       1, 0, 'R' },
	    { "reposurgeon",        0, 0, 'r' },
            { "graph",              0, 0, 'g' },
            { "remote",             1, 0, 'e' },
            { "strip",              1, 0, 's' },
            { "progress",           0, 0, 'p' },
            { "promiscuous",        0, 0, 'P' },
            { "incremental",        1, 0, 'i' },
            { "threads",	    0, 0, 't' },
            { "canonical",          0, 0, 'C' },
            { "fast",               0, 0, 'F' },
            { "embed-id",           0, 0, 'E' },
	    { "sizes",              0, 0, 'S' },	/* undocumented */
	};
	int c = getopt_long(argc, argv, "+hVw:grvaA:R:Tke:s:pPi:t:CFSE", options, NULL);
	if (c < 0)
	    break;
	switch(c) {
	case 'h':
	    printf("Usage: cvs-fast-export [OPTIONS] [FILE]...\n"
		   "Parse RCS files and emit a fast-import stream.\n\n"
                   "Mandatory arguments to long options are mandatory for short options too.\n"
                   " -h --help                       This help\n"
		   " -g --graph                      Dump the commit graph\n"
		   " -k                              Enable keyword expansion\n"
                   " -V --version                    Print version\n"
                   " -w --commit-time-window=WINDOW  Time window for commits(seconds)\n"
		   " -a --authorlist                 Author map file\n"
		   " -A --authormap                  Author map file\n"
		   " -R --revision-map               Revision map file\n"
		   " -r --reposurgeon                Issue cvs-revision properties\n"
		   " -T                              Force deteministic dates\n"
                   " -e --remote                     Relocate branches to refs/remotes/REMOTE\n"
                   " -s --strip                      Strip the given prefix instead of longest common prefix\n"
		   " -p --progress                   Enable load-status reporting\n"
		   " -P --promiscuous                Process files without ,v extension\n"
		   " -v --verbose                    Show verbose progress messages\n"
		   " -i --incremental TIME           Incremental dump beginning after specified RFC3339-format time.\n"
		   " -t --threads N                  Use threaded scheduler for CVS master analyses.\n"
		   " -E --embed-id                   Embed CVS revisions in the commit messages.\n"
		   "\n"
		   "Example: find | cvs-fast-export\n");
	    return 0;
	case 'g':
	    exec_mode = ExecuteGraph;
	    break;
        case 'k':
	    export_options.enable_keyword_expansion = true;
	    break;
	case 'v':
	    import_options.verbose = true;
#ifdef YYDEBUG
	    extern int yydebug;
	    yydebug = 1;
#endif /* YYDEBUG */
	    break;
	case 'V':
	    printf("%s: version " VERSION "\n", argv[0]);
	    return 0;
	case 'w':
	    commit_time_window = atoi(optarg);
	    break;
	case 'a':
	    exec_mode = ExecuteAuthors;
	    break;
	case 'A':
	    load_author_map(optarg);
	    break;
	case 'R':
	    export_options.revision_map = fopen(optarg, "w");
	    if (export_options.revision_map == NULL)
		fatal_error("cannot open %s for revision-map write", optarg);
	    break;
	case 'r':
	    export_options.reposurgeon = true;
	    break;
	case 'E':
	    export_options.embed_ids = true;
	    break;
	case 'T':
	    export_options.force_dates = true;
	    break;
	case 'e':
	    export_options.branch_prefix = (char*)xmalloc(strlen(optarg)+15, __func__);
	    sprintf(export_options.branch_prefix, "refs/remotes/%s/", optarg);
	    break;
	case 's':
	    import_options.striplen = strlen(optarg) + 1;
	    break;
	case 'p':
	    progress = true;
	    break;
	case 'i':
	    export_options.fromtime = convert_date(optarg);
	    break;
	case 't':
#ifdef THREADS
	    threads = atoi(optarg);
#else
	    announce("not built with thread support, -t option ignored.\n");
#endif
	    break;
	case 'C':
	    export_options.reportmode = canonical;
	    break;
	case 'F':
	    export_options.reportmode = fast;
	    break;
	case 'S':
	    print_sizes();
	    return 0;
	default: /* error message already emitted */
	    announce("try `%s --help' for more information.\n", argv[0]);
	    return 1;
	}
    }

    if (export_options.reposurgeon) {
	if (export_options.embed_ids)
	    fatal_error("The options --reposurgeon and --embed-id cannot be combined.\n");
    } else if (!export_options.embed_ids) {
	if (export_options.revision_map != NULL)
	    announce("Note, the option --revision-map requires --reposurgeon or --embed-id.\n");
    }

    argv[optind-1] = argv[0];
    argv += optind-1;
    argc -= optind-1;

    /* build CVS structures by parsing masters; may read stdin */
    analyze_masters(argc, argv, &import_options, &forest);

    /* commit set coalescence happens here */
    forest.head = rev_list_merge(premerge = forest.head);
#ifdef ORDERDEBUG2
    dump_rev_tree(head, stderr);
#endif /* ORDERDEBUG2 */

    /* report on the DAG */
    if (forest.head) {
	switch(exec_mode) {
	case ExecuteGraph:
	    dump_rev_graph(forest.head, NULL);
	    break;
	case ExecuteAuthors:
	    export_authors(&forest, &export_options);
	    break;
	case ExecuteExport:
	    export_commits(&forest, &export_options, &export_stats);
	    if (export_options.revision_map != NULL)
		fclose(export_options.revision_map);
	    break;
	}
    }

    if (progress)
    {
	struct timespec now;
	struct rusage rusage;
	float elapsed;

	(void)clock_gettime(CLOCK_REALTIME, &now);
	(void)getrusage(RUSAGE_SELF, &rusage);
	elapsed = seconds_diff(&now, &export_options.start_time);
	fprintf(STATUS, "%ld commits/%.3fM text in %.6fs (%d commits/sec) using %ldKb.\n",
		export_stats.export_total_commits,
		export_stats.snapsize / 1000000.0,
		elapsed,
		(int)(export_stats.export_total_commits / elapsed),
		rusage.ru_maxrss);
    }

    discard_atoms();
    discard_tags();
    rev_free_dirs();
    free_author_map();
    return forest.errcount > 0;
}

/* end */

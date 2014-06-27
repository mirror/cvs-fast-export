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
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <getopt.h>
#include <regex.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif /* __GLIBC__ */

#ifndef MAXPATHLEN
#define MAXPATHLEN  10240
#endif

typedef enum _rev_execution_mode {
    ExecuteExport, ExecuteGraph,
} rev_execution_mode;

/* options */
int commit_time_window = 300;
bool force_dates = false;
bool enable_keyword_expansion = false;
bool reposurgeon;
FILE *revision_map;
char *branch_prefix = "refs/heads/";
bool progress = false;
bool branchorder = false;
time_t start_time;

static int verbose = 0;
static rev_execution_mode rev_mode = ExecuteExport;

char *
stringify_revision (char *name, char *sep, cvs_number *number)
/* stringify a revision number */
{
    static char result[BUFSIZ];

    if (name != NULL)
    {
	if (strlen(name) >= sizeof(result) - strlen(sep) - 1)
	    fatal_error("filename too long");
	strncpy(result, name, sizeof(result) - strlen(sep) - 1);
	strcat(result, sep);
    }

    if (number) 
    {
	int i;
	char digits[32];

	for (i = 0; i < number->c; i++) {
	    snprintf (digits, sizeof(digits)-1, "%d", number->n[i]);
	    if (strlen(result) + 1 + strlen(digits) >= sizeof(result))
		fatal_error("Revision number too long\n");
	    strcat(result, digits);
	    if (i < number->c - 1)
		strcat (result, ".");
	}
    }

    return result;
}

void
dump_number_file (FILE *f, char *name, cvs_number *number)
/* dump a filename/CVS-version pair to a specified file pointer */
{
    fputs(stringify_revision(name, " ", number), f);
}

void
dump_number (char *name, cvs_number *number)
/* dump a filename/CVS-version pair to standard output */
{
    dump_number_file (stdout, name, number);
}

#ifdef __UNUSED__
void
dump_git_commit (git_commit *c)
/* dump all delta/revision pairs associated with a gitspace commit */
{
    rev_file	*f;
    int		i, j;

    for (i = 0; i < c->ndirs; i++) {
	rev_dir	*dir = c->dirs[i];
	
	for (j = 0; j < dir->nfiles; j++) {
	    f = dir->files[j];
	    dump_number (f->name, &f->number);
	    printf (" ");
	}
    }
    printf ("\n");
}

void
dump_rev_head (rev_ref *h)
/* dump all gitspace commits associated wit the specified head */
{
    git_commit	*c;
    for (c = h->commit; c; c = c->parent) {
	dump_git_commit (c);
	if (c->tail)
	    break;
    }
}

void
dump_rev_list (rev_list *rl)
/* dump an entire revision list */
{
    rev_ref	*h;

    for (h = rl->heads; h; h = h->next) {
	if (h->tail)
	    continue;
	dump_rev_head (h);
    }
}
#endif /* __UNUSED__ */

char *
ctime_nonl (cvstime_t *date)
/* ctime(3) with trailing \n removed */
{
    time_t	udate = RCS_EPOCH + *date;
    char	*d = ctime (&udate);
    
    d[strlen(d)-1] = '\0';
    return d;
}

extern FILE *yyin;
static int err = 0;
char *yyfilename;
extern int yylineno;

cvs_file	*this_file;

static rev_list *
rev_list_file (char *name, int *nversions)
{
    rev_list	*rl;
    struct stat	buf;

    yyin = fopen (name, "r");
    if (!yyin) {
	perror (name);
	++err;
	return NULL;
    }
    yyfilename = name;
    yylineno = 0;
    this_file = xcalloc (1, sizeof (cvs_file), __func__);
    this_file->name = name;
    if (yyin)
	assert (fstat (fileno (yyin), &buf) == 0);
    this_file->mode = buf.st_mode;
    yyparse ();
    fclose (yyin);
    yyfilename = 0;
    rl = rev_list_cvs (this_file);
    if (rev_mode == ExecuteExport)
	generate_files(this_file, export_blob);
   
    *nversions = this_file->nversions;
    cvs_file_free (this_file);
    return rl;
}

#ifdef __UNUSED__
void
dump_rev_tree (rev_list *rl)
{
    rev_ref	*h;
    rev_ref	*oh;
    git_commit	*c, *p;
    int		tail;

    printf ("rev_list {\n");

    for (h = rl->heads; h; h = h->next) {
	if (h->tail)
	    continue;
	for (oh = rl->heads; oh; oh = oh->next) {
	    if (h->commit == oh->commit)
		printf ("%s:\n", oh->name);
	}
	printf ("\t{\n");
	tail = h->tail;
	for (c = h->commit; c; c = p) {
	    printf ("\t\t%p ", c);
	    dump_log (stdout, c->log);
	    if (tail) {
		printf ("\n\t\t...\n");
		break;
	    }
	    printf (" {\n");
	    
	    p = c->parent;
#if 0
	    if (p && c->nfiles > 16) {
		rev_file	*ef, *pf;
		int		ei, pi;
		ei = pi = 0;
		while (ei < c->nfiles && pi < p->nfiles) {
		    ef = c->files[ei];
		    pf = p->files[pi];
		    if (ef != pf) {
			if (rev_file_later (ef, pf)) {
			    fprintf (stdout, "+ ");
			    dump_number_file (stdout, ef->name, &ef->number);
			    ei++;
			} else {
			    fprintf (stdout, "- ");
			    dump_number_file (stdout, pf->name, &pf->number);
			    pi++;
			}
			fprintf (stdout, "\n");
		    } else {
			ei++;
			pi++;
		    }
		}
		while (ei < c->nfiles) {
		    ef = c->files[ei];
		    fprintf (stdout, "+ ");
		    dump_number_file (stdout, ef->name, &ef->number);
		    ei++;
		    fprintf (stdout, "\n");
		}
		while (pi < p->nfiles) {
		    pf = p->files[pi];
		    fprintf (stdout, "- ");
		    dump_number_file (stdout, pf->name, &pf->number);
		    pi++;
		    fprintf (stdout, "\n");
		}
	    } else {
		for (i = 0; i < c->nfiles; i++) {
		    printf ("\t\t\t");
		    dump_number (c->files[i]->name, &c->files[i]->number);
		    printf ("\n");
		}
	    }
#endif
	    printf ("\t\t}\n");
	    tail = c->tail;
	}
	printf ("\t}\n");
    }
    printf ("}\n");
}

static int
strcommon (char *a, char *b)
/* return the length of the common prefix of strings a and b */
{
    int	c = 0;
    
    while (*a == *b) {
	if (!*a)
	    break;
	a++;
	b++;
	c++;
    }
    return c;
}
#endif /* __UNUSED__ */

static int
strcommonendingwith(char *a, char *b, char endc)
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

static int get_int_substr(const char * str, const regmatch_t * p)
{
    char buff[256];
    if(p->rm_so == -1)
	    return 0;
    if(p->rm_eo - p->rm_so >= sizeof(buff))
	    return 0;
    memcpy(buff, str + p->rm_so, p->rm_eo - p->rm_so);
    buff[p->rm_eo - p->rm_so] = 0;
    return atoi(buff);
}

static time_t mktime_utc(struct tm * tm, const char* tzbuf)
{
    /* coverity[tainted_string_return_content] */
    char * old_tz = getenv("TZ");
    time_t ret;

    setenv("TZ", tzbuf, 1);

    tzset();

    ret = mktime(tm);

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
	if(offsetm < 0)
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
    printf("sizeof(cvstime_t)     = %zu\n", sizeof(cvstime_t));
    printf("sizeof(time_t)        = %zu\n", sizeof(time_t));
    printf("sizeof(cvs_number)    = %zu\n", sizeof(cvs_number));
    printf("sizeof(Node)          = %zu\n", sizeof(Node));
    printf("sizeof(cvs_symbol)    = %zu\n", sizeof(cvs_symbol));
    printf("sizeof(cvs_branch)    = %zu\n", sizeof(cvs_branch));
    printf("sizeof(cvs_version)   = %zu\n", sizeof(cvs_version));
    printf("sizeof(cvs_patch)     = %zu\n", sizeof(cvs_patch));
    printf("sizeof(cvs_file)      = %zu\n", sizeof(cvs_file));
    printf("sizeof(rev_file)      = %zu\n", sizeof(rev_file));
    printf("sizeof(rev_dir)       = %zu\n", sizeof(rev_dir));
    printf("sizeof(cvs_commit)    = %zu\n", sizeof(cvs_commit));
    printf("sizeof(git_commit)    = %zu\n", sizeof(git_commit));
    printf("sizeof(rev_ref)       = %zu\n", sizeof(rev_ref));
    printf("sizeof(rev_list)      = %zu\n", sizeof(rev_list));
    printf("sizeof(rev_file_list) = %zu\n", sizeof(rev_file_list));
    printf("sizeof(rev_diff)      = %zu\n", sizeof(rev_diff));
    printf("sizeof(cvs_author)    = %zu\n", sizeof(cvs_author));
    printf("sizeof(Chunk)         = %zu\n", sizeof(Chunk));
    printf("sizeof(Tag)           = %zu\n", sizeof(Tag));
}

typedef struct _rev_filename {
    struct _rev_filename	*next;
    char		*file;
} rev_filename;

int load_current_file, load_total_files;

int
main (int argc, char **argv)
{
    rev_filename    *fn_head, **fn_tail = &fn_head, *fn;
    rev_list	    *head, **tail = &head;
    rev_list	    *rl;
    int		    j = 1;
    char	    name[10240], *last = NULL;
    int		    strip = -1;
    int		    c;
    char	    *file;
    int		    nfile = 0;
    time_t          fromtime = 0;
    off_t	    textsize = 0;

#if defined(__GLIBC__)
    /* 
     * The default sbrk call grabs memory from the OS in 128kb chunks
     * for malloc. As we use up memory in prodigous amounts it is
     * better to grab it in bigger chunks, and make less calls to
     * sbrk. 16MB seems to be a decent value.
     */
    mallopt(M_TOP_PAD,16*1024*1024); /* grab memory in 16MB chunks */
#endif /* __GLIBC__ */
    start_time = time(NULL);

    /* force times using mktime to be interpreted in UTC */
    setenv ("TZ", "UTC", 1);

    while (1) {
	static struct option options[] = {
	    { "help",		    0, 0, 'h' },
	    { "version",	    0, 0, 'V' },
	    { "verbose",	    0, 0, 'v' },
	    { "commit-time-window", 1, 0, 'w' },
	    { "author-map",         1, 0, 'A' },
	    { "revision-map",       1, 0, 'R' },
	    { "reposurgeon",        0, 0, 'r' },
            { "graph",              0, 0, 'g' },
            { "remote",             1, 0, 'e' },
            { "strip",              1, 0, 's' },
            { "progress",           0, 0, 'p' },
            { "incremental",        1, 0, 'i' },
            { "branchorder",        0, 0, 'B' },	/* undocumented */
	    { "sizes",              0, 0, 'S' },	/* undocumented */
	};
	int c = getopt_long(argc, argv, "+hVw:grvA:R:Tke:s:pi:BS", options, NULL);
	if (c < 0)
	    break;
	switch (c) {
	case 'h':
	    printf("Usage: cvs-fast-export [OPTIONS] [FILE]...\n"
		   "Parse RCS files and emit a fast-import stream.\n\n"
                   "Mandatory arguments to long options are mandatory for short options too.\n"
                   " -h --help                       This help\n"
		   " -g --graph                      Dump the commit graph\n"
		   " -k                              Enable keyword expansion\n"
                   " -V --version                    Print version\n"
                   " -w --commit-time-window=WINDOW  Time window for commits (seconds)\n"
		   " -A --authormap                  Author map file\n"
		   " -R --revision-map               Revision map file\n"
		   " -r --reposurgeon                Issue cvs-revision properties\n"
		   " -T                              Force deteministic dates\n"
                   " -e --remote                     Relocate branches to refs/remotes/REMOTE\n"
                   " -s --strip                      Strip the given prefix instead of longest common prefix\n"
		   " -p --progress                   Enable load-status reporting\n"
		   " -v --verbose                    Show verbose progress messages\n"
		   " -i --incremental TIME           Incremental dump beginning after specified RFC3339-format time.\n"
		   "\n"
		   "Example: find -name '*,v' | cvs-fast-export\n");
	    return 0;
	case 'g':
	    rev_mode = ExecuteGraph;
	    break;
        case 'k':
	    enable_keyword_expansion = true;
	    break;
	case 'v':
	    verbose++;
#ifdef YYDEBUG
	    extern int yydebug;
	    yydebug = 1;
#endif /* YYDEBUG */
	    break;
	case 'V':
	    printf("%s: version " VERSION "\n", argv[0]);
	    return 0;
	case 'w':
	    commit_time_window = atoi (optarg);
	    break;
	case 'A':
	    load_author_map (optarg);
	    break;
	case 'R':
	    revision_map = fopen(optarg, "w");
	    break;
	case 'r':
	    reposurgeon = true;
	    break;
	case 'T':
	    force_dates = true;
	    break;
	case 'e':
	    branch_prefix = (char*) xmalloc(strlen(optarg) + 15, __func__);
	    sprintf(branch_prefix, "refs/remotes/%s/", optarg);
	    break;
	case 's':
	    strip = strlen(optarg) + 1;
	    break;
	case 'p':
	    progress = true;
	    break;
	case 'i':
	    fromtime = convert_date(optarg);
	    break;
	case 'B':
	    branchorder = true;
	    break;
	case 'S':
	    print_sizes();
	    return 0;
	default: /* error message already emitted */
	    announce("try `%s --help' for more information.\n", argv[0]);
	    return 1;
	}
    }

    argv[optind-1] = argv[0];
    argv += optind-1;
    argc -= optind-1;

    progress_begin("Reading list of files...", NO_MAX);
    for (;;)
    {
	struct stat stb;

	if (argc < 2) {
	    int l;
	    /* coverity[tainted_data] Safe, never handed to exec */
	    if (fgets (name, sizeof (name) - 1, stdin) == NULL)
		break;
	    l = strlen (name);
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
	else
	    textsize += stb.st_size;

	fn = xcalloc (1, sizeof (rev_filename), "filename gathering");
	fn->file = atom (file);
	*fn_tail = fn;
	fn_tail = &fn->next;
	if (strip > 0 && last != NULL) {
	    c = strcommonendingwith (fn->file, last, '/');
	    if (c < strip)
		strip = c;
	} else if (strip < 0) {
	    size_t i;

	    strip = 0;
	    for (i = 0; i < strlen (fn->file); i++)
		if (fn->file[i] == '/')
		    strip = i + 1;
	}
	last = fn->file;
	nfile++;
	if (progress && nfile % 100 == 0)
	    progress_jump(nfile);
    }
    progress_end("done, %ldKB in %d files", (long)(textsize/1024), nfile);
    if (rev_mode == ExecuteExport)
	export_init();
    load_total_files = nfile;
    load_current_file = 0;
    /* analyze the files for CVS revision structure */
    while (fn_head) {
	int nversions;
	
	fn = fn_head;
	fn_head = fn_head->next;
	++load_current_file;
	if (verbose)
	    announce("processing %s\n", fn->file);
	if (progress)
	    load_status (fn->file + strip);
	rl = rev_list_file (fn->file, &nversions);
	*tail = rl;
	tail = &rl->next;

	free(fn);
    }
    if (progress)
	load_status_next ();
    /* commit set coalescence happens here */
    rl = rev_list_merge (head);
    /* report on the DAG */
    if (rl) {
	switch (rev_mode) {
	case ExecuteGraph:
	    dump_rev_graph (rl, NULL);
	    break;
	case ExecuteExport:
	    export_commits (rl, strip, fromtime, progress);
	    break;
	}
    }
    if (skew_vulnerable > 0 && load_total_files > 1 && !force_dates) {
	time_t udate = RCS_EPOCH + skew_vulnerable;
	announce("commits before this date lack commitids: %s",	ctime(&udate));
    }
    if (rl)
	rev_list_free (rl, 0);
    while (head) {
	rl = head;
	head = head->next;
	rev_list_free (rl, 1);
    }
    discard_atoms ();
    discard_tags ();
    rev_free_dirs ();
    git_commit_cleanup ();
    export_wrap();
    free_author_map ();
    if (revision_map)
	fclose(revision_map);
    return err;
}

/* end */

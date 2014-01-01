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

#ifndef _CVS_H_
#define _CVS_H_

#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdbool.h>

/*
 * Use _printflike(M, N) to mark functions that take printf-like formats
 * and parameter lists.  M refers to the format arg, and N refers to
 * the first variable arg (typically M+1), or N = 0 if the function takes
 * a va_list.
 *
 * If the compiler is GCC version 2.7 or later, this is implemented
 * using __attribute__((__format__(...))).
 */
#if defined(__GNUC__) \
    && (__GNUC__ > 2) || (__GNUC__ == 2 && __GNUC_MINOR__ >= 7)
#define _printflike(fmtarg, firstvararg)       \
            __attribute__((__format__(__printf__, fmtarg, firstvararg)))
#else
#define _printflike(fmtarg, firstvararg)       /* nothing */
#endif

#ifndef MAXPATHLEN
#define MAXPATHLEN  10240
#endif

/* 
 * CVS_MAX_BRANCHWIDTH should match the number in the longrev test.
 * If it goes above 128 some bitfields widths in rev_ref must increase.
 */
#define CVS_MAX_BRANCHWIDTH	10
#define CVS_MAX_DEPTH		(2*CVS_MAX_BRANCHWIDTH + 2)
#define CVS_MAX_REV_LEN	(CVS_MAX_DEPTH * 11)

/*
 * Use instead of bool in frequently used structures to reduce
 * working-set size.
 */
typedef char flag;

/*
 * On 64-bit Linux a time_t is 8 bytes.  We want to reduce memory
 * footprint; by storing dates as 32-bit offsets from the beginning of
 * 1982 (the year RCS was released) we can cover dates all the way to
 * 2118-02-07T06:28:15 in half that size.  If you're still doing
 * conversions after that you'll just have to change this to a uint64_t. 
 */
typedef uint32_t	cvstime_t;
#define RCS_EPOCH	378691200	/* 1982-01-01T00:00:00 */

/*
 * This type must be wide enough to enumerte every CVS revision.
 */
typedef uint32_t	serial_t;

/*
 * Structures built by master file parsing begin.
 */

typedef struct _cvs_number {
    /* digested form of a CVS revision */
    short		c;
    short		n[CVS_MAX_DEPTH];
} cvs_number;

struct _cvs_version;
struct _cvs_patch;
struct _rev_file;

typedef struct node {
    struct node *hash_next;
    struct _cvs_version *version;
    struct _cvs_patch *patch;
    struct _rev_file *file;
    struct node *next;
    struct node *to;
    struct node *down;
    struct node *sib;
    cvs_number number;
    flag starts;
} Node;

typedef struct _cvs_symbol {
    /* a CVS symbol-to-revision association */
    struct _cvs_symbol	*next;
    char		*name;
    cvs_number		number;
} cvs_symbol;

typedef struct _cvs_branch {
    /* a CVS branch name */
    struct _cvs_branch	*next;
    cvs_number		number;
    Node		*node;
} cvs_branch;

typedef struct _cvs_version {
    /* metadata of a delta within a CVS file */
    struct _cvs_version	*next;
    cvs_number		number;
    char		*author;
    char		*state;
    cvs_branch		*branches;
    char		*commitid;
    Node		*node;
    cvs_number		parent;	/* next in ,v file */
    cvstime_t		date;
    flag		dead;
} cvs_version;

typedef struct _cvs_patch {
    /* a CVS patch structure */
    struct _cvs_patch	*next;
    cvs_number		number;
    char		*log;
    char		*text;
    Node		*node;
} cvs_patch;

typedef struct {
    /* this represents the entire metadata content of a CVS master file */
    char		*name;
    cvs_symbol		*symbols;
    cvs_version		*versions;
    cvs_patch		*patches;
    char 		*expand;
    char		*description;
    cvs_number		head;
    cvs_number		branch;
    mode_t		mode;
    serial_t		nversions;
} cvs_file;

typedef struct _rev_file {
    /* a CVS file revision state (composed from delta in a master) */
    char		*name;
    cvs_number		number;
    union {
	cvstime_t	date;
	struct _rev_file *other;
    } u;
    serial_t            serial;
    mode_t		mode;
    struct _rev_file	*link;
} rev_file;

typedef struct _rev_dir {
    /* a directory containing a collection of file states */
    serial_t		nfiles;
    rev_file		*files[0];
} rev_dir;

/*
 * Structures built by master file parsing end.
 */

extern int commit_time_window;

extern bool force_dates;

extern int load_current_file, load_total_files;

extern FILE *revision_map;

extern bool reposurgeon;

extern bool suppress_keyword_expansion;

extern char *branch_prefix;

extern bool progress;
#define STATUS stderr
#define NO_MAX	-1

extern bool branchorder;

extern time_t start_time;

/*
 * Tricky polymorphism hack to reduce working set size for large repos
 * begins here.
 *
 * In Keith's original code, cvs_commit and git_commit were the same struct,
 * with different semantics at different points in the program's lifetime.
 * Early, the struct described a CVS revision of one file.  Later, it
 * described a gitspace commit potentially pointing at several files.  The
 * first semantics held for instances created while scanning CVS master files;
 * the second for those generated by a git_commit_build() call during 
 * global branch merging.
 *
 * The problem with this is that some members in the late interpretation 
 * are unused in the earlier one; we want to get rid of as many as possible to 
 * reduce working-set size.  So we exploit the fact that all instances are
 * malloced by splitting the struct into two types. 
 *
 * Because the rev_ref structure contains a pointer to the early
 * version, a few casts are needed at points in the code that erquire
 * that late-version fields.  (This is mainly in export.c and
 * graph.c).  All these casts are marked with PUNNING in the code.
 * 
 * If the common fields in these structures don't remain in the same order,
 * bad things will happen.
 */

typedef struct _cvs_commit {
    /* a CVS revision */
    struct _cvs_commit	*parent;
    char		*log;
    char		*author;
    char		*commitid;
    rev_file		*file;		/* first file */
    cvstime_t		date;
    serial_t            serial;
    serial_t		seen;
    unsigned		tail:1;
    unsigned		tailed:1;
    unsigned		tagged:1;
    unsigned		dead:1;
} cvs_commit;

typedef struct _git_commit {
    /* a gitspace changeset */
    struct _git_commit	*parent;
    char		*log;
    char		*author;
    char		*commitid;
    rev_file		*file;		/* first file */
    cvstime_t		date;
    serial_t            serial;
    serial_t		seen;
    unsigned		tail:1;
    unsigned		tailed:1;
    unsigned		tagged:1;
    unsigned		dead:1;		/* not actually used in gitspace */
    /* gitspace-only members begin here */
    short		nfiles;
    short		ndirs;
    rev_dir		*dirs[0];
} git_commit;

typedef struct _rev_ref {
    /* CVS context for changesets before coalescence */
    struct _rev_ref	*next;
    cvs_commit		*commit;
    struct _rev_ref	*parent;	/* link into tree */
    char		*name;
    cvs_number		number;
    unsigned		depth:7;	/* depth in branching tree (1 is trunk) */
    unsigned		degree:7;	/* number of digits in original CVS version */
    flag		shown:1;
    flag		tail:1;
} rev_ref;

typedef struct _rev_list {
    struct _rev_list	*next;
    rev_ref	*heads;
} rev_list;

typedef struct _rev_file_list {
    struct _rev_file_list   *next;
    rev_file		    *file;
} rev_file_list;

typedef struct _rev_diff {
    rev_file_list	*del;
    rev_file_list	*add;
    int			ndel;
    int			nadd;
} rev_diff;

typedef struct _cvs_author {
    struct _cvs_author	*next;
    char		*name;
    char		*full;
    char		*email;
    char		*timezone;
} cvs_author;

cvs_author * fullname (char *);

bool load_author_map (char *);

extern cvs_file     *this_file;

int yyparse (void);

extern char *yyfilename;

char *
ctime_nonl (cvstime_t *date);

cvs_number
lex_number (char *);

cvstime_t
lex_date (cvs_number *n);

char *
lex_text (void);

rev_list *
rev_list_cvs (cvs_file *cvs);

rev_list *
rev_list_merge (rev_list *lists);

void
rev_list_free (rev_list *rl, int free_files);

enum { Ncommits = 256 };

typedef struct _chunk {
	struct _chunk *next;
	cvs_commit *v[Ncommits];
} Chunk;

typedef struct _tag {
	struct _tag *next;
	struct _tag *hash_next;
	char *name;
	Chunk *commits;
	int count;
	int left;
	git_commit *commit;
	rev_ref *parent;
	char *last;
} Tag;

extern Tag *all_tags;
void tag_commit(cvs_commit *c, char *name);
cvs_commit **tagged(Tag *tag);
void discard_tags(void);

bool
cvs_is_head (cvs_number *n);

bool
cvs_same_branch (cvs_number *a, cvs_number *b);

int
cvs_number_compare (cvs_number *a, cvs_number *b);

int
cvs_number_compare_n (cvs_number *a, cvs_number *b, int l);

bool
cvs_is_branch_of (cvs_number *trunk, cvs_number *branch);

int
cvs_number_degree (cvs_number *a);

cvs_number
cvs_previous_rev (cvs_number *n);

cvs_number
cvs_master_rev (cvs_number *n);

cvs_number
cvs_branch_head (cvs_file *f, cvs_number *branch);

cvs_number
cvs_branch_parent (cvs_file *f, cvs_number *branch);

Node *
cvs_find_version (cvs_file *cvs, cvs_number *number);

bool
cvs_is_trunk (cvs_number *number);

bool
cvs_is_vendor (cvs_number *number);

void
cvs_file_free (cvs_file *cvs);

char *
cvs_number_string (cvs_number *n, char *str);

long
time_compare (cvstime_t a, cvstime_t b);

void
dump_ref_name (FILE *f, rev_ref *ref);

char *
stringify_revision (char *name, char *sep, cvs_number *number);

void
dump_number_file (FILE *f, char *name, cvs_number *number);

void
dump_number (char *name, cvs_number *number);

void
dump_log (FILE *f, char *log);

void
dump_git_commit (git_commit *e);

void
dump_rev_head (rev_ref *h);

void
dump_rev_list (rev_list *rl);

void
dump_splits (rev_list *rl);

void
dump_rev_graph (rev_list *rl, char *title);

void
dump_rev_tree (rev_list *rl);

extern int yylex (void);

char *
atom (char *string);

void
discard_atoms (void);

rev_ref *
rev_list_add_head (rev_list *rl, cvs_commit *commit, char *name, int degree);

bool
git_commit_has_file (git_commit *c, rev_file *f);

rev_diff *
git_commit_diff (git_commit *old, git_commit *new);

bool
rev_file_list_has_filename (rev_file_list *fl, char *name);

void
rev_diff_free (rev_diff *d);

rev_ref *
rev_branch_of_commit (rev_list *rl, cvs_commit *commit);

rev_file *
rev_file_rev (char *name, cvs_number *n, cvstime_t date);

void
rev_file_free (rev_file *f);

void
rev_list_set_tail (rev_list *rl);

bool
rev_file_later (rev_file *a, rev_file *b);

void
rev_list_validate (rev_list *rl);

#define time_compare(a,b) ((long) (a) - (long) (b))

void 
export_blob(Node *node, void *buf, size_t len);

void
export_init(void);

bool
export_commits (rev_list *rl, int strip, time_t fromtime, bool progress);

void
export_wrap(void);

void
free_author_map (void);

void generate_files(cvs_file *cvs, void (*hook)(Node *node, void *buf, size_t len));

rev_dir **
rev_pack_files (rev_file **files, int nfiles, int *ndr);

void
rev_free_dirs (void);
    
void
git_commit_cleanup (void);

void 
load_status (char *name);

void 
load_status_next (void);

void* 
xmalloc(size_t size, char const *legend);

void*
xcalloc(size_t, size_t, char const *legend);

void* 
xrealloc(void *ptr, size_t size, char const *legend);

void
announce(char const *format,...) _printflike(1, 2);

void
fatal_error(char const *format, ...) _printflike(1, 2);

void
fatal_system_error(char const *format, ...) _printflike(1, 2);

void hash_version(cvs_version *);
void hash_patch(cvs_patch *);
void hash_branch(cvs_branch *);
void clean_hash(void);
void build_branches(void);
extern Node *head_node;

extern cvstime_t skew_vulnerable;
extern unsigned int total_revisions;

void progress_begin(char * /*msg*/, int /*max*/);
void progress_step(void);
void progress_jump(int /*count*/);
void progress_end(const char * /*format*/, ...) _printflike(1, 2);

#endif /* _CVS_H_ */

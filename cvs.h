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
#include <limits.h>
#include "cvstypes.h"
/* 
 * CVS_MAX_BRANCHWIDTH should match the number in the longrev test.
 * If it goes above 128 some bitfield widths in rev_ref must increase.
 */
#define CVS_MAX_DIGITS		10	/* max digits in decimal numbers */
#define CVS_MAX_BRANCHWIDTH	10
#define CVS_MAX_DEPTH		(2*CVS_MAX_BRANCHWIDTH + 2)
#define CVS_MAX_REV_LEN		(CVS_MAX_DEPTH * (CVS_MAX_DIGITS + 1))


/*
 * Structures built by master file parsing begin.
 */

typedef struct _cvs_number {
    /* digested form of a CVS revision */
    short		c;
    short		n[CVS_MAX_DEPTH];
} cvs_number;

extern const cvs_number cvs_zero;

struct _cvs_version;
struct _cvs_patch;

typedef struct node {
    struct node *hash_next;
    struct _cvs_version *version;
    struct _cvs_patch *patch;
    struct _cvs_commit *commit;
    struct node *next;
    struct node *to;
    struct node *down;
    struct node *sib;
    const cvs_number *number;
    flag starts;
} node_t;

#define NODE_HASH_SIZE	97

typedef struct nodehash {
    node_t *table[NODE_HASH_SIZE];
    int nentries;
    node_t *head_node;
} nodehash_t;

typedef struct _cvs_symbol {
    /* a CVS symbol-to-revision association */
    struct _cvs_symbol	*next;
    const char		*symbol_name;
    const cvs_number	*number;
} cvs_symbol;

typedef struct _cvs_branch {
    /* a CVS branch name */
    struct _cvs_branch	*next;
    const cvs_number	*number;
    node_t		*node;
} cvs_branch;

typedef struct _cvs_version {
    /* metadata of a delta within a CVS file */
    struct _cvs_version	*next;
    const char		*restrict author;
    const char		*restrict state;
    const char		*restrict commitid;
    cvs_branch		*branches;
    node_t		*node;
    const cvs_number	*restrict number;
    cvstime_t		date;
    const cvs_number	*restrict parent; /* next in ,v file */
    flag		dead;
} cvs_version;

typedef struct _cvs_text {
    /* a reference to a @-encoded text fragment in an rcs file */
    const char 		*filename;
    size_t		length; /* includes terminating '@' */
    off_t		offset; /* position of initial '@' */
} cvs_text;

typedef struct _cvs_patch {
    /* a CVS patch structure */
    struct _cvs_patch	*next;
    const cvs_number	*number;
    const char		*log;
    cvs_text		text;
    node_t		*node;
} cvs_patch;


struct out_buffer_type {
    char *text, *ptr, *end_of_text;
    size_t size;
};

struct in_buffer_type {
    unsigned char *buffer;
    unsigned char *ptr;
    int read_count;
};

#ifdef LINESTATS
typedef struct _edit_line {
    unsigned char *ptr;
    size_t length;
    int has_stringdelim;
} editline_t;
#endif

enum expand_mode {EXPANDKKV,	/* default form, $<key>: <value>$ */
		  EXPANDKKVL,	/* like KKV but with locker's name inserted */
		  EXPANDKK,	/* keyword-only expansion, $<key>$ */
		  EXPANDKV,	/* value-only expansion, $<value>$ */
		  EXPANDKO,	/* old-value expansion */
		  EXPANDKB,	/* old-value with no EOL normalization */
		  EXPANDUNSPEC,	/* Not specified on command line */
		};

typedef struct _editbuffer {
    const char *Glog;
    int Gkvlen;
    char* Gkeyval;
    char const *Gfilename;
    char *Gabspath;
    cvs_version *Gversion;
    char Gversion_number[CVS_MAX_REV_LEN];
    struct out_buffer_type *Goutbuf;
    struct in_buffer_type in_buffer_store;
#ifdef LINESTATS
    int line_len; /* temporary used for insertline */
    int has_stringdelim;
#endif
    enum expand_mode Gexpand;
    /*
     * Gline contains pointers to the lines in the current edit buffer
     * It is a 0-origin array that represents Glinemax-Ggapsize lines.
     * Gline[0 .. Ggap-1] and Gline[Ggap+Ggapsize .. Glinemax-1] hold
     * pointers to lines.  Gline[Ggap .. Ggap+Ggapsize-1] contains garbage.
     * Any @s in lines are duplicated.
     * Lines are terminated by \n, or(for a last partial line only) by single @.
     */
    struct frame {
	node_t *next_branch;
	node_t *node;
	unsigned char *node_text;
#ifdef LINESTATS
	editline_t *line;
#else
	unsigned char **line;
#endif
	size_t gap, gapsize, linemax;
    } stack[CVS_MAX_DEPTH/2], *current;
#ifdef USE_MMAP
    /* A recently used list of mmapped files */
    struct text_map {
	const char *filename;
	unsigned char *base;
	size_t size;
    } text_map;
#endif /* USE_MMAP */
} editbuffer_t;

#define Gline(eb) eb->current->line
#define Ggap(eb) eb->current->gap
#define Ggapsize(eb) eb->current->gapsize
#define Glinemax(eb) eb->current->linemax
#define Gnode_text(eb) eb->current->node_text
#define Ginbuf(eb) (&eb->in_buffer_store)

typedef struct _generator {
    /* isolare parts of a CVS file context required for snapshot generation */
    const char		*master_name;
    enum expand_mode    expand;
    cvs_version		*versions;
    cvs_patch		*patches;
    nodehash_t		nodehash;
    editbuffer_t	editbuffer;
} generator_t;

typedef struct {
    /* this represents the entire metadata content of a CVS master file */
    const char		*export_name;
    cvs_symbol		*symbols;
#ifdef REDBLACK
    struct rbtree_node	*symbols_by_name;
#endif /* REDBLACK */
    const char		*description;
    generator_t		gen;
    const cvs_number	*head;
    const cvs_number	*branch;
    cvstime_t           skew_vulnerable;
    serial_t		nversions;
    mode_t		mode;
    unsigned short	verbose;
} cvs_file;

typedef struct _master_dir {
    /* directory reference for a master */
    const char          *name;
    const struct _master_dir *parent;
} master_dir;

typedef struct _rev_master {
    /* information shared by all revisions of a master */
    const char		*name;
    const char          *fileop_name;
    const master_dir    *dir;
    struct _cvs_commit  *commits;
    serial_t		ncommits;
    mode_t		mode;
} rev_master;


/*
 * Structures built by master file parsing end.
 */

/* revdir should be considered an opaque type outside of revdir.c, but
 * we want to take advantage of being able to pack the struct
 * in a parent struct. Represents a packed list of files.
 * manipulate using the interface defined in revdir.h
 */
#ifdef TREEPACK

typedef struct _rev_pack rev_pack;

typedef struct _revdir {
    const rev_pack *revpack;
} revdir;

#else

typedef struct _file_list file_list;

typedef struct _revdir {
    /* dirs is slightly misleading, as there may be subdirs in the same entry */
    unsigned short ndirs;
    file_list      **dirs;
} revdir;

#endif



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
 * version, a few casts are needed at points in the code that require
 * the late-version fields.  (This is mainly in export.c and
 * graph.c).  All these casts are marked with PUNNING in the code.
 * 
 * If the common fields in these structures don't remain in the same order,
 * bad things will happen.
 */

typedef struct _cvs_commit {
    /* a CVS revision */
    struct _cvs_commit	*parent;
    const char		*restrict log;
    const char		*restrict author;
    const char	        *restrict commitid;
    cvstime_t		date;
    serial_t            serial;
    branchcount_t	refcount;
    unsigned		tail:1;
    unsigned		tailed:1;
    unsigned		dead:1;
    /* CVS-only members begin here */
    bool                emitted:1;
    hash_t              hash;
    /* Shortcut to master->dir, more space but less dereferences
     * in the hottest inner loop in revdir
     */
    const master_dir    *dir;
    const rev_master    *master;
    struct _git_commit	*gitspace;
    const cvs_number	*number;
} cvs_commit;

typedef struct _git_commit {
    /* a gitspace changeset */
    struct _git_commit	*parent;
    const char		*restrict log;
    const char		*restrict author;
    const char		*restrict commitid;
    cvstime_t		date;
    serial_t            serial;
    branchcount_t	refcount;
    unsigned		tail:1;
    unsigned		tailed:1;
    unsigned		dead:1;
    /* gitspace-only members begin here. */
    revdir		revdir;
} git_commit;

typedef struct _rev_ref {
    /* a reference to a branch head */
    struct _rev_ref	*next;
    struct _rev_ref	*parent;	/* link into tree */
    cvs_commit		*commit;        /* or a git_commit in gitspace */
    const char		*ref_name;
    const cvs_number	*number;	/* not used in gitspace */
    unsigned		depth:7;	/* branch depth in tree (1 is trunk) */
    unsigned		degree:7;	/* # of digits in original CVS version */
    flag		shown:1;	/* only used in graph emission */
    flag		tail:1;
} rev_ref;

/* Type punning, previously cvs_master and git_repo were rev_lists underneath
 * now cvs_master is backed by an array. We still have some general 
 * functions that just operate on  the heads member, so this lets
 * us use them from both git and cvs sides.
 */
typedef struct _head_list {
    rev_ref *heads;
} head_list;

typedef struct _rev_list {
    rev_ref	*heads;
    struct _rev_list	*next;
} rev_list;

/*
 * These are created only as an attempt to make the code more readable.
 * The problem they address is that a rev_list pointer can have 
 * different semantics depending on whether code is going to iterate
 * through its next pointer or its heads, and what stage of the program
 * we're in.  This makes functions with undifferentiated rev_list arguments
 * hard to read.  The convention we use is that a rev_list variable, member
 * or formal argument can accept any of these, but we try to be more 
 * specific in order to express the domain of a function.
 */
typedef head_list cvs_master;   /* represents a single cvs master */
                                /* a CVS repo is represented by an array of such */
typedef rev_list git_repo;	/* represents a gitspace DAG */

typedef struct _cvs_commit_list {
    struct _cvs_commit_list   *next;
    cvs_commit		    *file;
} cvs_commit_list;

typedef struct _rev_diff {
    cvs_commit_list	*restrict del;
    cvs_commit_list	*restrict add;
    int			ndel;
    int			nadd;
} rev_diff;

typedef struct _cvs_author {
    struct _cvs_author	*next;
    const char		*name;
    const char		*full;
    const char		*email;
    const char		*timezone;
} cvs_author;

/*
 * Use _printflike(M, N) to mark functions that take printf-like formats
 * and parameter lists.  M refers to the format arg, and N refers to
 * the first variable arg (typically M+1), or N = 0 if the function takes
 * a va_list.
 *
 * If the compiler is GCC version 2.7 or later, this is implemented
 * using __attribute__((__format__(...))).
 *
 * OS X lacks __alloc_size__,
 */
#if defined(__GNUC__) \
    && (__GNUC__ > 2) || (__GNUC__ == 2 && __GNUC_MINOR__ >= 7) \
    && !defined(__APPLE__)
#define _printflike(fmtarg, firstvararg)       \
            __attribute__((__format__(__printf__, fmtarg, firstvararg)))
#define _alloclike(sizearg) 		       \
            __attribute__((__alloc_size__(sizearg)))
#define _alloclike2(sizearg1, sizearg2)        \
            __attribute__((__alloc_size__(sizearg1, sizearg2)))
#define _malloclike                            \
            __attribute__((__malloc__))
#define _noreturn                              \
            __attribute__((__noreturn__))
#define _pure                                  \
            __attribute__((__noreturn__))
#define _alignof(T)  __alignof__(T)
#else
#define _printflike(fmtarg, firstvararg)       /* nothing */
#define _alloclike(sizearg)                    /* nothing */
#define _malloclike                            /* nothing */
#define _noreturn                              /* nothing */
#define _pure                                  /* nothing */
#define _alignof(T)  sizeof(long double)
#endif

#if defined(__APPLE__)
/* we mock this in utils.c; the CLOCK_REALTIME value is not used */
typedef int clockid_t;
int clock_gettime(clockid_t clock_id, struct timespec *tp);
#define CLOCK_REALTIME	0
#endif

cvs_author *fullname(const char *);

bool load_author_map(const char *);

char *
cvstime2rfc3339(const cvstime_t date);

cvs_number
lex_number(const char *);

cvstime_t
lex_date(const cvs_number *n, void *, cvs_file *cvs);

void
cvs_master_digest(cvs_file *cvs, cvs_master *cm, rev_master *master);

git_repo *
merge_to_changesets(cvs_master *masters, size_t nmasters, int verbose);

enum { Ncommits = 256 };

typedef struct _chunk {
	struct _chunk *next;
	cvs_commit *v[Ncommits];
} chunk_t;

typedef struct _tag {
	struct _tag *next;
	struct _tag *hash_next;
	const char *name;
	chunk_t *commits;
	int count;
	int left;
	git_commit *commit;
	rev_ref *parent;
	const char *last;
} tag_t;

typedef struct _forest {
    int filecount;
    off_t textsize;
    int errcount;
    cvs_master *cvs;
    git_repo *git;
    generator_t *generators;
    cvstime_t skew_vulnerable;
    unsigned int total_revisions;
} forest_t;

extern tag_t  *all_tags;
extern size_t tag_count;
extern const master_dir *root_dir;

void tag_commit(cvs_commit *c, const char *name, cvs_file *cvsfile);
cvs_commit **tagged(tag_t *tag);
void discard_tags(void);

typedef struct _import_options {
    bool promiscuous;
    int verbose;
    ssize_t striplen;
} import_options_t;

typedef struct _export_options {
    struct timespec start_time;
    enum expand_mode id_token_expand;
    char *branch_prefix; 
    time_t fromtime;
    FILE *revision_map;
    bool reposurgeon;
    bool embed_ids;
    bool force_dates;
    enum {adaptive, fast, canonical} reportmode;
    bool authorlist;
    bool progress;
} export_options_t;

typedef struct _export_stats {
    long	export_total_commits;
    double	snapsize;
} export_stats_t;

void
analyze_masters(int argc, char *argv[0], import_options_t *options, forest_t *forest);

enum expand_mode expand_override(char const *s);

bool
cvs_is_head(const cvs_number *n);

bool
cvs_same_branch(const cvs_number *a, const cvs_number *b);

bool
cvs_number_equal(const cvs_number *n1, const cvs_number *n2);

int
cvs_number_compare(const cvs_number *a, const cvs_number *b);

int
cvs_number_compare_n(const cvs_number *a, const cvs_number *b, int l);

bool
cvs_is_branch_of(const cvs_number *trunk, const cvs_number *branch);

int
cvs_number_degree(const cvs_number *a);

const cvs_number
cvs_previous_rev(const cvs_number *n);

const cvs_number
cvs_master_rev(const cvs_number *n);

const cvs_number
cvs_branch_head(cvs_file *f, const cvs_number *branch);

const cvs_number
cvs_branch_parent(cvs_file *f, const cvs_number *branch);

node_t *
cvs_find_version(const cvs_file *cvs, const cvs_number *number);

bool
cvs_is_trunk(const cvs_number *number);

bool
cvs_is_vendor(const cvs_number *number);

void
cvs_file_free(cvs_file *cvs);

void
generator_free(generator_t *gen);

char *
cvs_number_string(const cvs_number *n, char *str, size_t maxlen);

void
dump_ref_name(FILE *f, rev_ref *ref);

char *
stringify_revision(const char *name, const char *sep, const cvs_number *number,
		   char *buf, size_t bufsz);

void
dump_number_file(FILE *f, const char *name, const cvs_number *number);

void
dump_number(const char *name, const cvs_number *number);

void
dump_log(FILE *f, const char *log);

void
dump_git_commit(const git_commit *e, FILE *);

void
dump_rev_head(rev_ref *h, FILE *);

void
dump_rev_graph(git_repo *rl, const char *title);

const char *
atom(const char *string);

const cvs_number *
atom_cvs_number(const cvs_number n);

unsigned long
hash_cvs_number(const cvs_number *const key);

void
discard_atoms(void);

rev_ref *
rev_list_add_head(head_list *rl, cvs_commit *commit, const char *name, int degree);

rev_diff *
git_commit_diff(git_commit *old, git_commit *new);

void
rev_diff_free(rev_diff *d);

void
rev_list_set_tail(head_list *rl);

bool
rev_list_validate(head_list *rl);

int
path_deep_compare(const void *a, const void *b);

#define time_compare(a,b) ((long)(a) - (long)(b))

void
export_commits(forest_t *forest, export_options_t *opts, export_stats_t *stats);

void
export_authors(forest_t *forest, export_options_t *opts);

void
free_author_map(void);

void
generate_files(generator_t *gen, export_options_t *opts,
	       void (*hook)(node_t *node, void *buf, size_t len, export_options_t *popts));

/* xnew(T) allocates aligned (packed) storage. It never returns NULL */
#define xnew(T, legend) \
		xnewf(T, 0, legend)
/* xnewf(T,x) allocates storage with a flexible array member */
#define xnewf(T, extra, legend) \
		(T *)xmemalign(_alignof(T), sizeof(T) + (extra), legend)
/* xnewa(T,n) allocates storage for an array of types */
#define xnewa(T, n, legend) \
		(T *)xmemalign(_alignof(T), (n) * sizeof(T), legend)

#if _POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600
void* 
xmemalign(size_t align, size_t size, char const *legend) _alloclike(2) _malloclike;
#else
# define xmemalign(align, size, legend)  xmalloc(size, legend)
#endif

void* 
xmalloc(size_t size, char const *legend) _alloclike(1) _malloclike;

void*
xcalloc(size_t, size_t, char const *legend) _alloclike2(1,2) _malloclike;

void* 
xrealloc(void *ptr, size_t size, char const *legend) _alloclike(2);

void
announce(char const *format,...) _printflike(1, 2);

void
warn(char const *format,...) _printflike(1, 2);

void
debugmsg(char const *format,...) _printflike(1, 2);

void
fatal_error(char const *format, ...) _printflike(1, 2) _noreturn;

void
fatal_system_error(char const *format, ...) _printflike(1, 2) _noreturn;

void hash_version(nodehash_t *, cvs_version *);
void hash_patch(nodehash_t *, cvs_patch *);
void hash_branch(nodehash_t *, cvs_branch *);
void clean_hash(nodehash_t *);
void build_branches(nodehash_t *);

void progress_begin(const char * /*msg*/, const int /*max*/);
void progress_step(void);
void progress_jump(const int /*count*/);
void progress_end(const char * /*format*/, ...) _printflike(1, 2);

#define NANOSCALE		1000000000.0
#define nanosec(ts)		((ts)->tv_nsec + NANOSCALE * (ts)->tv_sec) 
#define seconds_diff(a, b)	((nanosec(a) - nanosec(b)) / NANOSCALE)

/* Work around glitches in Bison and Flex */

/* FIXME: remove once the Bison bug requiring this is fixed */
#define YY_DECL int yylex \
	(YYSTYPE * yylval_param , yyscan_t yyscanner, cvs_file *cvs)

/*
 * Statistics gathering.
 */
extern unsigned int warncount;
extern unsigned int natoms;

/*
 * Global options
 */

extern int commit_time_window;
extern FILE *LOGFILE;

extern bool progress;
#define STATUS stderr
#define NO_MAX	-1

#ifdef THREADS
extern int threads;
#endif /* THREADS */

#endif /* _CVS_H_ */

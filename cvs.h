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

/* 
 * CVS_MAX_BRANCHWIDTH should match the number in the longrev test.
 * If it goes above 128 some bitfield widths in rev_ref must increase.
 */
#define CVS_MAX_DIGITS		10	/* max digits in decimal numbers */
#define CVS_MAX_BRANCHWIDTH	10
#define CVS_MAX_DEPTH		(2*CVS_MAX_BRANCHWIDTH + 2)
#define CVS_MAX_REV_LEN		(CVS_MAX_DEPTH * (CVS_MAX_DIGITS + 1))

/*
 * Typedefs following this (everything before rbtree_color) have been
 * carefully chosen to minimize working set.
 */

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
 * Yes, the code *does* sanity-check for input dates older than this epoch.
 */
typedef uint32_t	cvstime_t;
#define RCS_EPOCH	378691200	/* 1982-01-01T00:00:00 */
#define RCS_OMEGA	UINT32_MAX	/* 2118-02-07T06:28:15 */

/*
 * This type must be wide enough to enumerate every CVS revision.
 * There's a sanity check in the code.
 */
typedef uint32_t	serial_t;
#define MAX_SERIAL_T	UINT32_MAX

/*
 * This type must be wide enough to count all branches cointaining a commit.
 * There's a sanity check in the code.
 */
typedef uint8_t			branchcount_t;
#define MAX_BRANCHCOUNT_T	UINT8_MAX

/*
 * Structures for the red/black tree
 */

typedef enum _rbtree_color {
  RED = 0,
  BLACK = 1,
} rbtree_color;

typedef struct _rbtree_node {
    const void		*key;
    void		*value;
    struct _rbtree_node	*parent;
    struct _rbtree_node	*left;
    struct _rbtree_node	*right;
    rbtree_color	color;
} rbtree_node;

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
} node_t;

#define NODE_HASH_SIZE	4096

typedef struct nodehash {
    node_t *table[NODE_HASH_SIZE];
    int nentries;
    node_t *head_node;
} nodehash_t;

typedef struct _cvs_symbol {
    /* a CVS symbol-to-revision association */
    struct _cvs_symbol	*next;
    const char		*symbol_name;
    cvs_number		number;
} cvs_symbol;

typedef struct _cvs_branch {
    /* a CVS branch name */
    struct _cvs_branch	*next;
    cvs_number		number;
    node_t		*node;
} cvs_branch;

typedef struct _cvs_version {
    /* metadata of a delta within a CVS file */
    struct _cvs_version	*next;
    const char		*author;
    const char		*state;
    cvs_branch		*branches;
    const char		*commitid;
    node_t		*node;
    cvs_number		number;
    cvstime_t		date;
    cvs_number		parent;	/* next in ,v file */
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
    cvs_number		number;
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

enum expand_mode {EXPANDKKV,	/* default form, $<key>: <value>$ */
		  EXPANDKKVL,	/* like KKV but with locker's name inserted */
		  EXPANDKK,	/* keyword-only expansion, $<key>$ */
		  EXPANDKV,	/* value-only expansion, $<value>$ */
		  EXPANDKO,	/* old-value expansion */
		  EXPANDKB,	/* old-value with no EOL normalization */
		};

#define NMAPS 4

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
    enum expand_mode Gexpand;
    /*
     * Gline contains pointers to the lines in the currently edit buffer
     * It is a 0-origin array that represents Glinemax-Ggapsize lines.
     * Gline[0 .. Ggap-1] and Gline[Ggap+Ggapsize .. Glinemax-1] hold
     * pointers to lines.  Gline[Ggap .. Ggap+Ggapsize-1] contains garbage.
     * Any @s in lines are duplicated.
     * Lines are terminated by \n, or(for a last partial line only) by single @.
     */
    int depth;
    struct {
	node_t *next_branch;
	node_t *node;
	unsigned char *node_text;
	unsigned char **line;
	size_t gap, gapsize, linemax;
    } stack[CVS_MAX_DEPTH/2];
#ifdef USE_MMAP
    /* A recently used list of mmapped files */
    struct text_map {
	const char *filename;
	unsigned char *base;
	size_t size;
    } text_maps[NMAPS];
#endif /* USE_MMAP */
} editbuffer_t;

#define Gline(eb) eb->stack[eb->depth].line
#define Ggap(eb) eb->stack[eb->depth].gap
#define Ggapsize(eb) eb->stack[eb->depth].gapsize
#define Glinemax(eb) eb->stack[eb->depth].linemax
#define Gnode_text(eb) eb->stack[eb->depth].node_text
#define Ginbuf(eb) (&eb->in_buffer_store)

typedef struct {
    /* this represents the entire metadata content of a CVS master file */
    const char		*master_name;
    cvs_symbol		*symbols;
    rbtree_node		*symbols_by_name;
    cvs_version		*versions;
    cvs_patch		*patches;
    const char 		*expand;
    char		*description;
    cvs_number		head;
    cvs_number		branch;
    mode_t		mode;
    serial_t		nversions;
    nodehash_t		nodehash;
    editbuffer_t	editbuffer;
} cvs_file;

typedef struct _rev_file {
    /* a CVS file revision state (composed from delta in a master) */
    struct _rev_file	*link;
    const char		*file_name;
    cvs_number		number;
    union {
	cvstime_t	date;
	struct _rev_file *other;
    } u;
    serial_t            serial;
    mode_t		mode;
} rev_file;

typedef struct _rev_dir {
    /* a directory containing a collection of file states */
    serial_t		nfiles;
    rev_file		*files[0];
} rev_dir;

/*
 * Structures built by master file parsing end.
 */

/*
 * A Bloom filter is a technique for probabilistic set matching.
 * We use them here for testing sets of atoms.
 */
#define BLOOMSIZE 128		/* free param: size of the filter-bit vector */
typedef uint64_t bloomword;	/* integral type for bit-vector elements */

#define BLOOMWIDTH	(sizeof(bloomword) * CHAR_BIT)
#define BLOOMLENGTH	(BLOOMSIZE / BLOOMWIDTH)
typedef struct _bloom {
    bloomword el[BLOOMLENGTH];
} bloom_t;

#define BLOOM_OP(dst, src1, op, src2) do { \
    unsigned _i; \
    for (_i = 0; _i < BLOOMLENGTH; _i++) \
         (dst)->el[_i] = (src1)->el[_i] op (src2)->el[_i]; \
  } while (0)

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
    const char		*log;
    const char		*author;
    const char		*commitid;
    cvstime_t		date;
    serial_t            serial;
    branchcount_t	refcount;
    unsigned		tail:1;
    unsigned		tailed:1;
    unsigned		dead:1;
    /* CVS-only members begin here */
    rev_file		*file;		/* first file */
} cvs_commit;

typedef struct _git_commit {
    /* a gitspace changeset */
    struct _git_commit	*parent;
    const char		*log;
    const char		*author;
    const char		*commitid;
    cvstime_t		date;
    serial_t            serial;
    branchcount_t	refcount;
    unsigned		tail:1;
    unsigned		tailed:1;
    unsigned		dead:1;		/* not actually used in gitspace */
    /* gitspace-only members begin here */
    short		nfiles;
    short		ndirs;
    bloom_t		bloom;
    rev_dir		*dirs[0];
} git_commit;

typedef struct _rev_ref {
    /* a reference to a branch head */
    struct _rev_ref	*next;
    cvs_commit		*commit;
    struct _rev_ref	*parent;	/* link into tree */
    const char		*ref_name;
    cvs_number		number;
    unsigned		depth:7;	/* depth in branching tree (1 is trunk) */
    unsigned		degree:7;	/* number of digits in original CVS version */
    flag		shown:1;	/* only used in graph emission */
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
    const char		*name;
    char		*full;
    char		*email;
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
 */
#if defined(__GNUC__) \
    && (__GNUC__ > 2) || (__GNUC__ == 2 && __GNUC_MINOR__ >= 7)
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

cvs_author *fullname(const char *);

bool load_author_map(const char *);

char *
ctime_nonl(const cvstime_t *date);

cvs_number
lex_number(const char *);

cvstime_t
lex_date(cvs_number *n, void *, cvs_file *cvs);

rev_list *
rev_list_cvs(cvs_file *cvs);

rev_list *
rev_list_merge(rev_list *lists);

void
rev_list_free(rev_list *rl, const bool free_files);

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

typedef struct _stats {
    int filecount;
    off_t textsize;
    int errcount;
} stats_t;

extern tag_t *all_tags;
void tag_commit(cvs_commit *c, const char *name, cvs_file *cvsfile);
cvs_commit **tagged(tag_t *tag);
void discard_tags(void);

rev_list *
analyze_masters(int argc, char *argv[0], 
		const bool promiscuous,
		const bool enable_keyword_expansion,
		const bool generate,
		const bool verbose,
		stats_t *stats);

bool
cvs_is_head(const cvs_number *n);

bool
cvs_same_branch(const cvs_number *a, const cvs_number *b);

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

char *
cvs_number_string(const cvs_number *n, char *str, size_t maxlen);

long
time_compare(cvstime_t a, cvstime_t b);

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
dump_rev_list(rev_list *rl, FILE *);

void
dump_splits(rev_list *rl);

void
dump_rev_graph(rev_list *rl, const char *title);

void
dump_rev_tree(rev_list *rl, FILE *);

const char *
atom(const char *string);

const bloom_t *
atom_bloom(const char *atom);

void
discard_atoms(void);

rev_ref *
rev_list_add_head(rev_list *rl, cvs_commit *commit, const char *name, int degree);

bool
git_commit_has_file(const git_commit *c, const const rev_file *f);

rev_diff *
git_commit_diff(git_commit *old, git_commit *new);

bool
rev_file_list_has_filename(const rev_file_list *fl, const char *name);

void
rev_diff_free(rev_diff *d);

rev_ref *
rev_branch_of_commit(const rev_list *rl, const cvs_commit *commit);

rev_file *
rev_file_rev(const char *name, const cvs_number *n, const cvstime_t date);

void
rev_file_free(rev_file *f);

void
rev_list_set_tail(rev_list *rl);

bool
rev_file_later(rev_file *a, rev_file *b);

void
rev_list_validate(rev_list *rl);

int
path_deep_compare(const void *a, const void *b);

#define time_compare(a,b) ((long)(a) - (long)(b))

void 
export_blob(node_t *node, void *buf, size_t len);

void
export_init(void);

bool
export_commits(rev_list *rl, const char *branch_prefix, 
	       time_t fromtime, const char *revision_map,
	       bool reposurgeon, bool force_dates, bool branchorder,
	       bool progress);

void
export_wrap(void);

void
save_status_end(const struct timespec *);

void
free_author_map(void);

void generate_files(cvs_file *cvs, 
		    bool enable_keyword_expansion,
		    void (*hook)(node_t *node, void *buf, size_t len));

rev_dir **
rev_pack_files(rev_file **files, int nfiles, int *ndr);

void
rev_free_dirs(void);
    
void
git_commit_cleanup(void);

void
rbtree_insert(rbtree_node **root, const void *key, void *value,
              int (*compare)(const void* key1, const void* key2));

rbtree_node*
rbtree_lookup(rbtree_node *root, const void* key,
              int (*compare)(const void* key1, const void* key2));

void
rbtree_free(rbtree_node *root);

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
fatal_error(char const *format, ...) _printflike(1, 2) _noreturn;

void
fatal_system_error(char const *format, ...) _printflike(1, 2) _noreturn;

void hash_version(nodehash_t *, cvs_version *);
void hash_patch(nodehash_t *, cvs_patch *);
void hash_branch(nodehash_t *, cvs_branch *);
void clean_hash(nodehash_t *);
void build_branches(nodehash_t *);

extern cvstime_t skew_vulnerable;
extern unsigned int total_revisions;

void progress_begin(const char * /*msg*/, const int /*max*/);
void progress_step(void);
void progress_jump(const int /*count*/);
void progress_end(const char * /*format*/, ...) _printflike(1, 2);

#define NANOSCALE		1000000000.0
#define nanosec(ts)		((ts)->tv_nsec + NANOSCALE * (ts)->tv_sec) 
#define seconds_diff(a, b)	((nanosec(a) - nanosec(b)) / NANOSCALE)

/* Work around glitches in Bison and Flex */

typedef void *yyscan_t;

int yyget_column (yyscan_t);
void yyset_column(int, yyscan_t);

/* FIXME: remove once the Bison bug requiring this is fixed */
#define YY_DECL int yylex \
	(YYSTYPE * yylval_param , yyscan_t yyscanner, cvs_file *cvs)

/*
 * Global options
 */

extern int commit_time_window;

extern bool progress;
#define STATUS stderr
#define NO_MAX	-1

extern ssize_t striplen;

#ifdef THREADS
int threads;
#endif /* THREADS */

#endif /* _CVS_H_ */

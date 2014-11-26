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

/*
 * Build one in-core linked list corresponding to a single CVS
 * master.  Just one entry point, cvs_master_digest(), which takes the
 * structure built by the grammar parse of the master as its single
 * argument.
 */

#include "cvs.h"

#ifdef REDBLACK
#include "rbtree.h"
#endif /* REDBLACK */ 

#ifdef THREADS
#include <pthread.h>
#endif

static const char *fileop_name(const char *rectified)
{
    size_t rlen = strlen(rectified);

    if (rlen >= 10 && strcmp(rectified + rlen - 10, ".cvsignore") == 0) {
        char path[PATH_MAX];
        strncpy(path, rectified, PATH_MAX-1);
        path[rlen - 9] = 'g';
        path[rlen - 8] = 'i';
        path[rlen - 7] = 't';
        return atom(path);
    }
    // assume rectified is already an atom
    return rectified;
}

#define DIR_BUCKETS 24593

typedef struct _dir_bucket {
    struct _dir_bucket *next;
    master_dir         dir;
} dir_bucket;

static dir_bucket *dir_buckets[DIR_BUCKETS];
#ifdef THREADS
static pthread_mutex_t dir_bucket_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif /* THREADS */

static const master_dir *
atom_dir(const char* name)
/* Extract information about the directory a master is in .
 * atomize the result so all references to the same directory
 * point the the same value.
 */
{
    char       *slash = strrchr(name, '/');
    const char *dirname;
    short      dirlen;
    char       buf[PATH_MAX];
    if (slash) {
	strncpy(buf, name, slash - name);
	buf[slash - name + 1] = '\0';
	dirname = atom(buf);
	dirlen = slash - name;
    } else {
	dirname = atom("\0");
	dirlen = 0;
    }
    uintptr_t  hash = FNV_OFF * FNV_MIX ^ (uintptr_t)dirname;
    dir_bucket **head = &dir_buckets[hash % DIR_BUCKETS];
    dir_bucket *b;

    while ((b = *head)) {
    collision:
	if (b->dir.name == dirname)
	    return &(b->dir);
	head = &(b->next);
    }
#ifdef THREADS
    if (threads > 1)
	pthread_mutex_lock(&dir_bucket_mutex);
#endif /* THREADS */
    if ((b = *head)) {
#ifdef THREADS
	if (threads > 1)
	    pthread_mutex_unlock(&dir_bucket_mutex);
#endif /* THREADS */
	goto collision;
    }
    b = xmalloc(sizeof(dir_bucket), __func__);
    b->next = NULL;
    b->dir.name = dirname;
    b->dir.len = dirlen;
    /* used as an input to the hash of the dir_is_ancestor fn */
    b->dir.prehash = FNV_OFF * FNV_MIX ^ (uintptr_t)b;
    *head = b;
#ifdef THREADS
    if (threads > 1)
	pthread_mutex_unlock(&dir_bucket_mutex);
#endif /* THREADS */
    return &(b->dir);
}

static cvs_commit *
cvs_master_find_revision(cvs_master *cm, const cvs_number *number)
/* given a single-file revlist tree, locate the specific version number */
{
    rev_ref	*h;
    cvs_commit	*c;

    for (h = cm->heads; h; h = h->next) {
	if (h->tail)
	    continue;
	for (c = h->commit; c; c = c->parent)
	{
	     if (cvs_number_compare(c->number, number) == 0)
		    return c;
	     if (c->tail)
		 break;
	}
    }
    return NULL;
}

static cvs_commit *
cvs_master_branch_build(cvs_file *cvs, const cvs_number *branch)
/* build a list of commit objects representing a branch from deltas on it */
{
    cvs_number	n;
    const cvs_number *atom_n;
    cvs_commit	*head = NULL;
    cvs_commit	*c, *p, *gc;
    node_t	*node;
    rev_master  *master = xcalloc(1,sizeof(rev_master), "master construction");
    char buf[BUFSIZ];

    debugmsg("\tstarting new branch, branch number = %s\n",
	     cvs_number_string(branch, buf, BUFSIZ));

    master->name = cvs->export_name;
    master->fileop_name = fileop_name(cvs->export_name);
    master->dir = atom_dir(master->name);
    master->mode = cvs->mode;
    master->commits = xcalloc(cvs->nversions, sizeof(cvs_commit),
			      "commit slab alloc");

    memcpy(&n, branch, sizeof(cvs_number));
    n.n[n.c-1] = -1;
    atom_n = atom_cvs_number(n);
    for (node = cvs_find_version(cvs, atom_n); node; node = node->next) {
	cvs_version *v = node->version;
	cvs_patch *p = node->patch;
	cvs_commit *c;
	if (!v)
	     continue;
	c = master->commits + master->ncommits++;
	c->date = v->date;
	c->commitid = v->commitid;
	c->author = v->author;
	c->tail = c->tailed = false;
	c->refcount = c->serial = 0;
	if (p)
	    c->log = p->log;
	 c->dead = v->dead;
	/* leave this around so the branch merging stuff can find numbers */
	c->master = master;
	c->number = v->number;
	if (!v->dead) {
	    node->commit = c;
	}
	c->parent = head;
	head = c;
    }

    if (head == NULL)
	/* coverity[leaked_storage] */
	return NULL;

    /*
     * Make sure the dates along the branch are well ordered. As we
     * want to preserve current data, push previous versions back to
     * align with newer revisions. (The branch is being traversed
     * in reverse order. p = parent, c = child, gc = grandchild.)
     */
    for (c = head, gc = NULL; (p = c->parent); gc = c, c = p) {
	if (time_compare(p->date, c->date) > 0)
	{
	    warn("warning - %s:", cvs->gen.master_name);
	    dump_number_file(LOGFILE, " ", p->number);
	    dump_number_file(LOGFILE, " is newer than", c->number);

	    /* Try to catch an odd one out, such as a commit with the
	     * clock set wrong.  Don't push back all commits for that,
	     * just fix up the current commit instead of the
	     * parent. */
	    if (gc && time_compare(p->date, gc->date) <= 0)
	    {
	      dump_number_file(LOGFILE, ", adjusting", c->number);
	      c->date = p->date;
	    } else {
	      dump_number_file(LOGFILE, ", adjusting", c->number);
	      p->date = c->date;
	    }
	    fprintf(LOGFILE, "\n");
	}
    }

    debugmsg("\tnew branch, head number = %s\n",
	     cvs_number_string(head->number, buf, BUFSIZ));

    /* coverity[leaked_storage] */
    return head;
}

/*
 * "Vendor branches" (1.1.x) are created by importing sources from
 * an external source. In X.org, this was from XFree86 and DRI. When
 * these trees are imported, cvs sets the 'default' branch in each ,v file
 * to point along this branch. This means that tags made between
 * the time the vendor branch is imported and when a new revision
 * is committed to the head branch are placed on the vendor branch
 * In addition, any files without such a commit appear to adopt
 * the vendor branch as 'head'. We fix this by merging these two
 * branches together as if they were the same
 */
static void
cvs_master_patch_vendor_branch(cvs_master *cm, cvs_file *cvs)
{
    rev_ref	*trunk = NULL;
    rev_ref	*vendor = NULL;
    rev_ref	*h;
    cvs_commit	*t, **tp, *v, **vp;
    cvs_commit	*vlast;
    rev_ref	**h_p;

    trunk = cm->heads;
    for (h_p = &cm->heads; (h = *h_p);) {
	bool delete_head = false;
	if (h->commit && cvs_is_vendor(h->commit->number))
	{
	    /*
	     * Find version 1.2 on the trunk.
	     * This will reset the default branch set
	     * when the initial import was done.
	     * Subsequent imports will *not* set the default
	     * branch, and should be on their own branch
	     */
	    vendor = h;
	    t = trunk->commit;
	    v = vendor->commit;
	    for (vlast = vendor->commit; vlast; vlast = vlast->parent)
		if (!vlast->parent)
		    break;
	    tp = &trunk->commit;
	    /*
	     * Find the latest trunk revision older than
	     * the entire vendor branch
	     */
	    while ((t = *tp))
	    {
		if (!t->parent || 
		    time_compare(vlast->date, t->parent->date) >= 0)
		{
		    break;
		}
		tp = &t->parent;
	    }
	    if (t)
	    {
		/*
		 * If the first commit is older than the last element
		 * of the vendor branch, paste them together and
		 * nuke the vendor branch
		 */
		if (time_compare(vlast->date, t->date) >= 0)
		{
		    delete_head = true;
		}
		else
		{
		    /*
		     * Splice out any portion of the vendor branch
		     * newer than a the next trunk commit after
		     * the oldest branch commit.
		     */
		    for (vp = &vendor->commit; (v = *vp); vp = &v->parent)
			if (time_compare(v->date, t->date) <= 0)
			    break;
		    if (vp == &vendor->commit)
		    {
			/*
			 * Nothing newer, nuke vendor branch
			 */
			delete_head = true;
		    }
		    else
		    {
			/*
			 * Some newer stuff, patch parent
			 */
			*vp = NULL;
		    }
		}
	    }
	    else
		delete_head = true;
	    /*
	     * Patch up the remaining vendor branch pieces
	     */
	    if (!delete_head) {
		cvs_commit  *vr;
		if (!vendor->ref_name) {
		    char	rev[CVS_MAX_REV_LEN];
		    char	name[PATH_MAX];
		    cvs_number	branch;

		    memcpy(&branch, vlast->number, sizeof(cvs_number));
		    branch.c--;
		    cvs_number_string(&branch, rev, sizeof(rev));
		    snprintf(name, sizeof(name),
			      "import-%s", rev);
		    vendor->ref_name = atom(name);
		    vendor->parent = trunk;
		    vendor->degree = vlast->number->c;
		}
		for (vr = vendor->commit; vr; vr = vr->parent)
		{
		    if (!vr->parent) {
			vr->tail = true;
			vr->parent = v;
			break;
		    }
		}
	    }
	    
	    /*
	     * Merge two branches based on dates
	     */
	    while (t && v)
	    {
		if (time_compare(v->date, t->date) >= 0)
		{
		    *tp = v;
		    tp = &v->parent;
		    v = v->parent;
		}
		else
		{
		    *tp = t;
		    tp = &t->parent;
		    t = t->parent;
		}
	    }
	    if (t)
		*tp = t;
	    else
		*tp = v;
	}
	if (delete_head) {
	    *h_p = h->next;
	    free(h);
	} else {
	    h_p = &(h->next);
	}
    }
#if CVSDEBUG
    debugmsg("%s spliced:\n", cvs->export_name);
    for (t = trunk->commit; t; t = t->parent) {
	dump_number_file(LOGFILE, "\t", t->number);
	debugmsg("\n");
    }
#endif /* CVSDEBUG */
}

static void
cvs_master_graft_branches(cvs_master *cm, cvs_file *cvs)
/* turn disconnected branches into a tree by grafting roots to parents */ 
{
    rev_ref	*h;
    cvs_commit	*c;
    cvs_version	*cv;
    cvs_branch	*cb;

    /*
     * Glue branches together
     */
    for (h = cm->heads; h; h = h->next) {
	/*
	 * skip master branch; it "can't" join
	 * any other branches and it may well end with a vendor
	 * branch revision of the file, which will then create
	 * a loop back to the recorded branch point
	 */
        if (h == cm->heads)
	    continue;
	if (h->tail)
	    continue;
	/*
	 * Find last commit on branch
	 */
	for (c = h->commit; c && c->parent; c = c->parent)
	    if (c->tail) {
		c = NULL;	/* already been done, skip */
		break;
	    }
	if (c) {
	    /*
	     * Walk the version tree, looking for the branch location.
	     * Note that in the presense of vendor branches, the
	     * branch location may actually be out on that vendor branch
	     */
	    for (cv = cvs->gen.versions; cv; cv = cv->next) {
		for (cb = cv->branches; cb; cb = cb->next) {
		    if (cvs_number_compare(cb->number,
					    c->number) == 0)
		    {
			c->parent = cvs_master_find_revision(cm, cv->number);
			c->tail = true;
			break;
		    }
		}
		if (c->parent)
		{
#if 0
		    /*
		     * check for a parallel vendor branch
		     */
		    for (cb = cv->branches; cb; cb = cb->next) {
			if (cvs_is_vendor(cb->number)) {
			    cvs_number	v_n;
			    cvs_commit	*v_c, *n_v_c;
			    warn("Found merge into vendor branch\n");
			    memcpy(&v_n, cb->number, sizeof(cvs_number));
			    v_c = NULL;
			    /*
			     * Walk to head of vendor branch
			     */
			    while ((n_v_c = cvs_master_find_revision(cm, atom_cve_number(v_n))))
			    {
				/*
				 * Stop if we reach a date after the
				 * branch version date
				 */
				if (time_compare(n_v_c->date, c->date) > 0)
				    break;
				v_c = n_v_c;
				v_n.n[v_n.c - 1]++;
			    }
			    if (v_c)
			    {
				warn("%s: rewrite branch", cvs->name);
				dump_number_file(LOGFILE, " branch point",
						  v_c->number);
				dump_number_file(LOGFILE, " branch version",
						  c->number);
				fprintf(LOGFILE, "\n");
				c->parent = v_c;
			    }
			}
		    }
#endif
		    break;
		}
	    }
	}
    }
}

static rev_ref *
cvs_master_find_branch(cvs_master *cm, const cvs_number *number)
/* look up a revision reference in a revlist by symbol */
{
    cvs_number	n;
    rev_ref	*h;

    if (number->c < 2)
	return NULL;
    memcpy(&n, number, sizeof(cvs_number));
    h = NULL;
    while (n.c >= 2) {
	const cvs_number *k = atom_cvs_number(n);
	for (h = cm->heads; h; h = h->next) {
	    if (cvs_same_branch(h->number, k)) {
		break;
	    }
	}
	if (h)
	    break;
	n.c -= 2;
    }
    return h;
}

static void
cvs_master_set_refs(cvs_master *cm, cvs_file *cvsfile)
/* create head references or tags for each symbol in the CVS master */
{
    rev_ref	*h;
    cvs_symbol	*s;
    
    for (s = cvsfile->symbols; s; s = s->next) {
	cvs_commit	*c = NULL;
	/*
	 * Locate a symbolic name for this head
	 */
	if (cvs_is_head(s->number)) {
	    for (h = cm->heads; h; h = h->next) {
		if (cvs_same_branch(h->commit->number, s->number))
		    break;
	    }
	    if (h) {
		if (!h->ref_name) {
		    h->ref_name = s->symbol_name;
		    h->degree = cvs_number_degree(s->number);
		} else
		    h = rev_list_add_head(cm, h->commit, s->symbol_name,
					   cvs_number_degree(s->number));
	    } else {
		cvs_number n;

		memcpy(&n, s->number, sizeof(cvs_number));
		while (n.c >= 4) {
		    n.c -= 2;
		    c = cvs_master_find_revision(cm, atom_cvs_number(n));
		    if (c)
			break;
		}
		if (c)
		    h = rev_list_add_head(cm, c, s->symbol_name,
					   cvs_number_degree(s->number));
	    }
	    if (h)
		h->number = s->number;
	} else {
	    c = cvs_master_find_revision(cm, s->number);
	    if (c)
		tag_commit(c, s->symbol_name, cvsfile);
	}
    }
    /*
     * Fix up unnamed heads
     */
    for (h = cm->heads; h; h = h->next) {
	cvs_number	n;
	cvs_commit	*c;

	if (h->ref_name)
	    continue;
	for (c = h->commit; c; c = c->parent) {
	    if (!c->dead)
		break;
	}
	if (!c)
	    continue;
	memcpy(&n, c->number, sizeof(cvs_number));
	/* convert to branch form */
	n.n[n.c-1] = n.n[n.c-2];
	n.n[n.c-2] = 0;
	h->number = atom_cvs_number(n);
	h->degree = cvs_number_degree(&n);
	/* compute name after patching parents */
    }
    /*
     * Link heads together in a tree
     */
    for (h = cm->heads; h; h = h->next) {
	cvs_number	n;
	/*
         * Can get unnumbered heads here
         * not sure what that means
         */
	if (!h->number) {
	    h->number = atom_cvs_number(cvs_zero);
	    if (h->ref_name)
		warn("unnumbered head %s in %s\n", h->ref_name, cvsfile->export_name);
	    else
		warn("unnumbered head in %s\n", cvsfile->export_name);
	}

	if (h->number->c >= 4) {
	    memcpy(&n, h->number, sizeof(cvs_number));
	    n.c -= 2;
	    h->parent = cvs_master_find_branch(cm, atom_cvs_number(n));
	    if (!h->parent && !cvs_is_vendor(h->number))
		warn("warning - non-vendor %s branch %s has no parent\n",
			 cvsfile->gen.master_name, h->ref_name);
	}
	if (h->parent && !h->ref_name) {
	    char	name[1024];
	    char	rev[CVS_MAX_REV_LEN];

	    cvs_number_string(h->number, rev, sizeof(rev));
	    if (h->commit->commitid)
		sprintf(name, "%s-UNNAMED-BRANCH-%s", h->parent->ref_name,
			h->commit->commitid);
	    else
		sprintf(name, "%s-UNNAMED-BRANCH", h->parent->ref_name);
	    warn("warning - putting %s rev %s on unnamed branch %s off %s\n",
		cvsfile->gen.master_name, rev, name, h->parent->ref_name);
	    h->ref_name = atom(name);
	}
    }
}

#ifdef REDBLACK
static int
cvs_symbol_name_compare(const void *x, const void *y)
/* compare function used for red-black tree lookup */
{
    if (x < y)
	return -1;
    else if (y < x)
	return 1;
    else
	return 0;
}
#endif /* REDBLACK */

static cvs_symbol *
cvs_find_symbol(cvs_file *cvs, const char *name)
/* return the CVS symbol corresponding to a specified name */
{
#ifdef REDBLACK
    struct rbtree_node *n, **tree;

    tree = &cvs->symbols_by_name;
    if (!(*tree)) {
	cvs_symbol *s;
	for (s = cvs->symbols; s ; s = s->next)
	    rbtree_insert(tree, s->symbol_name, s, cvs_symbol_name_compare);
    }

    n = rbtree_lookup(*tree, name, cvs_symbol_name_compare);
    if (n)
	return(cvs_symbol*)rbtree_value(n);
#else
    cvs_symbol *s;

    for (s = cvs->symbols; s; s = s->next)
	if (s->symbol_name == name)
	    return s;
#endif /*  REDBLACK */
    return NULL;
}

static int
rev_ref_compare(cvs_file *cvs, const rev_ref *r1, const rev_ref *r2)
/* comparison function used for topological sorting */
{
    cvs_symbol *s1, *s2;
    s1 = cvs_find_symbol(cvs, r1->ref_name);
    s2 = cvs_find_symbol(cvs, r2->ref_name);
    if (!s1) {
	if (!s2) return 0;
	return -1;
    }
    if (!s2)
	return 1;
    return cvs_number_compare(s1->number, s2->number);
}

static void
cvs_master_sort_heads(cvs_master *cm, cvs_file *cvs)
/* sort branch heads so parents are always before children; trunk first. */
{
    rev_ref *p = cm->heads, *q;
    rev_ref *e;
    rev_ref *l = NULL, *lastl = NULL;
    int k = 1;
    int i, psize, qsize;

    /*
     * Implemented from description at
     * http://www.chiark.greenend.org.uk/~sgtatham/algorithms/listsort.html
     */
    for (;;) {
	int passmerges = 0;

	passmerges = 0;

	while (p) {

	    passmerges++;

	    q = p;
	    qsize = k;
	    psize = 0;
	    for (i = 0; i < k; i++) {
		if (!q->next) break;
		psize++;
		q = q->next;
	    }

	    while (psize || (qsize && q)) {
		if (!psize) {
		    e = q;
		} else if (!(qsize && q)) {
		    e = p;
		} else if (rev_ref_compare(cvs, p, q) > 0) {
		    e = q;
		} else {
		    e = p;
		}

		/*
		 * If the element ever equals q, it is always safe to assume it
		 * will come from q. The same is not true for p as p == q when
		 * psize == 0
		 */
		if (e == q) {
		    e = q;
		    q = q->next;
		    qsize--;
		} else {
		    e = p;
		    p = p->next;
		    psize--;
		}

		/*
		 * Break the element out of its old list and append it to the
		 * new sorted list
		 */
		e->next = NULL;
		if (l) {
		    lastl->next = e;
		    lastl = e;
		} else {
		    l = lastl = e;
		}
	    }
	    p = q;
	}

	if (passmerges <= 1) break;

	p = l;
	l = lastl = NULL;
	k = 2*k;
    }

    cm->heads = l;
#if DEBUG
    fprintf(stderr, "Sorted heads for %s\n", cvs->name);
    for (e = cm->heads; e;) {
	fprintf(stderr, "\t");
	//cvs_master_dump_ref_parents(stderr, e->parent);
	dump_number_file(stderr, e->name, e->number);
	fprintf(stderr, "\n");
	e = e->next;
    }
#endif
}

cvs_repo *
cvs_master_digest(cvs_file *cvs)
/* return a linked list capturing the CVS master file structure */ 
{
    cvs_master	*cm = xcalloc(1, sizeof(cvs_master), "cvs_master_digest");
    const cvs_number *trunk_number;
    cvs_commit	*trunk; 
    cvs_commit	*branch;
    cvs_version	*cv;
    cvs_branch	*cb;
    cvs_version	*ctrunk = NULL;
    char buf[BUFSIZ];

    build_branches(&cvs->gen.nodehash);
    /*
     * Locate first revision on trunk branch
     */
    for (cv = cvs->gen.versions; cv; cv = cv->next) {
	if (cvs_is_trunk(cv->number) &&
	    (!ctrunk || cvs_number_compare(cv->number, ctrunk->number) < 0))
	{
	    ctrunk = cv;
	}
    }
    /*
     * Generate trunk branch
     */
    if (ctrunk)
	trunk_number = ctrunk->number;
    else
	trunk_number = atom_cvs_number(lex_number("1.1"));
    trunk = cvs_master_branch_build(cvs, trunk_number);
    if (trunk) {
	rev_ref	*t;
	t = rev_list_add_head(cm, trunk, atom("master"), 2);
	t->number = trunk_number;
#if CVSDEBUG
	if (cvs->verbose > 0)
	    debugmsg("Building trunk branch %s for %s:\n", 
		     cvs_number_string(t->number, buf, BUFSIZ),
		     cvs->gen.master_name);
#endif /* CVSDEBUG */
    }
    else
	warn("warning - no master branch generated\n");
#if CVSDEBUG
    /*
     * Search for other branches
     */
    if (cvs->verbose > 0)
	debugmsg("Building non-trunk branches for %s:\n", cvs->gen.master_name);
#endif /* CVSDEBUG */
    
    for (cv = cvs->gen.versions; cv; cv = cv->next) {
	for (cb = cv->branches; cb; cb = cb->next)
	{
	    branch = cvs_master_branch_build(cvs, cb->number);
	    if (cvs->verbose > 0)
	    {
	        char buf2[BUFSIZ];
	        char buf3[BUFSIZ];
		debugmsg("\t%s\t->\t%s\t->\t%s\n",
			 cvs_number_string(cv->number, buf, BUFSIZ),
			 cvs_number_string(cb->number, buf2, BUFSIZ),
			 cvs_number_string(branch->number, buf3, BUFSIZ));
	    }
	    rev_list_add_head(cm, branch, NULL, 0);
	}
    }
    cvs_master_patch_vendor_branch(cm, cvs);
    cvs_master_graft_branches(cm, cvs);
    cvs_master_set_refs(cm, cvs);
    cvs_master_sort_heads(cm, cvs);
    rev_list_set_tail(cm);

#if CVSDEBUG
    if (cvs->verbose > 0) {
	rev_ref	*lh;

	debugmsg("Named heads in %s:\n", cvs->gen.master_name);
	for (lh = cm->heads; lh; lh = lh->next) {
	    char buf[BUFSIZ];

	    debugmsg("\tname = %s\tnumber = %s\n", lh->ref_name,
		     cvs_number_string(lh->number, buf, BUFSIZ));
	}
    }
#endif /* CVSDEBUG */

    //rev_list_validate(cm);
    return cm;
}

// end

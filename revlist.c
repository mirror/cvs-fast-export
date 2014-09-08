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

/*
 * A revision list is the history for an entire RCS/CVS repository.
 * These functions analyze a revlist into a changeset DAG.
 */

rev_ref *
rev_list_add_head(rev_list *rl, cvs_commit *commit, char *name, int degree)
/* decorate a commit list with a named head reference */
{
    rev_ref	*r;
    rev_ref	**list = &rl->heads;

    while (*list)
	list = &(*list)->next;
    r = xcalloc(1, sizeof(rev_ref), "adding head reference");
    r->commit = commit;
    r->ref_name = name;
    r->next = *list;
    r->degree = degree;
    *list = r;
    return r;
}

static rev_ref *
rev_find_head(rev_list *rl, char *name)
/* find a named hesd in a revlist */
{
    rev_ref	*h;

    for (h = rl->heads; h; h = h->next)
	if (h->ref_name == name)
	    return h;
    return NULL;
}

static bool
commit_time_close(cvstime_t a, cvstime_t b)
/* are two timestamps within the commit-coalescence window? */
{
    long	diff = (long)a - (long)b;
    if (diff < 0) diff = -diff;
    if (diff < commit_time_window)
	return true;
    return false;
}

static bool
cvs_commit_match(cvs_commit *a, cvs_commit *b)
/* are two commits eligible to be coalesced into a changeset? */
{
    /*
     * Versions of GNU CVS after 1.12 (2004) place a commitid in
     * each commit to track patch sets. Use it if present
     */
    if (a->commitid && b->commitid)
	return a->commitid == b->commitid;
    if (a->commitid || b->commitid)
	return false;
    if (!commit_time_close(a->date, b->date))
	return false;
    if (a->log != b->log)
	return false;
    if (a->author != b->author)
	return false;
    return true;
}

void
rev_list_set_tail(rev_list *rl)
/* set tail bits so we can walk through each conmit in a revlist exactly ince */
{
    rev_ref	*head;
    cvs_commit	*c;

    /* 
     * Set tail bit true where traversal should stop in order to avoid
     * multiple visits to the same commit.
     */ 
    for (head = rl->heads; head; head = head->next) {
	flag tail = true;
	/* set tail on each previously visited head reference */
	if (head->commit && head->commit->refcount > 0) {
	    head->tail = tail;
	    tail = false;
	}
	for (c = head->commit; c; c = c->parent) {
	    /* set tail on the child of the first join commit on this branch */
	    if (tail && c->parent && c->refcount < c->parent->refcount) {
		c->tail = true;
		tail = false;
	    }
	    c->refcount++;
	}
	/* all branch heads are considered tagged */
	head->commit->tagged = true;
    }
}

static rev_ref *
rev_ref_find_name(rev_ref *h, char *name)
/* find a revision reference by name */
{
    for (; h; h = h->next)
	if (h->ref_name == name)
	    return h;
    return NULL;
}

static bool
rev_ref_is_ready(char *name, rev_list *source, rev_ref *ready)
{
    for (; source; source = source->next) {
	rev_ref *head = rev_find_head(source, name);
	if (head) {
	    if (head->parent && !rev_ref_find_name(ready, head->parent->ref_name))
		    return false;
	}
    }
    return true;
}

static rev_ref *
rev_ref_tsort(rev_ref *refs, rev_list *head)
{
    rev_ref *done = NULL;
    rev_ref **done_tail = &done;
    rev_ref *r, **prev;

//    fprintf(stderr, "Tsort refs:\n");
    while (refs) {
	for (prev = &refs; (r = *prev); prev = &(*prev)->next) {
	    if (rev_ref_is_ready(r->ref_name, head, done)) {
		break;
	    }
	}
	if (!r) {
	    announce("internal error - branch cycle\n");
	    return NULL;
	}
	*prev = r->next;
	*done_tail = r;
//	fprintf(stderr, "\t%s\n", r->name);
	r->next = NULL;
	done_tail = &r->next;
    }
    return done;
}

static int
rev_list_count(rev_list *head)
/* count entries in a rev_list */
{
    int	count = 0;
    while (head) {
	count++;
	head = head->next;
    }
    return count;
}

static int
cvs_commit_date_compare(const void *av, const void *bv)
{
    const cvs_commit	*a = *(const cvs_commit **) av;
    const cvs_commit	*b = *(const cvs_commit **) bv;
    int			t;

    /*
     * NULL entries sort last
     */
    if (!a && !b)
	return 0;
    else if (!a)
	return 1;
    else if (!b)
	return -1;
#if 0
    /*
     * Entries with no files sort next
     */
    if (a->nfiles != b->nfiles)
	return b->nfiles - a->nfiles;
#endif
    /*
     * tailed entries sort next
     */
    if (a->tailed != b->tailed)
	return(int)a->tailed - (int)b->tailed;
    /*
     * Newest entries sort first
     */
    t = -time_compare(a->date, b->date);
    if (t)
	return t;
    /*
     * Ensure total order by ordering based on file address
     */
    if ((uintptr_t) a->file > (uintptr_t) b->file)
	return -1;
    if ((uintptr_t) a->file < (uintptr_t) b->file)
	return 1;
    return 0;
}

static int
cvs_commit_date_sort(cvs_commit **commits, int ncommit)
/* sort CVS commits by date */
{
    qsort(commits, ncommit, sizeof(cvs_commit *),
	   cvs_commit_date_compare);
    /*
     * Trim off NULL entries
     */
    while (ncommit && !commits[ncommit-1])
	ncommit--;
    return ncommit;
}

bool
git_commit_has_file(git_commit *c, rev_file *f)
/* does this commit toauch a specified file? */
{
    int	i, j;

    if (!c)
	return false;
    for (i = 0; i < c->ndirs; i++) {
	rev_dir	*dir = c->dirs[i];
	for (j = 0; j < dir->nfiles; j++)
	    if (dir->files[j] == f)
		return true;
    }
    return false;
}

/*
 * These statics are part of an optimization to reduce allocation calls
 * by only doing one when more memory needs to be grabbed than the 
 * previous commit build used.
 */
static rev_file **files = NULL;
static int	    sfiles = 0;

void
git_commit_cleanup(void)
/* clean up after a commit build */
{
    if (files) {
	free(files);
	files = NULL;
	sfiles = 0;
    }
}

static git_commit *
git_commit_build(cvs_commit **revisions, cvs_commit *leader, int nrevisions)
/* build a changeset commit from a clique of CVS revisions */
{
    int		n, nfile;
    git_commit	*commit;
    int		nds;
    rev_dir	**rds;
    rev_file	*first;

    if (nrevisions > sfiles) {
	free(files);
	files = 0;
    }
    if (!files)
	/* coverity[sizecheck] Coverity has a bug here */
	files = xmalloc((sfiles = nrevisions) * sizeof(rev_file *), __func__);

    nfile = 0;
    for (n = 0; n < nrevisions; n++)
	if (revisions[n] && revisions[n]->file)
	    files[nfile++] = revisions[n]->file;
    
    if (nfile)
	first = files[0];
    else
	first = NULL;
    
    rds = rev_pack_files(files, nfile, &nds);
        
    commit = xcalloc(1, sizeof(git_commit) +
		      nds * sizeof(rev_dir *), "creating commit");
    
    commit->date = leader->date;
    commit->commitid = leader->commitid;
    commit->log = leader->log;
    commit->author = leader->author;
    commit->tail = commit->tailed = false;
    commit->tagged = commit->dead = false;
    commit->refcount = commit->serial = 0;

    commit->file = first;
    commit->nfiles = nfile;

    memcpy(commit->dirs, rds, (commit->ndirs = nds) * sizeof(rev_dir *));
    
#ifdef ORDERDEBUG
    fprintf(stderr, "commit_build: %p\n", commit);

    for (n = 0; n < nfile; n++)
	fprintf(stderr, "%s\n", revisions[n]->file->name);
    fputs("After packing:\n", stderr);
    for (n = 0; n < commit->ndirs; n++)
    {
	rev_dir *rev_dir = commit->dirs[n];
	int i;

	for (i = 0; i < rev_dir->nfiles; i++)
	    fprintf(stderr, "   file name: %s\n", rev_dir->files[i]->name);
    }
#endif /* ORDERDEBUG */

    return commit;
}

static git_commit *
git_commit_locate_date(rev_ref *branch, cvstime_t date)
{
    git_commit	*commit;

    /* PUNNING: see the big comment in cvs.h */ 
    for (commit = (git_commit *)branch->commit; commit; commit = commit->parent)
    {
	if (time_compare(commit->date, date) <= 0)
	    return commit;
    }
    return NULL;
}

static git_commit *
git_commit_locate_one(rev_ref *branch, cvs_commit *file)
{
    git_commit	*commit;

    if (!branch)
	return NULL;

    /* PUNNING: see the big comment in cvs.h */ 
    for (commit = (git_commit *)branch->commit;
	 commit;
	 commit = commit->parent)
    {
	/* PUNNING: see the big comment in cvs.h */ 
	if (cvs_commit_match((cvs_commit *)commit, file))
	    return commit;
    }
    return NULL;
}

static git_commit *
git_commit_locate_any(rev_ref *branch, cvs_commit *file)
{
    git_commit	*commit;

    if (!branch)
	return NULL;
    commit = git_commit_locate_any(branch->next, file);
    if (commit)
	return commit;
    return git_commit_locate_one(branch, file);
}

static git_commit *
git_commit_locate(rev_ref *branch, cvs_commit *file)
{
    git_commit	*commit;

    /*
     * Check the presumed trunk first
     */
    commit = git_commit_locate_one(branch, file);
    if (commit)
	return commit;
    /*
     * Now look through all branches
     */
    while (branch->parent)
	branch = branch->parent;
    return git_commit_locate_any(branch, file);
}

rev_ref *
rev_branch_of_commit(rev_list *rl, cvs_commit *commit)
{
    rev_ref	*h;
    cvs_commit	*c;

    for (h = rl->heads; h; h = h->next)
    {
	if (h->tail)
	    continue;
	for (c = h->commit; c; c = c->parent) {
	    if (cvs_commit_match(c, commit))
		return h;
	    if (c->tail)
		break;
	}
    }
    return NULL;
}

/*
 * Time of first commit along entire history
 */
static cvstime_t
cvs_commit_first_date(cvs_commit *commit)
{
    while (commit->parent)
	commit = commit->parent;
    return commit->date;
}

/*
 * Merge a set of per-file branches into a global branch
 */
static void
rev_branch_merge(rev_ref **branches, int nbranch,
		  rev_ref *branch, rev_list *rl)
{
    int nlive;
    int n;
    git_commit *prev = NULL;
    git_commit *head = NULL, **tail = &head;
    cvs_commit **revisions = xcalloc(nbranch, sizeof(cvs_commit *), "merging per-file branches");
    git_commit *commit;
    cvs_commit *latest;
    cvs_commit **p;
    time_t start = 0;

    nlive = 0;
    for (n = 0; n < nbranch; n++) {
	cvs_commit *c;
	/*
	 * Initialize revisions to head of each branch
	 */
	c = revisions[n] = branches[n]->commit;
	/*
	 * Compute number of branches with remaining entries
	 */
	if (!c)
	    continue;
	if (branches[n]->tail) {
	    c->tailed = true;
	    continue;
	}
	nlive++;
	while (c && !c->tail) {
	    if (!start || time_compare(c->date, start) < 0)
		start = c->date;
	    c = c->parent;
	}
	if (c && (c->file || c->date != c->parent->date)) {
	    if (!start || time_compare(c->date, start) < 0)
		start = c->date;
	}
    }

    for (n = 0; n < nbranch; n++) {
	cvs_commit *c = revisions[n];
	if (!c->tailed)
	    continue;
	if (!start || time_compare(start, c->date) >= 0)
	    continue;
	if (c->file)
	    announce("warning - %s too late date through branch %s\n",
		     c->file->file_name, branch->ref_name);
	revisions[n] = NULL;
    }
    /*
     * Walk down branches until each one has merged with the
     * parent branch
     */
    while (nlive > 0 && nbranch > 0) {
	for (n = 0, p = revisions, latest = NULL; n < nbranch; n++) {
	    cvs_commit *rev = revisions[n];
	    if (!rev)
		continue;
	    *p++ = rev;
	    if (rev->tailed)
		continue;
	    if (!latest || time_compare(latest->date, rev->date) < 0)
		latest = rev;
	}
	nbranch = p - revisions;

	/*
	 * Construct current commit
	 */
	commit = git_commit_build(revisions, latest, nbranch);

	/*
	 * Step each branch
	 */
	nlive = 0;
	for (n = 0; n < nbranch; n++) {
	    cvs_commit *c = revisions[n];
	    cvs_commit *to;
	    /* already got to parent branch? */
	    if (c->tailed)
		continue;
	    /* not affected? */
	    if (c != latest && !cvs_commit_match(c, latest)) {
		if (c->parent || c->file)
		    nlive++;
		continue;
	    }
	    to = c->parent;
	    /* starts here? */
	    if (!to)
		goto Kill;

	    if (c->tail) {
		/*
		 * Adding file independently added on another
		 * non-trunk branch.
		 */
		if (!to->parent && !to->file)
		    goto Kill;
		/*
		 * If the parent is at the beginning of trunk
		 * and it is younger than some events on our
		 * branch, we have old CVS adding file
		 * independently
		 * added on another branch.
		 */
		if (start && time_compare(start, to->date) < 0)
		    goto Kill;
		/*
		 * XXX: we still can't be sure that it's
		 * not a file added on trunk after parent
		 * branch had forked off it but before
		 * our branch's creation.
		 */
		to->tailed = true;
	    } else if (to->file) {
		nlive++;
	    } else {
		/*
		 * See if it's recent CVS adding a file
		 * independently added on another branch.
		 */
		if (!to->parent)
		    goto Kill;
		if (to->tail && to->date == to->parent->date)
		    goto Kill;
		nlive++;
	    }
	    revisions[n] = to;
	    continue;
	Kill:
	    revisions[n] = NULL;
	}

	*tail = commit;
	tail = &commit->parent;
	prev = commit;
    }
    /*
     * Connect to parent branch
     */
    nbranch = cvs_commit_date_sort(revisions, nbranch);
    if (nbranch && branch->parent )
    {
	int	present;

//	present = 0;
	for (present = 0; present < nbranch; present++)
	    if (revisions[present]->file) {
		/*
		 * Skip files which appear in the repository after
		 * the first commit along the branch
		 */
		if (prev && revisions[present]->date > prev->date &&
		    revisions[present]->date == cvs_commit_first_date(revisions[present]))
		{
		    /* FIXME: what does this mean? */
		    announce("warning - file %s appears after branch %s date\n",
			     revisions[present]->file->file_name, branch->ref_name);
		    continue;
		}
		break;
	    }
	if (present == nbranch)
	    *tail = NULL;
	else if ((*tail = git_commit_locate_one(branch->parent,
						 revisions[present])))
	{
	    if (prev && time_compare((*tail)->date, prev->date) > 0) {
		announce("warning - branch point %s -> %s later than branch\n",
			 branch->ref_name, branch->parent->ref_name);
		fprintf(stderr, "\ttrunk(%3d):  %s %s", n,
			 ctime_nonl(&revisions[present]->date),
			 revisions[present]->file ? " " : "D" );
		if (revisions[present]->file)
		    dump_number_file(stderr,
				      revisions[present]->file->file_name,
				      &revisions[present]->file->number);
		fprintf(stderr, "\n");
		fprintf(stderr, "\tbranch(%3d): %s  ", n,
			 ctime_nonl(&prev->file->u.date));
		dump_number_file(stderr,
				  prev->file->file_name,
				  &prev->file->number);
		fprintf(stderr, "\n");
	    }
	} else if ((*tail = git_commit_locate_date(branch->parent,
						    revisions[present]->date)))
	    announce("warning - branch point %s -> %s matched by date\n",
		     branch->ref_name, branch->parent->ref_name);
	else {
	    rev_ref	*lost;
	    fprintf(stderr, "Error: branch point %s -> %s not found.",
		    branch->ref_name, branch->parent->ref_name);

	    if ((lost = rev_branch_of_commit(rl, revisions[present])))
		fprintf(stderr, " Possible match on %s.", lost->ref_name);
	    fprintf(stderr, "\n");
	}
	if (*tail) {
	    if (prev)
		prev->tail = true;
	} else 
	    *tail = git_commit_build(revisions, revisions[0], nbranch);
    }
    for (n = 0; n < nbranch; n++)
	if (revisions[n])
	    revisions[n]->tailed = false;
    free(revisions);
    /* PUNNING: see the big comment in cvs.h */ 
    branch->commit = (cvs_commit *)head;
}

/*
 * Locate position in tree corresponding to specific tag
 */
static void
rev_tag_search(Tag *tag, cvs_commit **revisions, rev_list *rl)
{
    cvs_commit_date_sort(revisions, tag->count);
    /* tag gets parented with branch of most recent matching commit */
    tag->parent = rev_branch_of_commit(rl, revisions[0]);
    if (tag->parent)
	tag->commit = git_commit_locate(tag->parent, revisions[0]);
    if (!tag->commit) {
	announce("tag %s could not be assigned to a commit\n", tag->name);
#if 0
	/*
	 * ESR: Keith's code appeared to be trying to create a
	 * synthetic commit for unmatched tags. The comment 
	 * from "AV" below points at one reason this is probably
	 * not a good idea.  Better to fail cleanly than risk
	 * doing something wacky to the DAG.
	 */
	/* AV: shouldn't we put it on some branch? */
	tag->commit = git_commit_build(revisions, revisions[0], tag->count);
#endif
    }
    if (tag->commit)
	tag->commit->tagged = true;
}

static void
rev_ref_set_parent(rev_list *rl, rev_ref *dest, rev_list *source)
{
    rev_list	*s;
    rev_ref	*p;
    rev_ref	*max;

    if (dest->depth)
	return;

    max = NULL;
    for (s = source; s; s = s->next) {
	rev_ref	*sh;
	sh = rev_find_head(s, dest->ref_name);
	if (!sh)
	    continue;
	if (!sh->parent)
	    continue;
	p = rev_find_head(rl, sh->parent->ref_name);
	assert(p);
	rev_ref_set_parent(rl, p, source);
	if (!max || p->depth > max->depth)
	    max = p;
    }
    dest->parent = max;
    if (max)
	dest->depth = max->depth + 1;
    else
	dest->depth = 1;
}

rev_list *
rev_list_merge(rev_list *head)
{
    int		count = rev_list_count(head);
    int		n; /* used only in progress messages */
    rev_list	*rl = xcalloc(1, sizeof(rev_list), "list merge");
    rev_list	*l;
    rev_ref	*lh, *h;
    Tag		*t;
    rev_ref	**refs = xcalloc(count, sizeof(rev_ref *), "list merge");

    /*
     * Find all of the heads across all of the incoming trees
     * Yes, this is currently very inefficient
     */
    progress_begin("Find heads...", count);
    n = 0;
    for (l = head; l; l = l->next) {
	for (lh = l->heads; lh; lh = lh->next) {
	    h = rev_find_head(rl, lh->ref_name);
	    if (!h)
		rev_list_add_head(rl, NULL, lh->ref_name, lh->degree);
	    else if (lh->degree > h->degree)
		h->degree = lh->degree;
	}
	if (++n % 100 == 0)
	    progress_jump(n);
    }
    progress_jump(n);
    progress_end("done, total revisions %d", total_revisions);
    /*
     * Sort by degree so that finding branch points always works
     */
    progress_begin("Sorting...", count);
//    rl->heads = rev_ref_sel_sort(rl->heads);
    rl->heads = rev_ref_tsort(rl->heads, head);
    if (!rl->heads) {
	free(refs);
	return NULL;
    }
    progress_end(NULL);
//    for (h = rl->heads; h; h = h->next)
//	fprintf(stderr, "head %s(%d)\n",
//		 h->name, h->degree);
    /*
     * Find branch parent relationships
     */
    progress_begin("Find branch parent relationships...", count);
    for (h = rl->heads; h; h = h->next) {
	rev_ref_set_parent(rl, h, head);
//	dump_ref_name(stderr, h);
//	fprintf(stderr, "\n");
    }
    progress_end(NULL);

#ifdef ORDERDEBUG
    fputs("rev_list_merge: before common branch merge:\n", stderr);
    for (l = head; l; l = l->next) {
	for (lh = l->heads; lh; lh = lh->next) {
	    cvs_commit *commit = lh->commit;
	    fputs("rev_ref: ", stderr);
	    dump_number_file(stderr, lh->name, &lh->number);
	    fputc('\n', stderr);
	    fprintf(stderr, "commit first file: %s\n", commit->file->file_name);
	}
    }
#endif /* ORDERDEBUG */

    /*
     * Merge common branches
     */
    progress_begin("Merge common branches...", count);
    for (h = rl->heads; h; h = h->next) {
	/*
	 * Locate branch in every tree
	 */
	int nref = 0;
	for (l = head; l; l = l->next) {
	    lh = rev_find_head(l, h->ref_name);
	    if (lh)
		refs[nref++] = lh;
	}
	if (nref)
	    rev_branch_merge(refs, nref, h, rl);
	progress_step();
    }
    progress_end(NULL);
    /*
     * Compute 'tail' values
     */
    progress_begin("Compute tail values...", NO_MAX);
    rev_list_set_tail(rl);
    progress_end(NULL);

    free(refs);
    /*
     * Find tag locations
     */
    progress_begin("Find tag locations...", NO_MAX);
    for (t = all_tags; t; t = t->next) {
	cvs_commit **commits = tagged(t);
	if (commits)
	    rev_tag_search(t, commits, rl);
	else
	    announce("internal error - lost tag %s\n", t->name);
	free(commits);
    }
    progress_end(NULL);
    progress_begin("Validate...", NO_MAX);
    rev_list_validate(rl);
    progress_end(NULL);
    return rl;
}

/*
 * Icky. each file revision may be referenced many times in a single
 * tree. When freeing the tree, queue the file objects to be deleted
 * and clean them up afterwards
 */

static rev_file *rev_files;

static void
rev_file_mark_for_free(rev_file *f)
{
    if (f->file_name) {
	f->file_name = NULL;
	f->link = rev_files;
	rev_files = f;
    }
}

static void
rev_file_free_marked(void)
{
    rev_file	*f, *n;

    for (f = rev_files; f; f = n)
    {
	n = f->link;
	free(f);
    }
    rev_files = NULL;
}

rev_file *
rev_file_rev(char *name, cvs_number *n, cvstime_t date)
{
    rev_file	*f = xcalloc(1, sizeof(rev_file), "allocating file rev");

    f->file_name = name;
    f->number = *n;
    f->u.date = date;
    return f;
}

void
rev_file_free(rev_file *f)
{
    free(f);
}

static void
rev_commit_free(cvs_commit *commit, int free_files)
{
    cvs_commit	*c;

    while ((c = commit)) {
	commit = c->parent;
	if (--c->refcount == 0)
	{
	    if (free_files && c->file)
		rev_file_mark_for_free(c->file);
	    free(c);
	}
    }
}

static void
rev_head_free(rev_ref *head, int free_files)
{
    rev_ref	*h;

    while ((h = head)) {
	head = h->next;
	rev_commit_free(h->commit, free_files);
	free(h);
    }
}

void
rev_list_free(rev_list *rl, int free_files)
{
    rev_head_free(rl->heads, free_files);
    if (free_files)
	rev_file_free_marked();
    free(rl);
}

void
rev_list_validate(rev_list *rl)
{
    rev_ref	*h;
    git_commit	*c;
    for (h = rl->heads; h; h = h->next) {
	if (h->tail)
	    continue;
	/* PUNNING: see the big comment in cvs.h */ 
	for (c = (git_commit *)h->commit; c && c->parent; c = c->parent) {
	    if (c->tail)
		break;
//	    assert(time_compare(c->date, c->parent->date) >= 0);
	}
    }
}

/*
 * Generate a list of files in uniq that aren't in common
 */

static rev_file_list *
rev_uniq_file(git_commit *uniq, git_commit *common, int *nuniqp)
{
    int	i, j;
    int nuniq = 0;
    rev_file_list   *head = NULL, **tail = &head, *fl;
    
    if (!uniq)
	return NULL;
    for (i = 0; i < uniq->ndirs; i++) {
	rev_dir	*dir = uniq->dirs[i];
	for (j = 0; j < dir->nfiles; j++)
	    if (!git_commit_has_file(common, dir->files[j])) {
		fl = xcalloc(1, sizeof(rev_file_list), "rev_uniq_file");
		fl->file = dir->files[j];
		*tail = fl;
		tail = &fl->next;
		++nuniq;
	    }
    }
    *nuniqp = nuniq;
    return head;
}

bool
rev_file_list_has_filename(rev_file_list *fl, char *name)
{
    for (; fl; fl = fl->next)
	if (fl->file->file_name == name)
	    return true;
    return false;
}

/*
 * Generate a diff between two gitspace commits. Either may be NULL
 */

rev_diff *
git_commit_diff(git_commit *old, git_commit *new)
{
    rev_diff	*diff = xcalloc(1, sizeof(rev_diff), __func__);

    diff->del = rev_uniq_file(old, new, &diff->ndel);
    diff->add = rev_uniq_file(new, old, &diff->nadd);
    return diff;
}

static void
rev_file_list_free(rev_file_list *fl)
{
    rev_file_list   *next;

    while (fl) {
	next = fl->next;
	free(fl);
	fl = next;
    }
}

void
rev_diff_free(rev_diff *d)
{
    rev_file_list_free(d->del);
    rev_file_list_free(d->add);
    free(d);
}

/* end */

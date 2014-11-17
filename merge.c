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
 * These functions analyze a CVS revlist into a changeset DAG.
 *
 * merge_to_changesets() is the main function.
 */

static rev_ref *
rev_find_head(rev_list *rl, const char *name)
/* find a named branch head in a revlist - used on both CVS and gitspace sides */
{
    rev_ref	*h;

    for (h = rl->heads; h; h = h->next)
	if (h->ref_name == name)
	    return h;
    return NULL;
}

static rev_ref *
rev_ref_find_name(rev_ref *h, const char *name)
/* find a revision reference by name */
{
    for (; h; h = h->next)
	if (h->ref_name == name)
	    return h;
    return NULL;
}

static bool
rev_ref_is_ready(const char *name, cvs_repo *source, rev_ref *ready)
/*
 * name:   Name of branch we are considering adding to toposorted list.
 * source: All the cvs masters.
 * ready:  The list of branches we have already sorted.
 *
 * A branch is ready, if it has no parent, or we've already sorted it's parent
 *
 * Parent branch names are determined by examining every cvs master
 * ?? Can one branch have different parents in different masters??
 */
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
rev_ref_tsort(rev_ref *refs, cvs_repo *masters)
/* Sort a list of git space branches so parents come before children */
{
    rev_ref *done = NULL;
    rev_ref **done_tail = &done;
    rev_ref *r, **prev;

    while (refs) {
	/* search the remaining input list */
	for (prev = &refs; (r = *prev); prev = &(*prev)->next) {
            /* find a branch where we've already sorted its parent */
	    if (rev_ref_is_ready(r->ref_name, masters, done)) {
		break;
	    }
	}
	if (!r) {
	    announce("internal error - branch cycle\n");
	    return NULL;
	}
        /*
         * Remove the found branch from the input list and 
         * append it to the output list
         */
	*prev = r->next;
	*done_tail = r;
	r->next = NULL;
	done_tail = &r->next;
    }
    return done;
}

static int
cvs_master_count(const cvs_repo *masters)
/* count all digested masters in the linked list corresponding to a CVS repo */
{
    int	count = 0;
    while (masters) {
	count++;
	masters = masters->next;
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
     * Ensure total order by ordering based on commit address
     */
    if ((uintptr_t) a > (uintptr_t) b)
	return -1;
    if ((uintptr_t) a < (uintptr_t) b)
	return 1;
    return 0;
}

static int
cvs_commit_date_sort(cvs_commit **commits, int ncommit)
/* sort CVS commits by date */
{
    qsort(commits, ncommit, sizeof(cvs_commit *), cvs_commit_date_compare);
    /*
     * Trim off NULL entries
     */
    while (ncommit && !commits[ncommit-1])
	ncommit--;
    return ncommit;
}

static bool
cvs_commit_time_close(const cvstime_t a, const cvstime_t b)
/* are two timestamps within the commit-coalescence window of each other? */
{
    long	diff = (long)a - (long)b;
    if (diff < 0) diff = -diff;
    if (diff < commit_time_window)
	return true;
    return false;
}

static bool
cvs_commit_match(const cvs_commit *a, const cvs_commit *b)
/* are two CVS commits eligible to be coalesced into a changeset? */
{
    /*
     * Versions of GNU CVS after 1.12 (2004) place a commitid in
     * each commit to track patch sets. Use it if present
     */
    if (a->commitid && b->commitid)
	return a->commitid == b->commitid;
    if (a->commitid || b->commitid)
	return false;
    if (!cvs_commit_time_close(a->date, b->date))
	return false;
    if (a->log != b->log)
	return false;
    if (a->author != b->author)
	return false;
    return true;
}

/*
 * These statics are part of an optimization to reduce allocation calls
 * by only doing one when more memory needs to be grabbed than the 
 * previous commit build used.
 */
static cvs_commit **files = NULL;
static int	    sfiles = 0;

static void
git_commit_cleanup(void)
/* clean up after rev list merge */
{
    if (files) {
	free(files);
	files = NULL;
	sfiles = 0;
    }
}

static git_commit *
git_commit_build(cvs_commit **revisions, cvs_commit *leader, const int nrevisions)
/* build a changeset commit from a clique of CVS revisions */
{
    int		n, nfile;
    git_commit	*commit;
    int		nds;
    rev_dir	**rds;

    if (nrevisions > sfiles) {
	free(files);
	files = 0;
    }
    if (!files)
	/* coverity[sizecheck] Coverity has a bug here */
	files = xmalloc((sfiles = nrevisions) * sizeof(cvs_commit *), __func__);

    nfile = 0;
    for (n = 0; n < nrevisions; n++)
	if (revisions[n] && !revisions[n]->dead)
	    files[nfile++] = revisions[n];
    
    rds = rev_pack_files(files, nfile, &nds);
        
    commit = xcalloc(1, sizeof(git_commit) +
		      nds * sizeof(rev_dir *), "creating commit");
    
    commit->date = leader->date;
    commit->commitid = leader->commitid;
    commit->log = leader->log;
    commit->author = leader->author;
    commit->tail = commit->tailed = false;
    commit->dead = false;
    commit->refcount = commit->serial = 0;

    commit->nfiles = nfile;

    memcpy(commit->dirs, rds, (commit->ndirs = nds) * sizeof(rev_dir *));

    /* link each CVS commit to the gitspace commit it is part of */
    nfile = 0;
    for (n = 0; n < nrevisions; n++)
       if (revisions[n] && !revisions[n]->dead)
           files[nfile++]->gitspace = commit;

#ifdef ORDERDEBUG
    fprintf(stderr, "commit_build: %p\n", commit);

    for (n = 0; n < nfile; n++)
	fprintf(stderr, "%s\n", revisions[n]->master->name);
    fputs("After packing:\n", stderr);
    for (n = 0; n < commit->ndirs; n++)
    {
	rev_dir *rev_dir = commit->dirs[n];
	int i;

	for (i = 0; i < rev_dir->nfiles; i++)
	    fprintf(stderr, "   file name: %s\n", rev_dir->files[i]->master->name);
    }
#endif /* ORDERDEBUG */

    return commit;
}

static git_commit *
git_commit_locate_date(const rev_ref *branch, const cvstime_t date)
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
git_commit_locate_one(const rev_ref *branch, const cvs_commit *part)
/* seek a gitspace commit on branch incorporating cvs_commit */
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
	if (cvs_commit_match((cvs_commit *)commit, part))
	    return commit;
    }
    return NULL;
}

static git_commit *
git_commit_locate_any(const rev_ref *branch, const cvs_commit *part)
/* seek a gitspace commit on *any* branch incorporating cvs_commit */
{
    git_commit	*commit;

    if (!branch)
	return NULL;
    commit = git_commit_locate_any(branch->next, part);
    if (commit)
	return commit;
    return git_commit_locate_one(branch, part);
}

static git_commit *
git_commit_locate(const rev_ref *branch, const cvs_commit *cm)
{
    git_commit	*commit;

    /*
     * Check the presumed trunk first
     */
    commit = git_commit_locate_one(branch, cm);
    if (commit)
	return commit;
    /*
     * Now look through all branches
     */
    while (branch->parent)
	branch = branch->parent;
    return git_commit_locate_any(branch, cm);
}

static rev_ref *
git_branch_of_commit(const git_repo *gl, const cvs_commit *commit)
/* return the gitspace branch head that owns a specified CVS commit */
{
    rev_ref	*h;
    cvs_commit	*c;

    for (h = gl->heads; h; h = h->next)
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

static cvstime_t
cvs_commit_first_date(cvs_commit *commit)
/* return time of first commit along entire history */
{
    while (commit->parent)
	commit = commit->parent;
    return commit->date;
}

static void
merge_branches(rev_ref **branches, int nbranch,
		  rev_ref *branch, git_repo *gl)
/* merge a set of per-CVS-master branches into a gitspace DAG branch */
{
    int nlive;
    int n;
    git_commit *prev = NULL;
    git_commit *head = NULL, **tail = &head;
    cvs_commit **revisions = xmalloc(nbranch * sizeof(cvs_commit *), "merging per-file branches");
    git_commit *commit;
    cvs_commit *latest;
    cvs_commit **p;
    time_t birth = 0;

    /*
     * It is expected that the array of input branches is all CVS branches
     * tagged with some single branch name. The job of this code is to
     * build the changeset sequence for the corresponding named git branch,
     * then graft it to its parent git branch.
     */
    nlive = 0;
    for (n = 0; n < nbranch; n++) {
	cvs_commit *c;
	/*
	 * Initialize revisions to head of each branch (that is, the
	 * most recent entry).
	 */
	c = revisions[n] = branches[n]->commit;
	/*
	 * Compute number of CVS branches that are still live - that is,
	 * have remaining older CVS file commits for this branch. Non-live
	 * branches are reachable by parent-of links from the named head
	 * reference but we're past their branch point from a parent with
	 * a different name (also in our set of heads).
	 */
	if (!c)
	    continue;
	if (branches[n]->tail) {
	    c->tailed = true;
	    continue;
	}
	nlive++;

	/*
	 * This code updates our notion of the start date for the
	 * gitspace branch - that is, the date of the oldest CVS
	 * commit contributing to it.  Once we've walked all the CVS
	 * branches, 'start' should hold that oldest commit date.
	 */
	while (c && !c->tail) {
	    if (!birth || time_compare(c->date, birth) < 0)
		birth = c->date;
	    c = c->parent;
	}
	if (c && (!c->dead || c->date != c->parent->date)) {
	    if (!birth || time_compare(c->date, birth) < 0)
		birth = c->date;
	}
    }

    /*
     * This is a sanity check done just once for each gitspace
     * branch. If any of the commits at our CVS branch heads is older
     * than the git branch's imputed start date, something is badly
     * wrong.  In a sane universe with a synchronous clock this
     * shouldn't be possible, but the CVS universe is not sane and
     * attempts to do time ordering among branches can be confused by
     * clock skew on the CVS clients.
     */
    for (n = 0; n < nbranch; n++) {
	cvs_commit *c = revisions[n];
	if (!c->tailed)
	    continue;
	if (!birth || time_compare(birth, c->date) >= 0)
	    continue;
	if (!c->dead)
	    warn("warning - %s branch %s: tip commit older than imputed branch join\n",
		     c->master->name, branch->ref_name);
	revisions[n] = NULL;
    }

    /*
     * Walk down CVS branches creating gitspace commits until each CVS
     * branch has merged with its parent.
     */
    while (nlive > 0 && nbranch > 0) {
	/*
	 * Gather the next set of CVS commits down the branch and
	 * figure out which (non-tailed) one of them is latest in
	 * time.  It will be the leader for the git commit build.
	 */
	for (n = 0, p = revisions, latest = NULL; n < nbranch; n++) {
	    /*
	     * Squeeze null commit pointers out of the current set.
	     */
	    cvs_commit *rev = revisions[n];
	    if (!rev)
		continue;
	    *p++ = rev;
	    if (rev->tailed)
		continue;
	    if (!latest || time_compare(latest->date, rev->date) < 0)
		latest = rev;
	}
	assert(latest != NULL);
	nbranch = p - revisions;

	/*
	 * Construct current commit from the set of CVS commits 
	 * accumulated the last time around the loop.
	 */
	commit = git_commit_build(revisions, latest, nbranch);

	/*
	 * Step down each CVS branch in parallel.  Our goal is to land on
	 * a clique of matching CVS commits that will  be made into a 
	 * matching gitspace commit on the next time around the loop.
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
		if (c->parent || !c->dead)
		    nlive++;
		continue;
	    }
	    to = c->parent;
	    /* 
	     * CVS branch starts here?  If so, drop it out of
	     * the revision set and keep going.
	     */
	    if (!to)
		goto Kill;

	    if (c->tail) {
		/*
		 * Adding file independently added on another
		 * non-trunk branch.
		 */
		if (!to->parent && to->dead)
		    goto Kill;
		/*
		 * If the parent is at the beginning of trunk
		 * and it is younger than some events on our
		 * branch, we have old CVS adding file
		 * independently added on another branch.
		 */
		if (birth && time_compare(birth, to->date) < 0)
		    goto Kill;
		/*
		 * XXX: we still can't be sure that it's
		 * not a file added on trunk after parent
		 * branch had forked off it but before
		 * our branch's creation.
		 */
		to->tailed = true;
	    } else if (!to->dead) {
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

	    /*
	     * Commit is either not tailed or passed all the special-case
	     * tests for tailed commits. Leave it in the set for the next
	     * changeset construction.
	     */
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
     * Gitspace branch construction is done. Now connect it to its
     * parent branch.  The CVS commits now referenced in the revisions
     * array are for the oldest commit on the branch (the last clique
     * to be collected in the previous phase).  This is not the brahch's
     * root commit, but the child of that root.
     */
    nbranch = cvs_commit_date_sort(revisions, nbranch);
    if (nbranch && branch->parent )
    {
	int	present;

	for (present = 0; present < nbranch; present++) {
	    if (!revisions[present]->dead) {
		/*
		 * Skip files which appear in the repository after
		 * the first commit along the branch
		 */
		if (prev && revisions[present]->date > prev->date &&
		    revisions[present]->date == cvs_commit_first_date(revisions[present]))
		{
		    /* FIXME: what does this mean? */
		    warn("file %s appears after branch %s date\n",
			     revisions[present]->master->name, branch->ref_name);
		    continue;
		}
		break;
	    }
	}
	if (present == nbranch)
	    /* 
	     * Branch join looks normal, we can just go ahead and build
	     * the last commit.
	     */
	    *tail = NULL;
	else if ((*tail = git_commit_locate_one(branch->parent,
						 revisions[present])))
	{
	    if (prev && time_compare((*tail)->date, prev->date) > 0) {
		cvs_commit *first;
		warn("warning - branch point %s -> %s later than branch\n",
			 branch->ref_name, branch->parent->ref_name);
		warn("\ttrunk(%3d):  %s %s", n,
			 cvstime2rfc3339(revisions[present]->date),
			 revisions[present]->dead ? "D" : " " );
		if (!revisions[present]->dead)
		    dump_number_file(LOGFILE,
				      revisions[present]->master->name,
				      revisions[present]->number);
		warn("\n");
		/*
		 * The file part of the error message could be spurious for
		 * a multi-file commit, alas.  It wasn't any better back when
		 * both flavors of commit had dedicated 'file' members; the
		 * problem is that we can't actually know which CVS file
		 * commit is the right one for purposes of this message.
		 */
		fprintf(LOGFILE, "\tbranch(%3d): %s  ",
			n, cvstime2rfc3339(prev->date));
		first = prev->dirs[0]->files[0];
		dump_number_file(LOGFILE,
				  first->master->name,
				  first->number);
		fprintf(LOGFILE, "\n");
	    }
	} else if ((*tail = git_commit_locate_date(branch->parent,
						    revisions[present]->date)))
	    warn("warning - branch point %s -> %s matched by date\n",
		     branch->ref_name, branch->parent->ref_name);
	else {
	    rev_ref	*lost;
	    warn("error - branch point %s -> %s not found.",
		branch->ref_name, branch->parent->ref_name);

	    if ((lost = git_branch_of_commit(gl, revisions[present])))
		warn(" Possible match on %s.", lost->ref_name);
	    fprintf(LOGFILE, "\n");
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
rev_tag_search(tag_t *tag, cvs_commit **revisions, git_repo *gl)
{
    rev_ref	*h;
    git_commit	*gc;

    cvs_commit_date_sort(revisions, tag->count);
    /* tag gets parented with oldest branch with a matching commit */
    tag->parent = NULL;
    for (h = gl->heads; h; h = h->next)
    {
	if (h->tail)
	    continue;
	for (gc = (git_commit *)h->commit; gc; gc = gc->parent) {
	    if (gc == revisions[0]->gitspace) {
		tag->parent = h;
		goto breakout;
	    }
	    if (gc->tail)
		break;
	}
    }
breakout:
#ifdef __UNUSED__
    if (tag->parent == NULL)
	/*
	 * This is what the code used to do before we put in the direct check
	 * against the gitspace pointer.  Advantage: my note requiring an
	 * exact match, it avoids omitting "could not be assigned"
	 * messages for a master that has been lifted out of context.
	 * Disadvantage: those messages may be a correctness feature.
	 * Also this computation is slightly more expensive.
	 */
	tag->parent = git_branch_of_commit(gl, revisions[0]);
#endif /* __UNUSED__ */
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
}

static void
rev_ref_set_parent(git_repo *gl, rev_ref *dest, cvs_repo *source)
/* compute parent relationships among gitspace branches */
{
    cvs_master	*s;
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
	p = rev_find_head(gl, sh->parent->ref_name);
	assert(p);
	rev_ref_set_parent(gl, p, source);
	if (!max || p->depth > max->depth)
	    max = p;
    }
    dest->parent = max;	/* where the magic happens */
    if (max)
	dest->depth = max->depth + 1;
    else
	dest->depth = 1;
}

git_repo *
merge_to_changesets(cvs_repo *masters, int verbose)
/* entry point - merge CVS revision lists to a gitspace DAG */
{
    int		head_count = 0, count = cvs_master_count(masters);
    int		n; /* used only in progress messages */
    git_repo	*gl = xcalloc(1, sizeof(git_repo), "list merge");
    cvs_master	*master;
    rev_ref	*lh, *h;
    tag_t	*t;
    rev_ref	**refs = xcalloc(count, sizeof(rev_ref *), "list merge");

    /*
     * Find all of the heads across all of the incoming trees.
     * Use them to initialize named branch heads in the output list.
     * Yes, this is currently very inefficient.
     */
    progress_begin("Make DAG branch heads...", count);
    n = 0;
    for (master = masters; master; master = master->next) {
	for (lh = master->heads; lh; lh = lh->next) {
	    h = rev_find_head(gl, lh->ref_name);
	    if (!h) {
		head_count++;
		rev_list_add_head(gl, NULL, lh->ref_name, lh->degree);
	    } else if (lh->degree > h->degree)
		h->degree = lh->degree;
	}
	if (++n % 100 == 0)
	    progress_jump(n);
    }
    progress_jump(n);
    progress_end(NULL);
    /*
     * Sort by degree so that finding branch points always works.
     * In later operations we always want to walk parent branches 
     * before children, with trunk first.
     */
    progress_begin("Sorting...", count);
    gl->heads = rev_ref_tsort(gl->heads, masters);
    if (!gl->heads) {
	free(refs);
	/* coverity[leaked_storage] */
	return NULL;
    }
    progress_end(NULL);

    if (verbose) {
	/*
	 * Display the result of the branch toposort.
	 * The "master" branch should always be at the front
	 * of the list.
	 */
	debugmsg("Sorted branches are:\n");
	for (h = gl->heads; h; h = h->next)
	    debugmsg("head %s(%d)\n", h->ref_name, h->degree);
    }
    /*
     * Compute branch parent relationships.
     */
    progress_begin("Compute branch parent relationships...", head_count);
    for (h = gl->heads; h; h = h->next) {
	rev_ref_set_parent(gl, h, masters);
	progress_step();
    }
    progress_end(NULL);

#ifdef ORDERDEBUG
    fputs("merge_to_changesets: before common branch merge:\n", stderr);
    for (master = masters; master; master = master->next) {
	for (lh = master->heads; lh; lh = lh->next) {
	    cvs_commit *commit = lh->commit;
	    fputs("rev_ref: ", stderr);
	    dump_number_file(stderr, lh->ref_name, lh->number);
	    fputc('\n', stderr);
	    fprintf(stderr, "commit first file: %s\n", commit->master->name);
	}
    }
#endif /* ORDERDEBUG */

    /*
     * Merge common branches
     */
    progress_begin("Merge common branches...", head_count);
    for (h = gl->heads; h; h = h->next) {
	/*
	 * For this imputed gitspace branch, locate the corresponding
	 * set of CVS branches from every master.
	 */
	int nref = 0;
	for (master = masters; master; master = master->next) {
	    lh = rev_find_head(master, h->ref_name);
	    if (lh)
		refs[nref++] = lh;
	}
	if (nref)
	    /* 
	     * Merge those branches into a single gitspace branch
	     * and add that to the output revlist on gl.
	     */
	    merge_branches(refs, nref, h, gl);
	progress_step();
    }
    progress_end(NULL);
    /*
     * Compute 'tail' values.  These allow us to recognize branch joins
     * so we can write efficient traversals that walk branches without
     * wandering on to their parent branches.
     */
    progress_begin("Compute tail values...", NO_MAX);
    rev_list_set_tail(gl);
    progress_end(NULL);

    free(refs);
    /*
     * Find tag locations.  The goal is to associate each tag object 
     * (which normally corresponds to a clique of named tags, one per master)
     * with the right gitspace commit.
     */
    progress_begin("Find tag locations...", NO_MAX);
    for (t = all_tags; t; t = t->next) {
	cvs_commit **commits = tagged(t);
	if (commits)
	    rev_tag_search(t, commits, gl);
	else
	    announce("internal error - lost tag %s\n", t->name);
	free(commits);
    }
    progress_end(NULL);

    //progress_begin("Validate...", NO_MAX);
    //rev_list_validate(gl);
    //progress_end(NULL);

    git_commit_cleanup();

    return gl;
}

/*
 * Generate a list of files in uniq that aren't in common
 */

static cvs_commit_list *
rev_uniq_file(git_commit *uniq, git_commit *common, int *nuniqp)
{
    int	i, j;
    int nuniq = 0;
    cvs_commit_list   *head = NULL, **tail = &head, *fl;
    
    if (!uniq)
	return NULL;
    for (i = 0; i < uniq->ndirs; i++) {
	rev_dir	*dir = uniq->dirs[i];
	for (j = 0; j < dir->nfiles; j++)
	    if (dir->files[j]->gitspace != common) {
		fl = xcalloc(1, sizeof(cvs_commit_list), "rev_uniq_file");
		fl->file = dir->files[j];
		*tail = fl;
		tail = &fl->next;
		++nuniq;
	    }
    }
    *nuniqp = nuniq;
    return head;
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
cvs_commit_list_free(cvs_commit_list *fl)
{
    cvs_commit_list   *next;

    while (fl) {
	next = fl->next;
	free(fl);
	fl = next;
    }
}

void
rev_diff_free(rev_diff *d)
{
    cvs_commit_list_free(d->del);
    cvs_commit_list_free(d->add);
    free(d);
}

/* end */

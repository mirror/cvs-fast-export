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
#include "revdir.h"
/*
 * These functions analyze a CVS revlist into a changeset DAG.
 *
 * merge_to_changesets() is the main function.
 */

static rev_ref *
rev_find_head(head_list *rl, const char *name)
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
parents_in_revlist(const char *child_name, rev_ref *rev_list,
		   cvs_master *source, size_t nsource)
/*
 * See whether all the parents of child_name are in rev_list
 * If child_name has no parents (e.g. master branch) then this is
 * trivally true.
 *
 * Parent branch names are determined by examining every cvs master.  See the
 * general note on branch matching under merge_changesets().
 */
{
    cvs_master *cm;
    for (cm = source; cm < source + nsource; cm++) {
	rev_ref *head = rev_find_head(cm, child_name);
	if (head) {
	    if (head->parent && !rev_ref_find_name(rev_list, head->parent->ref_name))
		    return false;
	}
    }
    return true;
}

static rev_ref *
rev_ref_tsort(rev_ref *git_branches, cvs_master *masters, size_t nmasters)
/* Sort a list of git space branches so parents come before children */
{
    rev_ref *sorted_git_branches = NULL;
    rev_ref **sorted_tail = &sorted_git_branches;
    rev_ref *r, **prev;

    while (git_branches) {
	/* search the remaining input list */
	for (prev = &git_branches; (r = *prev); prev = &(*prev)->next) {
            /*
	     * Find a branch where we've already sorted its parents.
	     * Toposorting with this relation will put the (parentless) trunk first,
	     * and child branches after their respective parent branches.
	     */
	    if (parents_in_revlist(r->ref_name, sorted_git_branches, masters, nmasters)) {
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
	*sorted_tail = r;
	r->next = NULL;
	sorted_tail = &r->next;
    }
    return sorted_git_branches;
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
/* not all platforms have qsort_r so use something global for compare func */
static int          srevisions = 0;
static cvs_commit **revisions = NULL;
static size_t      *sort_buf = NULL;
static size_t      *sort_temp = NULL;

static void
alloc_revisions(size_t nrev) 
/* Allocate buffers for merge_branches */
{
    if (srevisions < nrev) {
	/* As first branch is master, don't expect this to be hit more than once */
	revisions = xrealloc(revisions, nrev * sizeof(cvs_commit *), __func__);
	sort_buf = xrealloc(sort_buf, nrev * sizeof(size_t), __func__);
	sort_temp = xrealloc(sort_temp, nrev * sizeof(size_t), __func__);
	srevisions = nrev;
    }    
}

static void
merge_branches_cleanup(void)
{
    if (srevisions > 0) {
	free(revisions);
	free(sort_buf);
	free(sort_temp);
	srevisions = 0;
    }
}

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
git_commit_build(cvs_commit **revisions, cvs_commit *leader,
		 const int nrevisions, const int nactive)
/* build a changeset commit from a clique of CVS revisions */
{
    size_t	n, nfile;
    git_commit	*commit;

    if (nactive > sfiles) {
	free(files);
	files = NULL;
    }
    if (!files)
	/* coverity[sizecheck] Coverity has a bug here */
	files = xmalloc((sfiles = nactive) * sizeof(cvs_commit *), __func__);
    
    commit = xmalloc( sizeof(git_commit), "creating commit");
    
    commit->parent = NULL;
    commit->date = leader->date;
    commit->commitid = leader->commitid;
    commit->log = leader->log;
    commit->author = leader->author;
    commit->tail = commit->tailed = false;
    commit->dead = false;
    commit->refcount = commit->serial = 0;

    /*
     * Previously, the rev_dirs array was at the end of the git_commit struct.
     * This means we didn't know the size until after the call to rev_pack_files.
     * So, we had to do this iteration through the revisions twice, once to
     * pick out the files and then later to assign the back link.
     * In large repositories it is a hotspot. Before this, each iteration was
     * 13% of the CPU time of the netbsd-pkgsrc conversion.
     */
    nfile = 0;
    for (n = 0; n < nrevisions; n++)
	if (revisions[n] && !revisions[n]->dead) {
	    files[nfile] = revisions[n];
	    /* link each CVS commit to the gitspace commit it is part of */
	    files[nfile++]->gitspace = commit;
	}

    revdir_pack_files(files, nfile, &commit->revdir);
    /* Possible truncation */
    commit->nfiles = nfile;

#ifdef ORDERDEBUG
    debugmsg("commit_build: %p\n", commit);

    for (n = 0; n < nfile; n++)
	debugmsg("%s\n", revisions[n]->master->name);
    fputs("After packing:\n", LOGFILE);
    revdir_iter *i = revdir_iter_alloc(&commit->revdir);
    cvs_commit *c;
    while((c = revdir_iter_next(i)))
	debugmsg("   file name: %s\n", c->master->name);

#endif /* ORDERDEBUG */

    return commit;
}

static git_commit *
git_commit_locate_date(const rev_ref *branch, const cvstime_t date)
/* on branch, locate a commit within fuzz-time distance of date */
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

static int
compare_clique(const void *a, const void *b)
/*
 * Comparator that will sort null commits first, tailed commits
 * last and by time order, latest first in the middle
 * used for finding a clique of cvs commits to make a git commit
 */
{
    size_t i1 = *(size_t *)a, i2 = *(size_t *)b;
    const cvs_commit
	*c1 = revisions[i1],
	*c2 = revisions[i2];

    /* Null commits come first */
    if (!c1 && !c2)
	return 0;
    if (!c1)
	return -1;
    if (!c2)
	return 1;

    /* tailed commits come last*/
    if (c1->tailed && c2->tailed)
	return 0;
    if (c1->tailed)
	return 1;
    if (c2->tailed)
	return -1;

    /* most recent first date order in between */
    return time_compare(c2->date, c1->date);

    /* Could make it a total order by comparing a and b */
}

static void
resort_revs (size_t skip, size_t nrev, size_t resort) {
    /*
     * Resort the revisions array. First skip items are already sorted
     * and guaranteed to be nothing sorting less then them.
     * Next resort items are not sorted, remainder of the array is
     * Depending on how much we have altered, either sort the array or
     * sort the changed bits and merge the two sorted parts
     * There's probably an optimal cutoff point, which I haven't
     * calculated
     * sort func is hard coded to compare_clique.
     */
    if (resort > (nrev - skip) / 2)
	/* Sort the whole array again (except the previous nulls) */
	qsort(sort_buf + skip, nrev - skip, sizeof(size_t), compare_clique);
    else {
	size_t p, q, i;
	if (resort > 1)
	    /* sort the head of the array */
	    qsort(sort_buf + skip, resort, sizeof(size_t),  compare_clique);
	/* merge the two sorted pieces of array into sort_temp */
	p = skip;
	q = skip + resort;
	i = 0;
	while (p < skip + resort || q < nrev) {
	    if (p == skip + resort)
		/* Data is alread in the right place
		 * no need to copy to sort_temp and back
		 */
		break;
	    else if (q == nrev) {
		memcpy(sort_temp + i, sort_buf + p, sizeof(size_t) * (skip + resort - p));
		i += skip + resort - p;
		break;
	    }
	    else if (compare_clique(&sort_buf[p], &sort_buf[q]) < 0)
		sort_temp[i++] = sort_buf[p++];
	    else
		sort_temp[i++] = sort_buf[q++];
	}
	/* Copy the resorted piece back into sort_buf */
	memcpy(sort_buf + skip, sort_temp, sizeof(size_t) * i);
    }
}

static void
merge_branches(rev_ref **branches, int nbranch,
		  rev_ref *branch, git_repo *gl)
/* merge a set of per-CVS-master branches into a gitspace DAG branch */
{
    int nlive, n, nrev = nbranch;
    git_commit *prev = NULL, *head = NULL, **tail = &head, *commit;
    cvs_commit *latest;
    time_t birth = 0;
#ifdef ORDERDEBUG
    static ulong oc = 0;
#endif /* ORDERDEBUG */

    alloc_revisions(nrev);
    /*
     * It is expected that the array of input branches is all CVS branches
     * tagged with some single branch name. The job of this code is to
     * build the changeset sequence for the corresponding named git branch,
     * then graft it to its parent git branch.
     *
     * We want to keep the revisions array in the same order as branches
     * was passed to us (modulo removing items) if not the fast output
     * phase suffers badly.
     * However, for the purpose of computing cliques it is very useful to
     * have the array sorted by date (with special handling for null and
     & tailed commits)
     * So, we will maintain a sort_buf which has indexes into the revisons
     * array but is sorted to help us find cliques. On any given iteration
     * sort_buf indexes point to the following types of commit
     *
     * 0...skip..........................nrev
     * |null|non tailed newest first|tailed|
     *
     * As each clique will be found near "skip" we may only have to look
     * at a small number of commits to find the clique. This leaves the
     * end of the array sorted. After the clique items are moved onto
     * the next commits in their master, we can sort the changed items
     * and then merge the two sorted pieces of the array
     *
     * resort tracks the number of items past skip we have touched.
     * nbranch is always nrev - skip, might be worth eliding it.
     */
    nlive = 0;
    for (n = 0; n < nbranch; n++) {
	/*
	 * Initialize revisions to head of each branch (that is, the
	 * most recent entry).
	 */
	cvs_commit *c = revisions[n] = branches[n]->commit;
	sort_buf[n] = n;
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

    /* Initial sort into null/date/tailed order */
    qsort(sort_buf, nrev, sizeof(size_t), compare_clique);
    size_t skip = 0;
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
	for (n = skip, latest = NULL; n < nrev; n++) {
	    cvs_commit *rev = revisions[sort_buf[n]];
	    if (!rev) {
		skip++;
		nbranch--;
		continue;
	    }
	    /* array is sorted so we get the latest live item after the nulls */
	    latest = rev;
	    break;
	}
	assert(latest != NULL);

	/*
	 * Construct current commit from the set of CVS commits 
	 * accumulated the last time around the loop.
	 * This is the point at which revisions needs to be sorted
	 * by master for rev dir packing to perform
	 */
	commit = git_commit_build(revisions, latest, nrev, nbranch);

	/*
	 * Step down each CVS branch in parallel.  Our goal is to land on
	 * a clique of matching CVS commits that will  be made into a 
	 * matching gitspace commit on the next time around the loop.
	 */
	nlive = 0;
	/* worst case, we have to resort everything */
	size_t resort = nbranch;
	bool can_match = true;
	for (n = skip; n < nrev; n++) {
	    cvs_commit *c = revisions[sort_buf[n]];
	    cvs_commit *to;

	    /*
	     * Already got to parent branch?
             * We've sorted the list so everything else is tailed
	     */
	    if (c->tailed)
		break;

	    if (c != latest && can_match && !cvs_commit_time_close(latest->date, c->date)) {
		/*
		 * because we are in date order, one we hit something too
                 * far off, we can't get anything else in the clique
                 * unless there are cases where things with the same commitid
                 * have wildly differing dates
                 */
		can_match = false;
		/* how much of the array might now be unsorted */
		resort = n - skip;
	    }
	    /* not affected? */
	    if (c != latest && (!can_match || !cvs_commit_match(c, latest))) {
		if (c->parent || !c->dead)
		    nlive++;
		/*
		 * if we've found the clique, and at least one branch
		 * is still live then bail
                 * note, we are guaranteed to set resort before we get here
                 */
		if (!can_match && nlive > 0)
		    break;
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
	    revisions[sort_buf[n]] = to;
	    continue;
	Kill:
	    revisions[sort_buf[n]] = NULL;
	}
	/* we've changed some revs to their parents. Resort */
	resort_revs(skip, nrev, resort);

#ifdef ORDERDEBUG
	/*
	 * Sanity check that we've ordered things properly
	 * oc is useful for a conditional breakpoint if not
	 */
	size_t i;
	for (i = skip + 1; i < nrev; i++) {
	    if (compare_clique(&sort_buf[i-1], &sort_buf[i]) > 0)
		warn("Sort broken oc: %lu\n", oc);
	}
	oc++;
#endif /* ORDERDEBUG */
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
		revdir_iter *ri = revdir_iter_alloc(&prev->revdir);
		first = revdir_iter_next(ri);
		free(ri);
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
	    *tail = git_commit_build(revisions, revisions[0], nrev, nbranch);
    }

    for (n = 0; n < nbranch; n++)
	if (revisions[n])
	    revisions[n]->tailed = false;

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
	warn("tag %s could not be assigned to a commit\n", tag->name);
#if 0
	/*
	 * ESR: Keith's code appeared to be trying to create a
	 * synthetic commit for unmatched tags. The comment 
	 * from Al Viro below points at one reason this is probably
	 * not a good idea.  Better to fail cleanly than risk
	 * doing something wacky to the DAG.
	 */
	/* AV: shouldn't we put it on some branch? */
	tag->commit = git_commit_build(revisions, revisions[0], tag->count);
#endif
    }
}

static void
rev_ref_set_parent(git_repo *gl, rev_ref *dest, cvs_master *source, size_t nmasters)
/* compute parent relationships among gitspace branches */
{
    cvs_master	*s;
    rev_ref	*p, *max;

    if (dest->depth)
	return;

    max = NULL;
    for (s = source; s < source + nmasters; s++) {
	rev_ref	*sh;
	sh = rev_find_head(s, dest->ref_name);
	if (!sh)
	    continue;
	if (!sh->parent)
	    continue;
	p = rev_find_head((head_list*)gl, sh->parent->ref_name);
	assert(p);
	rev_ref_set_parent(gl, p, source, nmasters);
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
merge_to_changesets(cvs_master *masters, size_t nmasters, int verbose)
/* entry point - merge CVS revision lists to a gitspace DAG */
{
    size_t	head_count = 0;
    int		n; /* used only in progress messages */
    git_repo	*gl = xcalloc(1, sizeof(git_repo), "list merge");
    cvs_master	*cm;
    rev_ref	*lh, *h;
    tag_t	*t;
    rev_ref	**refs = xcalloc(nmasters, sizeof(rev_ref *), "list merge");

    /*
     * It is expected that the branch trees in all CVS masters have
     * equivalent sets of parent-child relationships, but not
     * necessarily that the branch nodes always occur in the same
     * order. Equivalently, it may not be the case that the branch IDs
     * of equivalent named branches in different masters are the
     * same. So the only way we can group CVS branches into cliques
     * that should be bundled into single gitspace branches is by the
     * labels at their tips.
     *
     * First, find all of the named heads across all of the incoming
     * CVS trees.  Use them to initialize named branch heads in the
     * output list.  Yes, this is currently very inefficient.
     */
    progress_begin("Make DAG branch heads...", nmasters);
    n = 0;
    for (cm = masters; cm < masters + nmasters; cm++) {
	for (lh = cm->heads; lh; lh = lh->next) {
	    h = rev_find_head((head_list *)gl, lh->ref_name);
	    if (!h) {
		head_count++;
		rev_list_add_head((head_list *)gl, NULL, lh->ref_name, lh->degree);
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
    progress_begin("Sorting...", nmasters);
    gl->heads = rev_ref_tsort(gl->heads, masters, nmasters);
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
	rev_ref_set_parent(gl, h, masters, nmasters);
	progress_step();
    }
    progress_end(NULL);

#ifdef ORDERDEBUG
    fputs("merge_to_changesets: before common branch merge:\n", stderr);
    for (cm = masters; cm < masters + nmasters; cm++) {
	for (lh = cm->heads; lh; lh = lh->next) {
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
	for (cm = masters; cm < masters + nmasters; cm++) {
	    lh = rev_find_head(cm, h->ref_name);
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
    merge_branches_cleanup();
    revdir_free_bufs();
    /*
     * Compute 'tail' values.  These allow us to recognize branch joins
     * so we can write efficient traversals that walk branches without
     * wandering on to their parent branches.
     */
    progress_begin("Compute tail values...", NO_MAX);
    rev_list_set_tail((head_list *)gl);
    progress_end(NULL);

    free(refs);
    /*
     * Find tag locations.  The goal is to associate each tag object 
     * (which normally corresponds to a clique of named tags, one per master)
     * with the right gitspace commit.
     */
    progress_begin("Find tag locations...", tag_count);
    for (t = all_tags; t; t = t->next) {
	cvs_commit **commits = tagged(t);
	if (commits)
	    rev_tag_search(t, commits, gl);
	else
	    announce("internal error - lost tag %s\n", t->name);
	free(commits);
	progress_step();
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
    int nuniq = 0;
    cvs_commit_list   *head = NULL, **tail = &head, *fl;
    cvs_commit *c;

    if (!uniq)
	return NULL;
    revdir_iter *ri = revdir_iter_alloc(&uniq->revdir);
    while ((c = revdir_iter_next(ri))) {
	if (c->gitspace != common) {
	    fl = xcalloc(1, sizeof(cvs_commit_list), "rev_uniq_file");
	    fl->file = c;
	    *tail = fl;
	    tail = &fl->next;
	    ++nuniq;
	}
    }
    free(ri);
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

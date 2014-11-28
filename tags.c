#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef THREADS
#include <pthread.h>
#endif /* THREADS */

#include "cvs.h"

/*
 * Manage objects that represent gitspace lightweight tags.
 *
 * Because we're going to try to unify tags from different branches
 * the tag table should *not* be local to any one master.  
 */

static tag_t *table[4096];

tag_t  *all_tags;
size_t tag_count = 0;

#ifdef THREADS
static pthread_mutex_t tag_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif /* THREADS */

static int tag_hash(const char *name)
/* return the hash code for a specified tag */ 
{
    uintptr_t l = (uintptr_t)name;
    int res = 0;
    while (l) {
	res ^= l;
	l >>= 12;
    }
    return res & 4095;
}

static tag_t *find_tag(const char *name)
/* look up a tag by name */
{
    int hash = tag_hash(name);
    tag_t *tag;
    for (tag = table[hash]; tag; tag = tag->hash_next)
	if (tag->name == name)
	    return tag;
    tag = xcalloc(1, sizeof(tag_t), "tag lookup");
    tag->name = name;
    tag->hash_next = table[hash];
    table[hash] = tag;
    tag->next = all_tags;
    all_tags = tag;
    tag_count++;
    return tag;
}

void tag_commit(cvs_commit *c, const char *name, cvs_file *cvsfile)
/* add a CVS commit to the list associated with a named tag */
{
    tag_t *tag;
#ifdef THREADS
    if (threads > 1)
	pthread_mutex_lock(&tag_mutex);
#endif /* THREADS */
    tag = find_tag(name);
    if (tag->last == cvsfile->gen.master_name) {
	announce("duplicate tag %s in CVS master %s, ignoring\n",
		 name, cvsfile->gen.master_name);
    } else {
	tag->last = cvsfile->gen.master_name;
	if (!tag->left) {
	    chunk_t *v = xmalloc(sizeof(chunk_t), __func__);
	    v->next = tag->commits;
	    tag->commits = v;
	    tag->left = Ncommits;
	}
	tag->commits->v[--tag->left] = c;
	tag->count++;
    }
#ifdef THREADS
    if (threads > 1)
	pthread_mutex_unlock(&tag_mutex);
#endif /* THREADS */
}

cvs_commit **tagged(tag_t *tag)
/* return an allocated list of pointers to commits with the specified tag */
{
    cvs_commit **v = NULL;

    if (tag->count) {
	/* not mutex-locked because it's not called during analysis phase */
	cvs_commit **p = xmalloc(tag->count * sizeof(*p), __func__);
	chunk_t *c = tag->commits;
	int n = Ncommits - tag->left;

	v = p;
	memcpy(p, c->v + tag->left, n * sizeof(*p));

	for (c = c->next, p += n; c; c = c->next, p += Ncommits)
	    memcpy(p, c->v, Ncommits * sizeof(*p));
    }
    return v;
}

void discard_tags(void)
/* discard all tag storage */
{
    tag_t *tag = all_tags;
    all_tags = NULL;
    while (tag) {
	tag_t *p = tag->next;
	chunk_t *c = tag->commits;
	while (c) {
	    chunk_t *next = c->next;
	    free(c);
	    c = next;
	}
	free(tag);
	tag = p;
    }
}

/* end */

/*
 * The per-CVS-master node list this module builds and exports is used
 * during the analysis phase (only) to walk through all deltas and 
 * build them into snapshots.
 */

#include "cvs.h"

/*
 * The choice of these mixing functions can have a major effect on 
 * lookup speed. There are two things to note about CVS numbers that
 * are relevant. One is that that they're not strings; the component
 * numbers are not limited to 0..255, though it would be highly odd
 * for anything but the rightmost part to achieve that high a value.
 * The other is that they're not uniformly distributed; low component 
 * values will be far more common than high ones and most of the variation
 * will be towards the right-hand end.
 *
 * This is Keith's original pair of mixers.  The second one tries to
 * weight the hash to be more senstive to the right-hand end, but
 * implicitly assumes that values there above 255 will be nonexistent
 * or rare. The first one has poor distribution properties and the
 * sole merit of being a fast operation.
 *
 * 	HASHMIX1(hash, new)	hash += new
 * 	HASHMIX2(hash, new)	hash = ((hash << 8) + new)
 *
 * Here are some plausible alternatives for the first mixer, from
 * <http://www.cse.yorku.ca/~oz/hash.html>. Descriptions are from there.
 *
 * DJB2: this algorithm (which effectively multiplies the old hash by 33 
 * before adding the new value) was first reported by dan bernstein
 * many years ago in comp.lang.c. another version of this algorithm
 * (now favored by bernstein) uses xor: hash(i) = hash(i - 1) * 33 ^
 * str[i]; the magic of number 33 (why it works better than many other
 * constants, prime or not) has never been adequately explained.
 *
 * (Note: for reasons not explained at the source page, hash is 
 * initialized to 5381 before the djb2 mixing begins.)
 */
#define DJB2(hash, new)	hash = (((hash << 5) + hash) + new)
/*
 * SDBM: this algorithm was created for sdbm (a public-domain
 * reimplementation of ndbm) database library. it was found to do well
 * in scrambling bits, causing better distribution of the keys and
 * fewer splits. it also happens to be a good general hashing function
 * with good distribution. the actual function is hash(i) = hash(i -
 * 1) * 65599 + str[i]; what is included below is the faster version
 * used in gawk. [there is even a faster, duff-device version] the
 * magic constant 65599 was picked out of thin air while experimenting
 * with different constants, and turns out to be a prime. this is one
 * of the algorithms used in berkeley db (see sleepycat) and
 * elsewhere.
 */
#define SDBM(hash, new)	hash = (new + (hash << 6) + (hash << 16) - hash)
/*
 * We choose SDBM because it seems less likely to have corner cases
 * on large components, and we change Keith's second mixer in case
 * the most rapidly varying component goes above 256.
 */

#define HASHMIX1(hash, new)	SDBM(hash, new)
#define HASHMIX2(hash, new)	hash = ((hash << 10) + new)

unsigned long
hash_cvs_number(const cvs_number key)
{
    int i;
    unsigned long hashval;

    for (i = 0, hashval = 0; i < key.c - 1; i++)
	HASHMIX1(hashval, key.n[i]);
    HASHMIX2(hashval, key.n[key.c - 1]);
    return hashval;
}

static node_t *
node_for_cvs_number(nodehash_t *context, const cvs_number *const n)
/* look up the node associated with a specified CVS release number */
{
    cvs_number key = *n;
    node_t *p;
    unsigned int hash;
    int i;

    if (key.c > 2 && !key.n[key.c - 2]) {
	/* not found a repo that exercises this yet */
	key.n[key.c - 2] = key.n[key.c - 1];
	key.c--;
    }
    if (key.c & 1)
	/* or this */
	key.n[key.c] = 0;
    hash = hash_cvs_number(key) % NODE_HASH_SIZE;
    for (p = context->table[hash]; p; p = p->hash_next) {
	if (p->number->c != key.c)
	    continue;
	for (i = 0; i < key.c && p->number->n[i] == key.n[i]; i++)
	    ;
	if (i == key.c)
	    return p;
    }
    /*
     * While it looks like a good idea, an attempt at slab allocation
     * failed miserably here.  Noted because tthe regression-test
     * suite didn't catch it.  Attempting to convert groff did.  The
     * problem shows as difficult-to-interpret errors under valgrind.
     */
    p = xcalloc(1, sizeof(node_t), "hash number generation");
    p->number = atom_cvs_number(key);
    p->hash_next = context->table[hash];
    context->table[hash] = p;
    context->nentries++;
    return p;
}

static node_t *find_parent(nodehash_t *context,
			   const cvs_number *const n, const int depth)
/* find the parent node of the specified prefix of a release number */
{
    cvs_number key = *n;
    node_t *p;
    unsigned int hash;
    int i;

    key.c -= depth;
    hash = hash_cvs_number(key) % NODE_HASH_SIZE;
    for (p = context->table[hash]; p; p = p->hash_next) {
	if (p->number->c != key.c)
	    continue;
	for (i = 0; i < key.c && p->number->n[i] == key.n[i]; i++)
	    ;
	if (i == key.c)
	    break;
    }
    return p;
}

void hash_version(nodehash_t *context, cvs_version *v)
/* intern a version onto the node list */
{
    v->node = node_for_cvs_number(context, v->number);
    if (v->node->version) {
	char name[CVS_MAX_REV_LEN];
	announce("more than one delta with number %s\n",
		 cvs_number_string(v->node->number, name, sizeof(name)));
    } else {
	v->node->version = v;
    }
    if (v->node->number->c & 1) {
	char name[CVS_MAX_REV_LEN];
	announce("revision with odd depth(%s)\n",
		 cvs_number_string(v->node->number, name, sizeof(name)));
    }
}

void hash_patch(nodehash_t *context, cvs_patch *p)
/* intern a patch onto the node list */
{
    p->node = node_for_cvs_number(context, p->number);
    if (p->node->patch) {
	char name[CVS_MAX_REV_LEN];
	announce("more than one delta with number %s\n",
		 cvs_number_string(p->node->number, name, sizeof(name)));
    } else {
	p->node->patch = p;
    }
    if (p->node->number->c & 1) {
	char name[CVS_MAX_REV_LEN];
	announce("patch with odd depth(%s)\n",
		 cvs_number_string(p->node->number, name, sizeof(name)));
    }
}

void hash_branch(nodehash_t *context, cvs_branch *b)
/* intern a branch onto the node list */
{
    b->node = node_for_cvs_number(context, b->number);
}

void clean_hash(nodehash_t *context)
/* discard the node list */
{
    int i;
    for (i = 0; i < NODE_HASH_SIZE; i++) {
	node_t *p = context->table[i];
	context->table[i] = NULL;
	while (p) {
	    node_t *q = p->hash_next;
	    free(p);
	    p = q;
	}
    }
    context->nentries = 0;
    context->head_node = NULL;
}

static int compare(const void *a, const void *b)
/* total ordering of nodes by associated CVS revision number */
{
    node_t *x = *(node_t * const *)a, *y = *(node_t * const *)b;
    int n, i;
    n = x->number->c;
    if (n < y->number->c)
	return -1;
    if (n > y->number->c)
	return 1;
    for (i = 0; i < n; i++) {
	if (x->number->n[i] < y->number->n[i])
	    return -1;
	if (x->number->n[i] > y->number->n[i])
	    return 1;
    }
    return 0;
}

static void try_pair(nodehash_t *context, node_t *a, node_t *b)
{
    int n = a->number->c;

    if (n == b->number->c) {
	int i;
	if (n == 2) {
	    a->next = b;
	    b->to = a;
	    return;
	}
	for (i = n - 2; i >= 0; i--)
	    if (a->number->n[i] != b->number->n[i])
		break;
	if (i < 0) {
	    a->next = b;
	    a->to = b;
	    return;
	}
    } else if (n == 2) {
	context->head_node = a;
    }
    if ((b->number->c & 1) == 0) {
	b->starts = true;
	/* can the code below ever be needed? */
	node_t *p = find_parent(context, b->number, 1);
	if (p)
	    p->next = b;
    }
}

/* entry points begin here */

node_t *
cvs_find_version(const cvs_file *cvs, const cvs_number *number)
/* find the file version associated with the specified CVS release number */
{
    cvs_version *cv;
    cvs_version	*nv = NULL;

    for (cv = cvs->gen.versions; cv; cv = cv->next) {
	if (cvs_same_branch(number, cv->number) &&
	    cvs_number_compare(cv->number, number) > 0 &&
	    (!nv || cvs_number_compare(nv->number, cv->number) > 0))
	    nv = cv;
    }
    return nv ? nv->node : NULL;
}

void build_branches(nodehash_t *context)
/* build branch links in the node list */ 
{
    if (context->nentries == 0)
	return;

    node_t **v = xmalloc(sizeof(node_t *) * context->nentries, __func__), **p = v;
    int i;

    for (i = 0; i < NODE_HASH_SIZE; i++) {
	node_t *q;
	for (q = context->table[i]; q; q = q->hash_next)
	    *p++ = q;
    }
    qsort(v, context->nentries, sizeof(node_t *), compare);
    /* only trunk? */
    if (v[context->nentries-1]->number->c == 2)
	context->head_node = v[context->nentries-1];
    for (p = v + context->nentries - 2 ; p >= v; p--)
	try_pair(context, p[0], p[1]);
    for (p = v + context->nentries - 1 ; p >= v; p--) {
	node_t *a = *p, *b = NULL;
	if (!a->starts)
	    continue;
	b = find_parent(context, a->number, 2);
	if (!b) {
	    char name[CVS_MAX_REV_LEN];
	    announce("no parent for %s\n", cvs_number_string(a->number, name, sizeof(name)));
	    continue;
	}
	a->sib = b->down;
	b->down = a;
    }
    free(v);
}

/* end */

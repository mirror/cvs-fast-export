/*
 * The per-CVS-master node list this module builds and exports is used
 * during the analysis phase (only) to walk through all deltas and 
 * build them into snapshots.
 */

#include "cvs.h"
#include "hash.h"

unsigned long
hash_cvs_number(const cvs_number *const key)
{
    return hash_value((const char *)key, sizeof(short) * (key->c + 1));
}

static node_t *
node_for_cvs_number(nodehash_t *context, const cvs_number *const n)
/*
 * look up the node associated with a specified CVS release number
 * only call with a number that has been through atom_cvs_number
 */
{
    const cvs_number *k = n;
    node_t *p;
    hash_t hash = hash_cvs_number(k) % NODE_HASH_SIZE;
    for (p = context->table[hash]; p; p = p->hash_next)
	if (p->number == k)
	    return p;

    /*
     * While it looks like a good idea, an attempt at slab allocation
     * failed miserably here.  Noted because the regression-test
     * suite didn't catch it.  Attempting to convert groff did.  The
     * problem shows as difficult-to-interpret errors under valgrind.
     */
    p = xcalloc(1, sizeof(node_t), "hash number generation");
    p->number = k;
    p->hash_next = context->table[hash];
    context->table[hash] = p;
    context->nentries++;
    return p;
}

static node_t *find_parent(nodehash_t *context,
			   const cvs_number *const n, const int depth)
/* find the parent node of the specified prefix of a release number */
{
    cvs_number key;
    const cvs_number *k;
    node_t *p;
    hash_t hash;

    memcpy(&key, n, sizeof(cvs_number));
    key.c -= depth;
    k = atom_cvs_number(key);
    hash = hash_cvs_number(k) % NODE_HASH_SIZE;
    for (p = context->table[hash]; p; p = p->hash_next)
	if (p->number == k)
	    return p;

    return NULL;
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
	/* can the code below ever be needed?
	 * it's called 90,000 times in netbsd-pkgsrc
         * but no parent is ever found.
	 */
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

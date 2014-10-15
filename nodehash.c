/*
 * The per-CVS-master node list this module builds and exports is used
 * during the analysis phase (only) to walk through all deltas and 
 * build them into snapshots.
 */

#include "cvs.h"

inline static int hash_bucket(const cvs_number key)
{
    int i, hashval;

    for (i = 0, hashval = 0; i < key.c - 1; i++)
	hashval += key.n[i];
    hashval = (hashval * 256 + key.n[key.c - 1]) % NODE_HASH_SIZE;
    return hashval;
}

static node_t *hash_number(nodehash_t *context, const cvs_number *const n)
/* look up the node associated with a specified CVS release number */
{
    cvs_number key = *n;
    node_t *p;
    int hash;
    int i;

    if (key.c > 2 && !key.n[key.c - 2]) {
	key.n[key.c - 2] = key.n[key.c - 1];
	key.c--;
    }
    if (key.c & 1)
	key.n[key.c] = 0;
    hash = hash_bucket(key);
    for (p = context->table[hash]; p; p = p->hash_next) {
	if (p->number.c != key.c)
	    continue;
	for (i = 0; i < key.c && p->number.n[i] == key.n[i]; i++)
	    ;
	if (i == key.c)
	    return p;
    }
    p = xcalloc(1, sizeof(node_t), "hash number generation");
    p->number = key;
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
    int hash;
    int i;

    key.c -= depth;
    hash = hash_bucket(key);
    for (p = context->table[hash]; p; p = p->hash_next) {
	if (p->number.c != key.c)
	    continue;
	for (i = 0; i < key.c && p->number.n[i] == key.n[i]; i++)
	    ;
	if (i == key.c)
	    break;
    }
    return p;
}

void hash_version(nodehash_t *context, cvs_version *v)
/* intern a version onto the node list */
{
    v->node = hash_number(context, &v->number);
    if (v->node->version) {
	char name[CVS_MAX_REV_LEN];
	announce("more than one delta with number %s\n",
		 cvs_number_string(&v->node->number, name, sizeof(name)));
    } else {
	v->node->version = v;
    }
    if (v->node->number.c & 1) {
	char name[CVS_MAX_REV_LEN];
	announce("revision with odd depth(%s)\n",
		 cvs_number_string(&v->node->number, name, sizeof(name)));
    }
}

void hash_patch(nodehash_t *context, cvs_patch *p)
/* intern a patch onto the node list */
{
    p->node = hash_number(context, &p->number);
    if (p->node->patch) {
	char name[CVS_MAX_REV_LEN];
	announce("more than one delta with number %s\n",
		 cvs_number_string(&p->node->number, name, sizeof(name)));
    } else {
	p->node->patch = p;
    }
    if (p->node->number.c & 1) {
	char name[CVS_MAX_REV_LEN];
	announce("patch with odd depth(%s)\n",
		 cvs_number_string(&p->node->number, name, sizeof(name)));
    }
}

void hash_branch(nodehash_t *context, cvs_branch *b)
/* intern a branch onto the node list */
{
    b->node = hash_number(context, &b->number);
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
    n = x->number.c;
    if (n < y->number.c)
	return -1;
    if (n > y->number.c)
	return 1;
    for (i = 0; i < n; i++) {
	if (x->number.n[i] < y->number.n[i])
	    return -1;
	if (x->number.n[i] > y->number.n[i])
	    return 1;
    }
    return 0;
}

static void try_pair(nodehash_t *context, node_t *a, node_t *b)
{
    int n = a->number.c;

    if (n == b->number.c) {
	int i;
	if (n == 2) {
	    a->next = b;
	    b->to = a;
	    return;
	}
	for (i = n - 2; i >= 0; i--)
	    if (a->number.n[i] != b->number.n[i])
		break;
	if (i < 0) {
	    a->next = b;
	    a->to = b;
	    return;
	}
    } else if (n == 2) {
	context->head_node = a;
    }
    if ((b->number.c & 1) == 0) {
	b->starts = true;
	/* can the code below ever be needed? */
	node_t *p = find_parent(context, &b->number, 1);
	if (p)
	    p->next = b;
    }
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
    if (v[context->nentries-1]->number.c == 2)
	context->head_node = v[context->nentries-1];
    for (p = v + context->nentries - 2 ; p >= v; p--)
	try_pair(context, p[0], p[1]);
    for (p = v + context->nentries - 1 ; p >= v; p--) {
	node_t *a = *p, *b = NULL;
	if (!a->starts)
	    continue;
	b = find_parent(context, &a->number, 2);
	if (!b) {
	    char name[CVS_MAX_REV_LEN];
	    announce("no parent for %s\n", cvs_number_string(&a->number, name, sizeof(name)));
	    continue;
	}
	a->sib = b->down;
	b->down = a;
    }
    free(v);
}

/* end */

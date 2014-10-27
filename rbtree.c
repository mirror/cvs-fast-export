#include "cvs.h"
#include "rbtree.h"

/*
 * Structures for the red/black tree
 */

typedef enum _rbtree_color {
  RED = 0,
  BLACK = 1,
} rbtree_color;

typedef struct rbtree_node {
    const void		*key;
    void		*value;
    struct rbtree_node	*restrict parent;
    struct rbtree_node	*restrict left;
    struct rbtree_node	*restrict right;
    rbtree_color	color;
} rbtree_node;

static struct rbtree_node *
rbtree_parent(const struct rbtree_node *node)
/* This function should be called if existence of the parent is
   guaranteed within or required by the calling function */
{
    assert(node->parent);
    return node->parent;
}

static bool
rbtree_is_left_child(const struct rbtree_node *node, const struct rbtree_node *parent)
{
    assert(rbtree_parent(node) == parent);
    if (parent && parent->left == node)
	return true;
    return false;
}

static bool
rbtree_is_right_child(const struct rbtree_node *node, const struct rbtree_node *parent)
{
    assert(rbtree_parent(node) == parent);
    if (parent && parent->right == node)
	return true;
    return false;
}

static struct rbtree_node *
rbtree_sibling(const struct rbtree_node *node)
{
    struct rbtree_node *parent;
    parent = rbtree_parent(node);
    if (rbtree_is_left_child(node, parent))
	return parent->right;
    else
	return parent->left;
}

static rbtree_color
rbtree_node_color(const struct rbtree_node *node)
/* we use NULL as sentinel */
{
    if (node)
	return node->color;
    return BLACK;
}

#if RBDEBUG
static void
rbtree_assert_links(const struct rbtree_node *node)
/* not a red-black property, but make sure parent <-> child links are
 * correct at all times */
{
    if (node) {
	if (node->left)
	    assert(rbtree_is_left_child(node->left, node));
	if (node->right)
	    assert(rbtree_is_right_child(node->right, node));
	rbtree_assert_links(node->left);
	rbtree_assert_links(node->right);
    }
}

static void
rbtree_assert_property_2(const struct rbtree_node *root)
/* 2. red-black property: the root is black */
{
    assert(struct rbtree_node_color(root) == BLACK);
}

static void
rbtree_assert_property_4(const struct rbtree_node *node)
/* 4. red-black property: If a node is red, both its children are black */
{
    if (node) {
	if (struct rbtree_node_color(node) == RED) {
	    assert(struct rbtree_node_color(node->left) == BLACK);
	    assert(struct rbtree_node_color(node->right) == BLACK);
	}
	rbtree_assert_property_4 (node->left);
	rbtree_assert_property_4 (node->right);
    }
}

static void
rbtree_assert_property_5_helper(const struct rbtree_node *node,
			     const int current_count,
			     int *first_leaf_count)
{
    if (struct rbtree_node_color(node) == BLACK)
	++current_count;

    if (node) {
	rbtree_assert_property_5_helper(node->left, current_count, first_leaf_count);
	rbtree_assert_property_5_helper(node->right, current_count, first_leaf_count);
    }
    else {
	if (*first_leaf_count != -1)
	    assert(current_count == *first_leaf_count);
	else
	    *first_leaf_count = current_count;
    }
}

static void
rbtree_assert_property_5(const struct rbtree_node *node)
/* 5. red-black property: For each node, all paths to its leaf nodes
   contain the same number of black nodes */
{
    int first_leaf_count = -1;
    rbtree_assert_property_5_helper(node, 0, &first_leaf_count);
}


static void
rbtree_assert_properties(const struct rbtree_node *root)
{
    rbtree_assert_links(root);
    /* property 1 is implicit: every node is either red or black */
    rbtree_assert_property_2 (root);
    /* property 3 is implicit: every leaf is black, see struct rbtree_node_color */
    rbtree_assert_property_4 (root);
    rbtree_assert_property_5 (root);
}
#endif /* #if RBDEBUG */

static void
rbtree_rotate_helper(struct rbtree_node *x, struct rbtree_node *y)
/* change the parent <-> child links for all nodes involved in a
   binary tree rotation */
{
    assert(x);
    assert(y);
    assert(y->parent == x);

    struct rbtree_node *p, *b;
    p = x->parent;

    if (rbtree_is_left_child(y, x)) {
	b = y->right;
	x->left = b;
	y->right = x;
    }
    else {
	b = y->left;
	x->right = b;
	y->left = x;
    }

    if (p) {
	if (rbtree_is_left_child(x, p))
	    p->left = y;
	else
	    p->right = y;
    }

    x->parent = y;
    y->parent = p;
    if (b) {
	b->parent = x;
    }
}

static void
rbtree_rotate_left(struct rbtree_node *x)
{
    rbtree_rotate_helper(x, x->right);
}

static void
rbtree_rotate_right(struct rbtree_node *x)
{
    rbtree_rotate_helper(x, x->left);
}

static void
rbtree_insert_fixup(struct rbtree_node **root, struct rbtree_node *z)
/* this one is pretty much copied from the pseudo code in
   Cormen/Leisterson/Rivest/Stein */
{
    if (*root != z) {
	while (rbtree_node_color(z->parent) == RED) {
	    struct rbtree_node *p, *g, *y;
	    p = rbtree_parent(z);
	    g = rbtree_parent(p);
	    y = rbtree_sibling(p);
	    if (rbtree_node_color(y) == RED) {
		g->color = RED;
		p->color = BLACK;
		y->color = BLACK;
		z = g;
	    }
	    else if (rbtree_is_left_child(p, g)) {
		if (rbtree_is_right_child(z, p)) {
		    z = p;
		    rbtree_rotate_left(z);
		    p = rbtree_parent(z);
		    g = rbtree_parent(p);
		}
		p->color = BLACK;
		g->color = RED;
		rbtree_rotate_right(g);
	    }
	    else /* rbtree_is_right_child(p, g) */ {
		if (rbtree_is_left_child(z, p)) {
		    z = p;
		    rbtree_rotate_right(z);
		    p = rbtree_parent(z);
		    g = rbtree_parent(p);
		}
		p->color = BLACK;
		g->color = RED;
		rbtree_rotate_left(g);
	    }
	}
	/* we might have a new root node after a rotation, so update
	   the place where it is stored */
	struct rbtree_node *r;
	r = *root;
	while (r->parent)
	    r = r->parent;
	*root = r;
    }
    (*root)->color = BLACK;
}

void
rbtree_insert(struct rbtree_node **root, const void *key, void *value,
              int(*compare)(const void *key1, const void *key2))
{
    struct rbtree_node *parent, *node, **nodep;

    parent = NULL;
    nodep = root;
    while ((node = *nodep)) {
	parent = node;
	if (compare(node->key, key) < 0)
	    nodep = (struct rbtree_node **)&node->left;
	else if (compare(node->key, key) > 0)
	    nodep = (struct rbtree_node **)&node->right;
	else
	    fatal_error("internal error - duplicate key in red black tree");
    }
    node = xcalloc(1, sizeof(struct rbtree_node), "red black tree insertion");
    node->key = key;
    node->value = value;
    node->parent = parent;
    assert(node->color == RED);
    assert(node->left == NULL);
    assert(node->right == NULL);
    *nodep = node;

    rbtree_insert_fixup(root, node);
#if RBDEBUG
    rbtree_assert_properties(*root);
#endif /* RBDEBUG */
}

struct rbtree_node*
rbtree_lookup(struct rbtree_node *root, const void *key,
              int(*compare)(const void *key1, const void *key2))
{
    struct rbtree_node *node;
    node = root;
    while (node && compare(node->key, key) != 0) {
	if (compare(node->key, key) < 0)
	    node = node->left;
	else
	    node = node->right;
    }
    return node;
}

void *rbtree_value(struct rbtree_node *n)
{
    return (void *)n->value;
}


void
rbtree_free(struct rbtree_node *node)
{
    if (node) {
	rbtree_free(node->left);
	rbtree_free(node->right);
	free(node);
    }
}

/* Local Variables:    */
/* mode: c             */
/* c-basic-offset: 4   */
/* indent-tabs-mode: t */
/* End:                */

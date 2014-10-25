/* rbtree.h - prototypes fot red/black tree lookup code */

struct rbtree_node;

void
rbtree_insert(struct rbtree_node **root, const void *key, void *value,
              int (*compare)(const void* key1, const void* key2));

struct rbtree_node*
rbtree_lookup(struct rbtree_node *root, const void* key,
              int (*compare)(const void* key1, const void* key2));

void *rbtree_value(struct rbtree_node *n);

void
rbtree_free(struct rbtree_node *root);

/* end */

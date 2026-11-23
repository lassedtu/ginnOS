/**
 * @file search.c
 * @brief search trees (an AVL tree: the heights of the two sides of each
 *        node differ by one at most, so a search takes log n steps) and
 *        linear search.
 *
 * a node starts with its key pointer, so a pointer to the node is also a
 * pointer to the key pointer, which is what tsearch and tfind return.
 */

#include "search.h"

#include "stdlib.h"
#include "string.h"

typedef struct node
{
    const void *key; // first: see above
    struct node *left;
    struct node *right;
    int height; // 1 for a leaf
} node_t;

typedef int (*compare_t)(const void *, const void *);

static int height(const node_t *n)
{
    return n ? n->height : 0;
}

static void update(node_t *n)
{
    int l = height(n->left);
    int r = height(n->right);
    n->height = 1 + (l > r ? l : r);
}

static node_t *rotate_right(node_t *n)
{
    node_t *l = n->left;
    n->left = l->right;
    l->right = n;
    update(n);
    update(l);
    return l;
}

static node_t *rotate_left(node_t *n)
{
    node_t *r = n->right;
    n->right = r->left;
    r->left = n;
    update(n);
    update(r);
    return r;
}

/** make n balanced again after one side changed by one */
static node_t *balance(node_t *n)
{
    update(n);
    int diff = height(n->left) - height(n->right);
    if (diff > 1)
    {
        if (height(n->left->left) < height(n->left->right))
            n->left = rotate_left(n->left);
        return rotate_right(n);
    }
    if (diff < -1)
    {
        if (height(n->right->right) < height(n->right->left))
            n->right = rotate_right(n->right);
        return rotate_left(n);
    }
    return n;
}

/**
 * add key below n (or find it).
 * @param found gets the node of the key; NULL when there is no memory.
 * @return the new top of this part of the tree.
 */
static node_t *insert(node_t *n, const void *key, compare_t compare, node_t **found)
{
    if (!n)
    {
        n = malloc(sizeof(*n));
        *found = n;
        if (!n)
            return NULL;
        n->key = key;
        n->left = NULL;
        n->right = NULL;
        n->height = 1;
        return n;
    }
    int c = compare(key, n->key);
    if (c == 0)
    {
        *found = n;
        return n;
    }
    if (c < 0)
    {
        node_t *l = insert(n->left, key, compare, found);
        if (!*found)
            return n;
        n->left = l;
    }
    else
    {
        node_t *r = insert(n->right, key, compare, found);
        if (!*found)
            return n;
        n->right = r;
    }
    return balance(n);
}

void *tsearch(const void *key, void **root, compare_t compare)
{
    if (!root)
        return NULL;
    node_t *found = NULL;
    node_t *top = insert(*root, key, compare, &found);
    if (found)
        *root = top;
    return found;
}

void *tfind(const void *key, void *const *root, compare_t compare)
{
    if (!root)
        return NULL;
    node_t *n = *root;
    while (n)
    {
        int c = compare(key, n->key);
        if (c == 0)
            return n;
        n = c < 0 ? n->left : n->right;
    }
    return NULL;
}

/** take the smallest node out from below n: into *smallest */
static node_t *take_smallest(node_t *n, node_t **smallest)
{
    if (!n->left)
    {
        *smallest = n;
        return n->right;
    }
    n->left = take_smallest(n->left, smallest);
    return balance(n);
}

/** remove key from below n; *removed is set when it was there */
static node_t *remove_key(node_t *n, const void *key, compare_t compare, int *removed)
{
    if (!n)
        return NULL;
    int c = compare(key, n->key);
    if (c < 0)
        n->left = remove_key(n->left, key, compare, removed);
    else if (c > 0)
        n->right = remove_key(n->right, key, compare, removed);
    else
    {
        *removed = 1;
        node_t *l = n->left;
        node_t *r = n->right;
        free(n);
        if (!r)
            return l;
        // the smallest node on the right takes this node's place
        node_t *next;
        node_t *rest = take_smallest(r, &next);
        next->left = l;
        next->right = rest;
        return balance(next);
    }
    return balance(n);
}

void *tdelete(const void *restrict key, void **restrict root, compare_t compare)
{
    if (!root || !*root)
        return NULL;
    // the parent, found before the tree changes (the node itself stays)
    static node_t no_parent;
    node_t *parent = &no_parent;
    for (node_t *n = *root; n;)
    {
        int c = compare(key, n->key);
        if (c == 0)
            break;
        parent = n;
        n = c < 0 ? n->left : n->right;
    }
    int removed = 0;
    *root = remove_key(*root, key, compare, &removed);
    return removed ? parent : NULL;
}

static void walk(const node_t *n, void (*action)(const void *, VISIT, int), int depth)
{
    if (!n->left && !n->right)
    {
        action(n, leaf, depth);
        return;
    }
    action(n, preorder, depth);
    if (n->left)
        walk(n->left, action, depth + 1);
    action(n, postorder, depth);
    if (n->right)
        walk(n->right, action, depth + 1);
    action(n, endorder, depth);
}

void twalk(const void *root, void (*action)(const void *node, VISIT which, int depth))
{
    if (root && action)
        walk(root, action, 0);
}

void tdestroy(void *root, void (*free_key)(void *key))
{
    node_t *n = root;
    if (!n)
        return;
    tdestroy(n->left, free_key);
    tdestroy(n->right, free_key);
    if (free_key)
        free_key((void *)n->key);
    free(n);
}

void *lfind(const void *key, const void *base, size_t *count, size_t size, compare_t compare)
{
    const char *p = base;
    for (size_t i = 0; i < *count; i++, p += size)
        if (compare(key, p) == 0)
            return (void *)p;
    return NULL;
}

void *lsearch(const void *key, void *base, size_t *count, size_t size, compare_t compare)
{
    void *found = lfind(key, base, count, size, compare);
    if (found)
        return found;
    char *end = (char *)base + *count * size;
    memcpy(end, key, size);
    (*count)++;
    return end;
}

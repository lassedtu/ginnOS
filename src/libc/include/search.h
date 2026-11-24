#pragma once

/**
 * @file search.h
 * @brief search trees (tsearch: a balanced tree of pointers the caller
 *        owns) and linear search in an array.
 */

#include <stddef.h>

typedef enum
{
    preorder,  // a node is seen before its children
    postorder, // between its children
    endorder,  // after its children
    leaf       // a node with no children (seen once)
} VISIT;

typedef int (*__compar_fn_t)(const void *, const void *);

/**
 * find key in the tree at *root, or add it.
 * @return a pointer to the node's key pointer (the found one, or key), or
 *         NULL when there is no memory.
 */
void *tsearch(const void *key, void **root, int (*compare)(const void *, const void *));
/** find key; NULL when it is not in the tree */
void *tfind(const void *key, void *const *root, int (*compare)(const void *, const void *));
/**
 * remove key from the tree.
 * @return a pointer to the parent's key pointer (any non-NULL pointer when
 *         the root was removed), or NULL when key is not in the tree.
 */
void *tdelete(const void *restrict key, void **restrict root,
              int (*compare)(const void *, const void *));
/** call action for each node, in the order of the tree, with its depth */
void twalk(const void *root, void (*action)(const void *node, VISIT which, int depth));
/** free every node, calling free_key with each key (GNU) */
void tdestroy(void *root, void (*free_key)(void *key));

/** find key in the array of *count items of size bytes; NULL if it is not there */
void *lfind(const void *key, const void *base, size_t *count, size_t size,
            int (*compare)(const void *, const void *));
/** as lfind, but add key at the end (and count it) when it is not there */
void *lsearch(const void *key, void *base, size_t *count, size_t size,
              int (*compare)(const void *, const void *));

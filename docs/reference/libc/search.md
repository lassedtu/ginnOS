<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;search.h&gt;

Source: [`src/libc/include/search.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/search.h)

search trees (tsearch: a balanced tree of pointers the caller
       owns) and linear search in an array.

## compare

```c
void *tsearch(const void *key, void **root, int (*compare)(const void *, const void *));
```

find key in the tree at \*root, or add it.

Returns: a pointer to the node's key pointer (the found one, or key), or NULL when there is no memory.

## compare (2)

```c
void *tfind(const void *key, void *const *root, int (*compare)(const void *, const void *));
```

find key; NULL when it is not in the tree

## compare (3)

```c
void *tdelete(const void *restrict key, void **restrict root,
              int (*compare)(const void *, const void *));
```

remove key from the tree.

Returns: a pointer to the parent's key pointer (any non-NULL pointer when the root was removed), or NULL when key is not in the tree.

## action

```c
void twalk(const void *root, void (*action)(const void *node, VISIT which, int depth));
```

call action for each node, in the order of the tree, with its depth

## free\_key

```c
void tdestroy(void *root, void (*free_key)(void *key));
```

free every node, calling free\_key with each key (GNU)

## compare (4)

```c
void *lfind(const void *key, const void *base, size_t *count, size_t size,
            int (*compare)(const void *, const void *));
```

find key in the array of \*count items of size bytes; NULL if it is not there

## compare (5)

```c
void *lsearch(const void *key, void *base, size_t *count, size_t size,
              int (*compare)(const void *, const void *));
```

as lfind, but add key at the end (and count it) when it is not there

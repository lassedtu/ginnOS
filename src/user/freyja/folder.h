#pragma once

/**
 * @file folder.h
 * @brief Freyja: the pictures in the folder of the open one, by name, for
 *        the next and the one before. plain C (host tested).
 */

#include <stdbool.h>
#include <stddef.h>

typedef struct
{
    char *folder;  // with no / at its end ("" for /)
    char **names;  // sorted, the pictures only
    size_t count;
    size_t current; // the open picture's place in names
} folder_t;

/** true when a file name ends as a picture Freyja opens (.png, .jpg ...) */
bool folder_is_picture(const char *name);

/** read the folder of path; false when it cannot be read (or no memory) */
bool folder_read(folder_t *f, const char *path);
void folder_free(folder_t *f);

/**
 * the path of the picture step places on (+1 the next, -1 the one
 * before), round at the ends; it becomes the current one.
 * @return a new string (free it), or NULL when there is no other.
 */
char *folder_step(folder_t *f, int step);

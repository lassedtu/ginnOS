#pragma once

/**
 * @file fileops.h
 * @brief yggdrasil: copy, move and delete folder trees, and the trash.
 *
 * plain POSIX calls only, so the host tests run them in a temporary
 * folder. the trash has the freedesktop layout: <trash>/files holds the
 * things, <trash>/info/<name>.trashinfo where each came from and when.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/** what an operation tells while it works (each hook can be NULL) */
typedef struct
{
    void (*file)(const char *path, void *data);  // it starts on this file
    void (*bytes)(uint64_t count, void *data);   // count more bytes are done
    void (*error)(const char *path, int error, void *data);
    void *data;
    volatile int *stop; // set (by a signal): stop soon with ECANCELED, no half file stays
} fileops_report_t;

/** the bytes of a file or of a folder tree; files counts the files */
uint64_t fileops_size(const char *path, int *files);

/**
 * copy a file or a folder tree to a path that is not there yet. it goes
 * on after an error and reports each one.
 * @return 0, or the first error (an errno value).
 */
int fileops_copy(const char *from, const char *to, const fileops_report_t *report);

/** delete a file or a folder tree; 0 or the first error */
int fileops_remove(const char *path, const fileops_report_t *report);

/**
 * move: a rename, or on another disk a copy and then a delete.
 * @return 0, or the first error.
 */
int fileops_move(const char *from, const char *to, const fileops_report_t *report);

/**
 * a path for name in dir that is not there yet: dir/name, else
 * "name (2).ext", "name (3).ext" and so on.
 */
void fileops_unique(const char *dir, const char *name, char *out, size_t length);

/** true when path is dir or a path inside it */
bool fileops_inside(const char *path, const char *dir);

/** make a folder and the folders above it */
int fileops_mkdirs(const char *path);

/**
 * move path into the trash, with its info file.
 * @return 0, or an errno value (EXDEV: on another disk than the trash).
 */
int trash_put(const char *trash, const char *path, time_t now);

/**
 * put a thing of the trash back where it was (another name when that
 * place is taken now).
 * @param name its name in <trash>/files.
 * @param restored receives the path it went to.
 */
int trash_restore(const char *trash, const char *name, char *restored, size_t length);

/** delete all in the trash */
int trash_empty(const char *trash);

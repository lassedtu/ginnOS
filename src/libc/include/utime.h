#pragma once

#include <sys/types.h>

/**
 * @file utime.h
 * @brief utime: set a file's times in whole seconds (the older call; see
 *        utimensat in <sys/stat.h>).
 */

struct utimbuf
{
    time_t actime;  // last access
    time_t modtime; // last change of the data
};

/**
 * set a file's access and modification times; with times NULL, both to
 * the time now.
 * @return 0, or -1 (errno set).
 */
int utime(const char *path, const struct utimbuf *times);

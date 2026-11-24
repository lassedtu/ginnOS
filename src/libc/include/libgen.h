#pragma once

/**
 * @file libgen.h
 * @brief the parts of a path. both may change path, and both may return
 *        a static string (".", "/").
 */

/** the last part: "/usr/lib/" gives "lib" */
char *basename(char *path);
/** all but the last part: "/usr/lib/" gives "/usr", "file" gives "." */
char *dirname(char *path);

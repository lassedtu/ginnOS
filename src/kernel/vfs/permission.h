#pragma once

/**
 * file permissions: who may read, write and run a file.
 *
 * the effective user id picks the class: the owner's bits if it owns the
 * file, the group's bits if the effective group id is the file's group,
 * else the bits for others. root (effective uid 0) may read and write
 * anything, and run anything with at least one execute bit (or a
 * directory: search it).
 *
 * path permissions are checked on the file itself and its parent
 * directory; directories higher up are not searched for execute rights.
 */

#include "common/stdint.h"
#include "kernel/fs/fs.h"

struct process;

#define MAY_EXEC  1
#define MAY_WRITE 2
#define MAY_READ  4

#define S_ISUID 04000 // run with the owner's user id
#define S_ISGID 02000 // run with the file's group id
#define S_ISVTX 01000 // sticky: in a directory, only owners remove entries

/**
 * check an access against a file's mode and owner.
 * @param st the file's stat.
 * @param uid the user id to check as (effective, or real for access()).
 * @param gid the group id to check as.
 * @param want MAY_* bits.
 * @return 0, or -EACCES.
 */
int32_t permission_check(const fs_stat_t *st, uint32_t uid, uint32_t gid, uint32_t want);

/**
 * check an access to a path for the current process (effective ids).
 * @return 0, -EACCES, or the error of finding the file (-ENOENT).
 */
int32_t permission_check_path(const char *path, uint32_t want);

/**
 * check that the current process may add or remove an entry of a
 * directory: write and search rights on the parent of path. for a removal
 * (victim true) in a sticky directory, it must also own the entry or the
 * directory.
 * @return 0, -EACCES, -EPERM, or the error of finding the parent.
 */
int32_t permission_check_parent(const char *path, bool victim);

/**
 * the parent directory of an absolute path ("/a/b" -> "/a", "/a" -> "/").
 * @param path the path.
 * @param out receives the parent.
 * @param size size of out.
 */
void permission_parent_path(const char *path, char *out, uint32_t size);

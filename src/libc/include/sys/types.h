#pragma once

/**
 * @file sys/types.h
 * @brief basic system types. the other headers take their types from here,
 * so each type has one definition.
 *
 * the sizes that the kernel ABI fixes (struct stat, struct timespec) are the
 * same on i686 and x86_64: the ids, modes and file offsets are 32 bits;
 * time_t and clock_t are long (the width of the kernel's long).
 */

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ssize_t;
typedef int pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned int mode_t;
typedef int off_t;
typedef unsigned int ino_t;
typedef unsigned int dev_t;
typedef unsigned int nlink_t;
typedef unsigned int blksize_t;
typedef unsigned int blkcnt_t;
typedef long time_t;
typedef unsigned int id_t; // a pid, group or user id (getpriority)
typedef long clock_t;

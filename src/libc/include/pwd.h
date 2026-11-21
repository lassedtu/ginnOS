#pragma once

#include <sys/types.h>

/**
 * @file pwd.h
 * @brief the user database (/etc/passwd: name:password:uid:gid:gecos:home:shell).
 */

struct passwd
{
    char *pw_name;
    char *pw_passwd;
    uid_t pw_uid;
    gid_t pw_gid;
    char *pw_gecos;
    char *pw_dir;
    char *pw_shell;
};

/**
 * look up a user by id or name. the result is overwritten by the next call.
 * @return the entry, or NULL if there is none.
 */
struct passwd *getpwuid(uid_t uid);
struct passwd *getpwnam(const char *name);

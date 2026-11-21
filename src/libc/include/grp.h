#pragma once

#include <sys/types.h>

/**
 * @file grp.h
 * @brief the group database (/etc/group: name:password:gid:members).
 */

struct group
{
    char *gr_name;
    char *gr_passwd;
    gid_t gr_gid;
    char *gr_mem_list; // the members, comma separated (no gr_mem array yet)
};

/**
 * look up a group by id or name. the result is overwritten by the next call.
 * @return the entry, or NULL if there is none.
 */
struct group *getgrgid(gid_t gid);
struct group *getgrnam(const char *name);

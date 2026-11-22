#pragma once

/**
 * @file utsname.h
 * @brief the name of the system: uname.
 */

#define _UTSNAME_LENGTH 65

struct utsname
{
    char sysname[_UTSNAME_LENGTH];  // "ginnOS"
    char nodename[_UTSNAME_LENGTH]; // the host name
    char release[_UTSNAME_LENGTH];  // the release, "0.1"
    char version[_UTSNAME_LENGTH];  // the commit and its date
    char machine[_UTSNAME_LENGTH];  // "x86_64" or "i686"
};

/**
 * fill name with the system's names.
 * @return 0, or -1 with errno set.
 */
int uname(struct utsname *name);

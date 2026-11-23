#pragma once

/**
 * @file shadow.h
 * @brief the password hashes (/etc/shadow: name:hash:last:min:max:warn:
 *        inactive:expire:), which only root can read.
 *
 * ginnOS uses the name and the hash; the other fields are kept as they
 * are. a hash of "*" or "!" (or one that starts with "!") is a locked
 * account; an empty one needs no password.
 */

#define SHADOW_FILE "/etc/shadow"

struct spwd
{
    char *sp_namp; // the user's name
    char *sp_pwdp; // the hash ("$5$...")
};

/**
 * the entry of a user, in a static buffer.
 * @return NULL if there is none, or the file cannot be read (not root).
 */
struct spwd *getspnam(const char *name);

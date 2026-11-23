#pragma once

/**
 * @file bifrost.h
 * @brief Bifrost, the login screen: the parts its files share. (Bifrost
 *        is the bridge into Asgard.)
 *
 * the files:
 *   main.c     the loop: the login screen, then the session, then again
 *   backdrop.c the wallpaper, blurred and darkened, under the login screen
 *   screen.c   the login screen: the time, the users, the password
 *   session.c  the desktop as the user, and what is left after it
 *   users.c    the users who can log in (/etc/passwd)
 */

#include <gfx.h>
#include <stdbool.h>
#include <sys/types.h>

#define USERS_MAX 16
#define USER_NAME_MAX 32

typedef struct
{
    char name[USER_NAME_MAX];
    char full_name[64]; // the GECOS field, or the name
    uid_t uid;
} login_user_t;

// users.c
/** the users who can log in: root and the uids from 1000, not nologin */
int users_read(login_user_t *out, int max);

// backdrop.c
/** the wallpaper at this size, blurred and darkened (NULL: no memory) */
gfx_buffer_t *backdrop_make(int32_t width, int32_t height);

// screen.c
/**
 * show the login screen until a user logs in.
 * @param last the name to choose at first, or "".
 * @return false if there is no window server.
 */
bool screen_run(const char *last, char *name_out, size_t length);

// session.c
/** run the desktop as the user and wait until it ends; then end what is left */
void session_run(const char *name);

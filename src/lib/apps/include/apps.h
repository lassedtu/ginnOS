#pragma once

/**
 * @file apps.h
 * @brief the programs of the desktop: the .app files.
 *
 * each program the launcher shows has a file /usr/share/applications/
 * <id>.app in the settings format of libconf:
 *
 *     [app]
 *     name = Window demo
 *     comment = A test of the window server
 *     exec = heimdemo 2
 *     icon = application-x-executable
 *     category = Development
 *
 * name and exec must be there. icon is a Tela icon name (the icon library
 * gives "unknown" for a name it does not have). category is one of
 * APP_CATEGORIES; another or no category is "Other". hidden = true keeps
 * the program out of the launcher (it can still be found by its id).
 *
 * exec is split into words at spaces. a word in double quotes can have
 * spaces in it. the first word is the program: a path, or a name in /bin.
 */

#include <stdbool.h>

#define APPS_DIR "/usr/share/applications"

// the categories, in the order the launcher shows them
#define APP_CATEGORIES "Accessories", "Development", "Games", "Graphics", "Sound", "System", "Other"

#define APP_ARGS_MAX 16

typedef struct
{
    char *id;       // the file name without .app
    char *name;     // "Window demo"
    char *comment;  // may be ""
    char *exec;     // "heimdemo 2"
    char *icon;     // "application-x-executable", may be ""
    char *category; // one of APP_CATEGORIES
    bool hidden;
} app_t;

typedef struct
{
    app_t *apps; // sorted by name (letter case is not looked at)
    int count;
} app_list_t;

/**
 * read the .app files of a directory (APPS_DIR for the system ones). a
 * file without a name or exec is skipped, with a warning on stderr.
 * @return the list (empty if there is no directory), or NULL (no memory).
 */
app_list_t *apps_load(const char *dir);

void apps_free(app_list_t *list);

/**
 * the program with this id.
 * @return it, or NULL.
 */
const app_t *apps_find(const app_list_t *list, const char *id);

/**
 * split exec into words.
 * @param buffer receives the words (it must hold strlen(exec) + 1 bytes).
 * @param argv receives up to APP_ARGS_MAX words and a NULL.
 * @return the number of words, or -1 (none, too many, or a quote that is
 *         not closed).
 */
int app_split_exec(const char *exec, char *buffer, char *argv[APP_ARGS_MAX + 1]);

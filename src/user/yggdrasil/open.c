/**
 * @file open.c
 * @brief yggdrasil: opening a file with the program for its type.
 *
 * the programs are in open-with.conf ([open] type = command, %f is the
 * file, "text/" and a star is all text types): /etc/ginnos/open-with.conf, and the
 * user's own in ~/.config/ginnos/open-with.conf wins. a program file
 * runs: a desktop program (one with a .app file) on its own, any other
 * in mimir, which stays open to show what it wrote.
 */

#include "yggdrasil.h"

#include <apps.h>
#include <conf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ARGS_MAX 16

bool spawn(char *const argv[], const char *folder)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        if (folder && chdir(folder) != 0)
            chdir("/");
        setsid(); // not in the file manager's group: it lives on after it
        execvp(argv[0], argv);
        _exit(127);
    }
    return pid > 0;
}

void open_in_mimir(const char *folder, const char *program)
{
    char *with_program[] = {"mimir", "-k", "-d", (char *)folder, (char *)program, NULL};
    char *shell[] = {"mimir", "-d", (char *)folder, NULL};
    if (!spawn(program ? with_program : shell, folder))
        show_message("Cannot start Mimir", "There are too many programs.");
}

/** true when a .app file starts this program (it has a window of its own) */
static bool desktop_program(const char *path)
{
    app_list_t *apps = apps_load(APPS_DIR);
    if (!apps)
        return false;
    const char *name = path_name(path);
    bool found = false;
    for (int i = 0; i < apps->count && !found; i++)
    {
        char buffer[512];
        char *argv[APP_ARGS_MAX + 1];
        if (strlen(apps->apps[i].exec) >= sizeof(buffer))
            continue;
        if (app_split_exec(apps->apps[i].exec, buffer, argv) > 0)
            found = strcmp(path_name(argv[0]), name) == 0;
    }
    apps_free(apps);
    return found;
}

/** the command for a type: the type, then its kind with a star, then a star */
static bool command_for(const char *type, char *out, size_t length)
{
    conf_t *conf = conf_open("open-with");
    if (!conf)
        return false;
    const char *command = conf_get(conf, "open", type, NULL);
    char kind[96];
    const char *slash = strchr(type, '/');
    if (!command && slash)
    {
        snprintf(kind, sizeof(kind), "%.*s/*", (int)(slash - type), type);
        command = conf_get(conf, "open", kind, NULL);
    }
    if (!command)
        command = conf_get(conf, "open", "*", NULL);
    if (command)
        snprintf(out, length, "%s", command);
    conf_free(conf);
    return command != NULL;
}

/** run a command of open-with.conf with %f as path */
static void run_command(const char *command, const char *path)
{
    char buffer[512];
    char *words[APP_ARGS_MAX + 1];
    if (strlen(command) >= sizeof(buffer))
        return;
    int count = app_split_exec(command, buffer, words);
    if (count <= 0)
        return;
    char *argv[APP_ARGS_MAX + 2];
    int n = 0;
    bool has_file = false;
    for (int i = 0; i < count && n < APP_ARGS_MAX; i++)
    {
        if (strcmp(words[i], "%f") == 0)
        {
            argv[n++] = (char *)path;
            has_file = true;
        }
        else
            argv[n++] = words[i];
    }
    if (!has_file)
        argv[n++] = (char *)path;
    argv[n] = NULL;
    if (!spawn(argv, Y.folder.path))
        show_message("Cannot start the program", command);
}

void open_entry(const entry_t *e)
{
    char path[PATH_LEN];
    path_join(Y.folder.path, e->name, path, sizeof(path));
    if (S_ISDIR(e->mode))
    {
        navigate(path, true);
        return;
    }
    if (strcmp(e->type, "application/x-executable") == 0)
    {
        if (desktop_program(path))
        {
            char *argv[] = {path, NULL};
            spawn(argv, Y.folder.path);
        }
        else
            open_in_mimir(Y.folder.path, path);
        return;
    }
    char command[256];
    if (!command_for(e->type, command, sizeof(command)))
    {
        char text[160];
        snprintf(text, sizeof(text), "No program opens %s files yet.", e->type);
        show_message("Cannot open the file", text);
        return;
    }
    run_command(command, path);
}

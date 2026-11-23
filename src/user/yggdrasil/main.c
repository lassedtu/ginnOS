/**
 * @file main.c
 * @brief yggdrasil, the file manager: the window, going from folder to
 *        folder, and the timer that reads the open folder again.
 *
 * usage: yggdrasil [folder]   (the home folder without one)
 *
 * there is no change notification in the kernel, so the open folder is
 * read again every two seconds, and after each change the window makes.
 * the keys are in actions.c (actions_key).
 */

#include "fileops.h"
#include "yggdrasil.h"

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define REFRESH_MS 2000

ygg_t Y;

void show_message(const char *title, const char *text)
{
    idunn_widget_t *dialog = idunn_dialog(title, text);
    idunn_dialog_add_button(dialog, "OK", IDUNN_PRIMARY, NULL, NULL);
    idunn_dialog_open(view_widget(), dialog);
}

/**
 * an absolute path without ".", ".." and double slashes, from a path that
 * may be relative to the open folder.
 */
static void clean_path(const char *path, char *out, size_t length)
{
    char joined[PATH_LEN];
    if (path[0] == '/')
        snprintf(joined, sizeof(joined), "%s", path);
    else
        path_join(Y.folder.path[0] ? Y.folder.path : "/", path, joined, sizeof(joined));
    char result[PATH_LEN] = "";
    size_t n = 0;
    for (char *part = strtok(joined, "/"); part; part = strtok(NULL, "/"))
    {
        if (strcmp(part, ".") == 0)
            continue;
        if (strcmp(part, "..") == 0)
        {
            char *slash = strrchr(result, '/');
            n = slash ? (size_t)(slash - result) : 0;
            result[n] = '\0';
            continue;
        }
        n += (size_t)snprintf(result + n, sizeof(result) - n, "/%s", part);
        if (n >= sizeof(result))
            n = sizeof(result) - 1;
    }
    snprintf(out, length, "%s", n ? result : "/");
}

static void push(char **stack, int *count, const char *path)
{
    if (*count == HISTORY_MAX)
    {
        free(stack[0]);
        memmove(stack, stack + 1, (HISTORY_MAX - 1) * sizeof(char *));
        (*count)--;
    }
    stack[(*count)++] = strdup(path);
}

/** open path without touching the history */
static void show(const char *path)
{
    folder_read(&Y.folder, path, Y.show_hidden, Y.mime);
    char trash_files[PATH_LEN];
    snprintf(trash_files, sizeof(trash_files), "%s/files", Y.trash);
    const char *title = strcmp(path, "/") == 0             ? "Computer"
                        : strcmp(path, Y.home) == 0        ? "Home"
                        : strcmp(path, trash_files) == 0   ? "Trash"
                                                           : path_name(path);
    idunn_window_set_title(Y.win, title);
    view_build();
    toolbar_update();
    places_update();
}

void navigate(const char *path, bool history)
{
    char clean[PATH_LEN];
    clean_path(path, clean, sizeof(clean));
    if (history && strcmp(clean, Y.folder.path) != 0)
    {
        push(Y.back, &Y.back_count, Y.folder.path);
        for (int i = 0; i < Y.forward_count; i++)
            free(Y.forward[i]);
        Y.forward_count = 0;
    }
    show(clean);
}

void go_back(void)
{
    if (Y.back_count == 0)
        return;
    char *path = Y.back[--Y.back_count];
    push(Y.forward, &Y.forward_count, Y.folder.path);
    show(path);
    free(path);
}

void go_forward(void)
{
    if (Y.forward_count == 0)
        return;
    char *path = Y.forward[--Y.forward_count];
    push(Y.back, &Y.back_count, Y.folder.path);
    show(path);
    free(path);
}

void go_up(void)
{
    if (strcmp(Y.folder.path, "/") == 0)
        return;
    char parent[PATH_LEN];
    char was[256];
    snprintf(was, sizeof(was), "%s", path_name(Y.folder.path));
    path_parent(Y.folder.path, parent, sizeof(parent));
    navigate(parent, true);
    view_select_name(was); // the folder it came from
}

static void every(void *data)
{
    (void)data;
    // the programs it started end on their own; they are not waited for
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    if (!job_running())
        view_refresh();
}

/** the home folder, its usual folders, and the trash's place */
static void find_home(void)
{
    const char *home = getenv("HOME");
    struct passwd *pw = home ? NULL : getpwuid(getuid());
    snprintf(Y.home, sizeof(Y.home), "%s", home ? home : pw ? pw->pw_dir : "/");
    path_join(Y.home, ".local/share/Trash", Y.trash, sizeof(Y.trash));
    char files[PATH_LEN];
    snprintf(files, sizeof(files), "%s/files", Y.trash);
    fileops_mkdirs(files);
    snprintf(files, sizeof(files), "%s/info", Y.trash);
    fileops_mkdirs(files);
    static const char *const folders[] = {"Desktop", "Documents", "Downloads", "Pictures",
                                          "Music"};
    if (strcmp(Y.home, "/") == 0)
        return; // no home folder of its own: its usual folders are not made
    for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++)
    {
        char path[PATH_LEN];
        path_join(Y.home, folders[i], path, sizeof(path));
        mkdir(path, 0755); // there already: nothing happens
    }
}

int main(int argc, char **argv)
{
    Y.app = idunn_app_new("yggdrasil");
    if (!Y.app)
    {
        printf("yggdrasil: no window server (start the desktop with startde)\n");
        return 1;
    }
    find_home();
    Y.mime = mime_open(MIME_CONF);
    Y.win = idunn_window_new(Y.app, "Yggdrasil", 960, 600);
    if (!Y.win)
        return 1;

    idunn_widget_t *root = idunn_row(0, 0);
    Y.places = idunn_add(root, places_build());
    idunn_add(root, idunn_separator());
    idunn_widget_t *main_column = idunn_grow(idunn_add(root, idunn_column(0, 0)), 1);
    Y.toolbar = idunn_add(main_column, toolbar_build());
    idunn_add(main_column, idunn_separator());
    Y.view_holder = idunn_grow(idunn_add(main_column, idunn_column(0, 0)), 1);
    idunn_add(main_column, idunn_separator());
    idunn_widget_t *status_bar = idunn_add(main_column, idunn_row(0, 0));
    idunn_box_set_padding(status_bar, 1, 4);
    Y.status = idunn_add(status_bar, idunn_label(" ", IDUNN_TEXT_SMALL));
    idunn_label_set_color(Y.status, IDUNN_COLOR_MUTED_FOREGROUND);
    idunn_window_set_root(Y.win, root);
    idunn_window_on_key(Y.win, actions_key, NULL);

    char start[PATH_LEN];
    if (argc > 1)
    {
        char cwd[PATH_LEN];
        if (argv[1][0] != '/' && getcwd(cwd, sizeof(cwd)))
            path_join(cwd, argv[1], start, sizeof(start));
        else
            snprintf(start, sizeof(start), "%s", argv[1]);
    }
    else
        snprintf(start, sizeof(start), "%s", Y.home);
    navigate(start, false);

    idunn_app_every(Y.app, REFRESH_MS, every, NULL);
    int status = idunn_app_run(Y.app);
    folder_free(&Y.folder);
    mime_close(Y.mime);
    return status;
}

/**
 * @file jobs.c
 * @brief yggdrasil: copy, move and delete in a child process, so the
 *        window stays usable, with a progress dialog and Cancel.
 *
 * the child does the work with fileops.c and writes lines on a pipe:
 *   T bytes       the total, first
 *   F path        it starts on a file
 *   B count       count more bytes are done
 *   E error path  a file failed (an errno value)
 * the dialog shows only when the work takes longer than a moment, so a
 * small copy does not flash it.
 */

#include "fileops.h"
#include "yggdrasil.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SHOW_AFTER_MS 300
#define LINE_MAX_     (PATH_LEN + 32)

static struct
{
    pid_t pid; // 0: no job
    int fd;
    job_kind_t kind;
    uint64_t total;
    uint64_t done;
    uint32_t started;
    char current[PATH_LEN];
    int errors;
    int first_error;
    char first_path[PATH_LEN];
    char line[LINE_MAX_];
    size_t line_length;

    idunn_widget_t *dialog;
    idunn_widget_t *name;
    idunn_widget_t *bar;
    idunn_widget_t *amount;
    bool cancelled;

    // waiting for the answer about names that are there
    char **paths;
    int count;
    char to[PATH_LEN];
} J;

static uint32_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

bool job_running(void)
{
    return J.pid != 0;
}

// the child

static int out_fd;
static volatile int stop_now;

static void say(const char *format, ...)
{
    char line[LINE_MAX_];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line, sizeof(line) - 1, format, args);
    va_end(args);
    if (n < 0)
        return;
    if (n > (int)sizeof(line) - 2)
        n = (int)sizeof(line) - 2;
    line[n++] = '\n';
    for (int done = 0; done < n;)
    {
        ssize_t w = write(out_fd, line + done, (size_t)(n - done));
        if (w <= 0 && errno != EINTR)
            _exit(1); // the window is gone
        done += w > 0 ? (int)w : 0;
    }
}

static void said_file(const char *path, void *data)
{
    (void)data;
    say("F %s", path);
}

static void said_bytes(uint64_t count, void *data)
{
    (void)data;
    say("B %llu", (unsigned long long)count);
}

static void said_error(const char *path, int error, void *data)
{
    (void)data;
    if (error != ECANCELED)
        say("E %d %s", error, path);
}

static void stop_signal(int sig)
{
    (void)sig;
    stop_now = 1;
}

static void child(job_kind_t kind, char **paths, int count, const char *to, conflict_t conflict)
{
    signal(SIGTERM, stop_signal);
    fileops_report_t report = {said_file, said_bytes, said_error, NULL, &stop_now};
    uint64_t total = 0;
    for (int i = 0; i < count; i++)
        total += fileops_size(paths[i], NULL);
    say("T %llu", (unsigned long long)total);

    for (int i = 0; i < count && !stop_now; i++)
    {
        if (kind == JOB_DELETE)
        {
            fileops_remove(paths[i], &report);
            continue;
        }
        char parent[PATH_LEN];
        path_parent(paths[i], parent, sizeof(parent));
        const char *name = path_name(paths[i]);
        char target[PATH_LEN];
        path_join(to, name, target, sizeof(target));
        struct stat st;
        bool there = lstat(target, &st) == 0;
        if (strcmp(parent, to) == 0)
        {
            if (kind == JOB_MOVE)
                continue; // it is there already
            fileops_unique(to, name, target, sizeof(target)); // a copy beside it
        }
        else if (there && conflict == CONFLICT_SKIP)
            continue;
        else if (there && conflict == CONFLICT_KEEP_BOTH)
            fileops_unique(to, name, target, sizeof(target));
        else if (there)
        {
            int error = fileops_remove(target, NULL);
            if (error)
            {
                say("E %d %s", error, target);
                continue;
            }
        }
        if (kind == JOB_COPY)
            fileops_copy(paths[i], target, &report);
        else
            fileops_move(paths[i], target, &report);
    }
    _exit(0);
}

// the window

static void cancel_clicked(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    J.dialog = NULL; // the button closed it
    J.cancelled = true;
    if (J.pid > 0)
        kill(J.pid, SIGTERM);
}

static void show_dialog(void)
{
    static const char *const titles[] = {"Copying", "Moving", "Deleting"};
    J.dialog = idunn_dialog(titles[J.kind], NULL);
    idunn_widget_t *box = idunn_column(2, 0);
    idunn_set_size(box, 360, 0);
    idunn_add(idunn_dialog_content(J.dialog), box);
    J.name = idunn_add(box, idunn_label(" ", IDUNN_TEXT_BODY));
    J.bar = idunn_add(box, idunn_progress());
    J.amount = idunn_add(box, idunn_label(" ", IDUNN_TEXT_SMALL));
    idunn_label_set_color(J.amount, IDUNN_COLOR_MUTED_FOREGROUND);
    idunn_dialog_add_button(J.dialog, "Cancel", IDUNN_OUTLINE, cancel_clicked, NULL);
    idunn_dialog_open(view_widget(), J.dialog);
}

static void update_dialog(void)
{
    if (!J.dialog && !J.cancelled && now_ms() - J.started >= SHOW_AFTER_MS)
        show_dialog();
    if (!J.dialog)
        return;
    idunn_label_set_text(J.name, J.current[0] ? path_name(J.current) : " ");
    char done[32], total[32], text[96];
    format_size(J.done, done, sizeof(done));
    format_size(J.total, total, sizeof(total));
    snprintf(text, sizeof(text), "%s of %s", done, total);
    idunn_label_set_text(J.amount, text);
    uint64_t total_bytes = J.total ? J.total : 1;
    uint64_t part = J.done > total_bytes ? total_bytes : J.done;
    idunn_progress_set(J.bar, (int32_t)(part * IDUNN_PROGRESS_MAX / total_bytes));
}

static void take_line(const char *line)
{
    switch (line[0])
    {
    case 'T':
        J.total = strtoull(line + 2, NULL, 10);
        break;
    case 'B':
        J.done += strtoull(line + 2, NULL, 10);
        break;
    case 'F':
        snprintf(J.current, sizeof(J.current), "%s", line + 2);
        break;
    case 'E':
    {
        char *rest;
        int error = (int)strtol(line + 2, &rest, 10);
        if (J.errors++ == 0)
        {
            J.first_error = error;
            snprintf(J.first_path, sizeof(J.first_path), "%s", rest + (*rest == ' '));
        }
        break;
    }
    default:
        break;
    }
}

static void finished(void)
{
    idunn_app_watch(Y.app, J.fd, NULL, NULL);
    close(J.fd);
    waitpid(J.pid, NULL, 0);
    J.pid = 0;
    if (J.dialog)
        idunn_dialog_close(J.dialog);
    J.dialog = NULL;
    view_refresh();
    if (J.errors && !J.cancelled)
    {
        static const char *const what[] = {"Some files were not copied",
                                           "Some files were not moved",
                                           "Some files were not deleted"};
        char text[PATH_LEN + 160];
        snprintf(text, sizeof(text), "%s: %s.%s", path_name(J.first_path),
                 strerror(J.first_error),
                 J.errors > 1 ? " There were more errors." : "");
        show_message(what[J.kind], text);
    }
}

static void job_ready(int fd, void *data)
{
    (void)data;
    char buffer[4096];
    for (;;)
    {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            break; // nothing more now
        if (n == 0)
        {
            finished();
            return;
        }
        for (ssize_t i = 0; i < n; i++)
        {
            if (buffer[i] == '\n')
            {
                J.line[J.line_length] = '\0';
                take_line(J.line);
                J.line_length = 0;
            }
            else if (J.line_length + 1 < sizeof(J.line))
                J.line[J.line_length++] = buffer[i];
        }
    }
    update_dialog();
}

static void run(conflict_t conflict)
{
    int fds[2];
    if (pipe(fds) != 0)
    {
        show_message("Could not start the operation", strerror(errno));
        return;
    }
    pid_t pid = fork();
    if (pid == 0)
    {
        close(fds[0]);
        out_fd = fds[1];
        child(J.kind, J.paths, J.count, J.to, conflict);
    }
    close(fds[1]);
    if (pid < 0)
    {
        close(fds[0]);
        show_message("Could not start the operation", strerror(errno));
        return;
    }
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    J.pid = pid;
    J.fd = fds[0];
    J.total = J.done = 0;
    J.current[0] = '\0';
    J.errors = 0;
    J.line_length = 0;
    J.cancelled = false;
    J.dialog = NULL;
    J.started = now_ms();
    idunn_app_watch(Y.app, J.fd, job_ready, NULL);
}

static void forget_paths(void)
{
    for (int i = 0; i < J.count; i++)
        free(J.paths[i]);
    free(J.paths);
    J.paths = NULL;
    J.count = 0;
}

static void answer(idunn_widget_t *w, void *data)
{
    (void)w;
    run((conflict_t)(intptr_t)data);
}

void job_start(job_kind_t kind, char **paths, int count, const char *to)
{
    if (J.pid)
    {
        show_message("Please wait", "Another file operation is still in progress.");
        return;
    }
    forget_paths();
    J.kind = kind;
    snprintf(J.to, sizeof(J.to), "%s", to ? to : "");
    J.paths = calloc((size_t)count, sizeof(char *));
    for (int i = 0; J.paths && i < count; i++)
    {
        // a folder cannot go into itself
        if (to && fileops_inside(to, paths[i]) && kind != JOB_DELETE)
        {
            char text[PATH_LEN + 64];
            snprintf(text, sizeof(text), "%s cannot be placed inside itself.", path_name(paths[i]));
            show_message("Could not paste", text);
            forget_paths();
            return;
        }
        J.paths[J.count++] = strdup(paths[i]);
    }
    if (!J.paths)
        return;

    // names that are in the folder already (not the files' own folder)
    int there = 0;
    for (int i = 0; kind != JOB_DELETE && i < J.count; i++)
    {
        char parent[PATH_LEN], target[PATH_LEN];
        path_parent(J.paths[i], parent, sizeof(parent));
        path_join(J.to, path_name(J.paths[i]), target, sizeof(target));
        struct stat st;
        if (strcmp(parent, J.to) != 0 && lstat(target, &st) == 0)
            there++;
    }
    if (there == 0)
    {
        run(CONFLICT_SKIP);
        return;
    }
    char title[96];
    if (there == 1)
        snprintf(title, sizeof(title), "An item with the same name already exists");
    else
        snprintf(title, sizeof(title), "%d items with the same names already exist", there);
    idunn_widget_t *dialog = idunn_dialog(title, "Replace the existing items, keep both (new "
                                                 "copies get a number), or skip them?");
    idunn_dialog_add_button(dialog, "Skip", IDUNN_OUTLINE, answer,
                            (void *)(intptr_t)CONFLICT_SKIP);
    idunn_dialog_add_button(dialog, "Keep both", IDUNN_SECONDARY, answer,
                            (void *)(intptr_t)CONFLICT_KEEP_BOTH);
    idunn_dialog_add_button(dialog, "Replace", IDUNN_DESTRUCTIVE, answer,
                            (void *)(intptr_t)CONFLICT_REPLACE);
    idunn_dialog_open(view_widget(), dialog);
}

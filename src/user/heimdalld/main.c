/**
 * @file main.c
 * @brief heimdalld, the ginnOS window server.
 *
 * usage: heimdalld [-s] [-x] [-w image] [program [arguments]]
 *
 * takes the screen, the keyboard and the mouse, and serves windows on the
 * Unix socket HEIM_SOCKET (heimdall_proto.h). with a program, starts it
 * once the socket is ready. Ctrl+Alt+Backspace stops the server; the text
 * console comes back as it was.
 *
 *   -w image   the wallpaper (PNG, JPEG or BMP), scaled to cover the screen;
 *              without it, the one of the settings ([desktop] wallpaper in
 *              desktop.conf), else /usr/share/wallpapers/default.png
 *   -x         stop when the program ends (the session: startde runs
 *              "heimdalld -x asgard"); heimdalld ends with the
 *              program's exit status
 *   -s         every 2 seconds with frames, a line of statistics in the
 *              kernel log (/dev/kmsg, the serial port): frames, pixels and
 *              the time in composing, presenting and flushing
 */

#include "heimdalld.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fb.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define FONTS "/usr/share/fonts/"
#define POLL_MAX 64
#define DEFAULT_WALLPAPER "/usr/share/wallpapers/default.png"

server_t S;

extern char **environ;

static void on_signal(int sig)
{
    (void)sig;
    S.quit = true;
}

static bool listen_socket(void)
{
    S.listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, HEIM_SOCKET);
    if (S.listen_fd < 0 || bind(S.listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(S.listen_fd, 16) != 0)
    {
        printf("heimdalld: cannot serve on %s (is a server running?)\n", HEIM_SOCKET);
        return false;
    }
    return true;
}

static void load_look(void)
{
    S.title_font = bragi_font_open(FONTS "inter/Inter-SemiBold.ttf");
    idunn_theme_load(&S.theme);
}

static pid_t program; // the program of the command line (0: none)
static bool exit_with_program;
static int program_status; // its exit status: heimdalld -x ends with it

static void start(char **argv)
{
    pid_t pid = fork();
    program = pid > 0 ? pid : 0;
    if (pid == 0)
    {
        // heimdalld ignores SIGPIPE (a client can go away while it writes),
        // and an ignored signal stays ignored through execve: give the
        // program, and so every program of the desktop, the default back
        signal(SIGPIPE, SIG_DFL);
        execve(argv[0], argv, environ);
        // a name without a path is in /bin
        char path[128];
        snprintf(path, sizeof(path), "/bin/%s", argv[0]);
        execve(path, argv, environ);
        printf("heimdalld: cannot start %s\n", argv[0]);
        _exit(127);
    }
}

/**
 * SIGCHLD only wakes poll (it is ignored if no handler catches it): the
 * loop then reaps the child, so a program that ends while nothing else
 * happens is seen at once.
 */
static void child_ended(int sig)
{
    (void)sig;
}

static void reap_children(void)
{
    pid_t ended;
    int status;
    while ((ended = waitpid(-1, &status, WNOHANG)) > 0)
    {
        if (ended == program && exit_with_program)
        {
            S.quit = true; // the session's program ended
            program_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        }
    }
}

static void loop(void)
{
    struct pollfd fds[POLL_MAX];
    client_t *owners[POLL_MAX];
    while (!S.quit)
    {
        reap_children();
        if (S.quit)
            break;
        screen_compose();

        int n = 0;
        fds[n++] = (struct pollfd){S.listen_fd, POLLIN, 0};
        fds[n++] = (struct pollfd){S.input_fd, POLLIN, 0};
        fds[n++] = (struct pollfd){S.display_fd, POLLIN, 0}; // poll skips -1
        for (client_t *c = S.clients; c && n < POLL_MAX; c = c->next)
        {
            owners[n] = c;
            short events = POLLIN;
            if (c->out_count)
                events |= POLLOUT;
            fds[n++] = (struct pollfd){c->fd, events, 0};
        }
        if (poll(fds, (nfds_t)n, -1) < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }

        if (fds[1].revents)
            input_read();
        if (fds[2].revents & POLLIN)
            screen_display_events();
        for (int i = 3; i < n; i++)
        {
            client_t *c = owners[i];
            if (fds[i].revents & (POLLIN | POLLHUP | POLLERR))
                client_read(c);
            if (fds[i].revents & POLLOUT)
                client_flush(c);
        }
        if (fds[0].revents)
            client_accept(S.listen_fd);
        client_reap();
    }
}

int main(int argc, char **argv)
{
    const char *wallpaper = screen_wallpaper_setting(
        access(DEFAULT_WALLPAPER, R_OK) == 0 ? DEFAULT_WALLPAPER : NULL);
    int arg = 1;
    for (;;)
    {
        if (arg + 1 < argc && strcmp(argv[arg], "-w") == 0)
        {
            wallpaper = argv[arg + 1];
            arg += 2;
        }
        else if (arg < argc && strcmp(argv[arg], "-x") == 0)
        {
            exit_with_program = true;
            arg++;
        }
        else if (arg < argc && strcmp(argv[arg], "-s") == 0)
        {
            S.stats.fd = open("/dev/kmsg", O_WRONLY);
            arg++;
        }
        else
            break;
    }

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGPIPE, SIG_IGN);

    load_look();
    if (!listen_socket() || !screen_init(wallpaper) || !input_init())
        return 1;
    // the scale can depend on the size of the screen
    idunn_theme_fit(&S.theme, S.width);
    window_look();
    struct sigaction sa = {0};
    sa.sa_handler = child_ended; // no SA_RESTART: poll ends with EINTR
    sigaction(SIGCHLD, &sa, NULL);
    if (arg < argc)
        start(argv + arg);

    loop();

    // the clients see their sockets close; the console comes back
    int fb = fb_open();
    int off = 0;
    ioctl(fb, FBIO_GRAPHICS, &off);
    close(fb);
    printf("heimdalld: stopped\n");
    return program_status;
}

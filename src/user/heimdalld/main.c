/**
 * @file main.c
 * @brief heimdalld, the ginnOS window server.
 *
 * usage: heimdalld [-w image] [program [arguments]]
 *
 * takes the screen, the keyboard and the mouse, and serves windows on the
 * Unix socket HEIM_SOCKET (heimdall_proto.h). with a program, starts it
 * once the socket is ready. Ctrl+Alt+Backspace stops the server; the text
 * console comes back as it was.
 *
 *   -w image   the wallpaper (PNG, JPEG or BMP), scaled to cover the screen;
 *              without it, /usr/share/wallpapers/default.png if it is there
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

static void start(char **argv)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        execve(argv[0], argv, environ);
        // a name without a path is in /bin
        char path[128];
        snprintf(path, sizeof(path), "/bin/%s", argv[0]);
        execve(path, argv, environ);
        printf("heimdalld: cannot start %s\n", argv[0]);
        _exit(127);
    }
}

static void loop(void)
{
    struct pollfd fds[POLL_MAX];
    client_t *owners[POLL_MAX];
    while (!S.quit)
    {
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
        while (waitpid(-1, NULL, WNOHANG) > 0)
            ;
    }
}

int main(int argc, char **argv)
{
    const char *wallpaper = access(DEFAULT_WALLPAPER, R_OK) == 0 ? DEFAULT_WALLPAPER : NULL;
    int arg = 1;
    if (arg + 1 < argc && strcmp(argv[arg], "-w") == 0)
    {
        wallpaper = argv[arg + 1];
        arg += 2;
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
    if (arg < argc)
        start(argv + arg);

    loop();

    // the clients see their sockets close; the console comes back
    int fb = fb_open();
    int off = 0;
    ioctl(fb, FBIO_GRAPHICS, &off);
    close(fb);
    printf("heimdalld: stopped\n");
    return 0;
}

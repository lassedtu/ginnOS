/**
 * @file posix.c
 * @brief libctest: the POSIX parts the Unix tools need: getline, the
 *        parts of a path, device numbers, the time zone fields, shell
 *        patterns, search trees, memory streams, strptime, realpath,
 *        mkdtemp, sysconf and the names of signals and users.
 */

#define SYSLOG_NAMES
#include "libctest.h"
#include <syslog.h>
#include <dlfcn.h>
#include <langinfo.h>

#include <fnmatch.h>
#include <inttypes.h>
#include <libgen.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pty.h>
#include <search.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

static void test_getline(void)
{
    char path[] = "/tmp/libctest.XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd < 0)
        return;
    const char text[] = "one\nlonger line two, longer than the first buffer of all\nthree";
    CHECK(write(fd, text, sizeof(text) - 1) == (ssize_t)(sizeof(text) - 1));
    close(fd);

    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    char *line = NULL;
    size_t size = 0;
    CHECK(getline(&line, &size, f) == 4 && strcmp(line, "one\n") == 0);
    ssize_t second = (ssize_t)(strchr(text + 4, '\n') - (text + 4)) + 1;
    CHECK(getline(&line, &size, f) == second && line[second - 1] == '\n' && size > 64);
    CHECK(getline(&line, &size, f) == 5 && strcmp(line, "three") == 0); // no newline at the end
    CHECK(getline(&line, &size, f) == -1);
    rewind(f);
    CHECK(getdelim(&line, &size, 'e', f) == 3 && strcmp(line, "one") == 0);
    free(line);
    fclose(f);
    unlink(path);
}

/** basename and dirname change their argument: each check has its own copy */
static int base_is(const char *path, const char *want)
{
    char copy[64];
    strcpy(copy, path);
    return strcmp(basename(copy), want) == 0;
}

static int dir_is(const char *path, const char *want)
{
    char copy[64];
    strcpy(copy, path);
    return strcmp(dirname(copy), want) == 0;
}

static void test_paths(void)
{
    CHECK(base_is("/usr/lib", "lib") && base_is("/usr/lib/", "lib") && base_is("lib", "lib"));
    CHECK(base_is("/", "/") && base_is("", "."));
    CHECK(dir_is("/usr/lib", "/usr") && dir_is("/usr/lib//", "/usr") && dir_is("usr", "."));
    CHECK(dir_is("/usr", "/") && dir_is("/", "/") && dir_is("a//b", "a"));
}

static void test_numbers(void)
{
    CHECK(strtoumax("18446744073709551615", NULL, 10) == UINTMAX_MAX);
    CHECK(strtoimax("-42", NULL, 10) == -42);
    unsigned int dev = makedev(8, 300);
    CHECK(major(dev) == 8 && minor(dev) == 300);
}

static void test_zone(void)
{
    time_t t = 0;
    struct tm tm;
    gmtime_r(&t, &tm);
    CHECK(tm.tm_gmtoff == 0 && strcmp(tm.tm_zone, "UTC") == 0);
}

static void test_fnmatch(void)
{
    CHECK(fnmatch("*.c", "main.c", 0) == 0);
    CHECK(fnmatch("*.c", "main.h", 0) == FNM_NOMATCH);
    CHECK(fnmatch("[a-c]?x", "b1x", 0) == 0);
    CHECK(fnmatch("*", "a/b", 0) == 0);
    CHECK(fnmatch("*", "a/b", FNM_PATHNAME) == FNM_NOMATCH);
    CHECK(fnmatch("*", ".hidden", FNM_PERIOD) == FNM_NOMATCH);
    CHECK(fnmatch("\\*", "*", 0) == 0);
    CHECK(fnmatch("README", "readme", FNM_CASEFOLD) == 0);
    CHECK(fnmatch("[[:digit:]]*", "7up", 0) == 0);
}

static int compare_ints(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;
    return x < y ? -1 : x > y;
}

static int deepest;
static int visited;

static void note(const void *node, VISIT which, int depth)
{
    (void)node;
    if (which == leaf || which == postorder)
        visited++;
    deepest = depth > deepest ? depth : deepest;
}

static void test_tree(void)
{
    static int numbers[1000];
    void *root = NULL;
    // in order: an unbalanced tree would be a list 1000 deep
    for (int i = 0; i < 1000; i++)
    {
        numbers[i] = i;
        CHECK_QUIET(tsearch(&numbers[i], &root, compare_ints) != NULL);
    }
    int again = 500;
    CHECK(*(int **)tsearch(&again, &root, compare_ints) == &numbers[500]); // found, not added
    CHECK(tfind(&again, &root, compare_ints) != NULL);
    deepest = 0;
    visited = 0;
    twalk(root, note);
    CHECK(visited == 1000);
    CHECK(deepest < 15); // an AVL tree of 1000 is at most 14 deep
    for (int i = 0; i < 1000; i += 2)
        CHECK_QUIET(tdelete(&numbers[i], &root, compare_ints) != NULL);
    CHECK(tfind(&again, &root, compare_ints) == NULL);
    int odd = 501;
    CHECK(tfind(&odd, &root, compare_ints) != NULL);
    visited = 0;
    twalk(root, note);
    CHECK(visited == 500);
    tdestroy(root, NULL);

    int table[4] = {3, 1};
    size_t count = 2;
    int four = 4;
    CHECK(lfind(&four, table, &count, sizeof(int), compare_ints) == NULL);
    CHECK(lsearch(&four, table, &count, sizeof(int), compare_ints) == &table[2] && count == 3);
}

static void test_memory_stream(void)
{
    char memory[16] = "hello world";
    FILE *f = fmemopen(memory, sizeof(memory), "r");
    CHECK(f != NULL);
    char word[8];
    CHECK(fgets(word, 6, f) && strcmp(word, "hello") == 0);
    fclose(f);

    f = fmemopen(memory, sizeof(memory), "w");
    CHECK(memory[0] == '\0');
    fprintf(f, "n=%d", 42);
    fflush(f);
    CHECK(strcmp(memory, "n=42") == 0);
    CHECK(ftell(f) == 4);
    fseek(f, 0, SEEK_SET);
    fputc('m', f);
    fclose(f);
    CHECK(strcmp(memory, "m=42") == 0);

    // writing stops at the end of the memory
    char small[4];
    f = fmemopen(small, sizeof(small), "w");
    fputs("abcdef", f);
    fclose(f);
    CHECK(memcmp(small, "abcd", 4) == 0);

    // no memory given: the stream has its own
    f = fmemopen(NULL, 32, "w+");
    fputs("own", f);
    rewind(f);
    char back[8] = {0};
    CHECK(fgets(back, sizeof(back), f) && strcmp(back, "own") == 0);
    fclose(f);
}

static void test_strptime(void)
{
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    char *end = strptime("2026-11-23T13:45:07rest", "%Y-%m-%dT%H:%M:%S", &tm);
    CHECK(end && strcmp(end, "rest") == 0);
    CHECK(tm.tm_year == 126 && tm.tm_mon == 10 && tm.tm_mday == 23);
    CHECK(tm.tm_hour == 13 && tm.tm_min == 45 && tm.tm_sec == 7);
    memset(&tm, 0, sizeof(tm));
    CHECK(strptime("Mon  23 nov 98 1:05 pm", "%a %d %b %y %I:%M %p", &tm) != NULL);
    CHECK(tm.tm_wday == 1 && tm.tm_mon == 10 && tm.tm_year == 98 && tm.tm_hour == 13);
    CHECK(strptime("12/31/05", "%D", &tm) && tm.tm_year == 105 && tm.tm_mday == 31);
    CHECK(strptime("+0130", "%z", &tm) && tm.tm_gmtoff == 5400);
    CHECK(strptime("13:61", "%H:%M", &tm) == NULL); // no minute 61
    CHECK(strptime("x", "%d", &tm) == NULL);
}

static void test_temp_files(void)
{
    char path[] = "/tmp/libctest.XXXXXX";
    CHECK(mkdtemp(path) != NULL);
    struct stat st;
    CHECK(stat(path, &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & 0777) == 0700);
    char inside[64];
    snprintf(inside, sizeof(inside), "%s/../%s/.", path, path + 5);
    char resolved[PATH_MAX];
    CHECK(realpath(inside, resolved) && strcmp(resolved, path) == 0);
    CHECK(realpath("/tmp/no-such-file", resolved) == NULL);
    char *made = realpath("/", NULL);
    CHECK(made && strcmp(made, "/") == 0);
    free(made);
    rmdir(path);
}

static void test_system(void)
{
    CHECK(sysconf(_SC_CLK_TCK) == 100 && sysconf(_SC_PAGESIZE) == 4096);
    CHECK(sysconf(-5) == -1);
    char host[HOST_NAME_MAX + 1];
    CHECK(gethostname(host, sizeof(host)) == 0 && host[0]);
    CHECK(getlogin() != NULL);
    CHECK(strcmp(strsignal(SIGTERM), "Terminated") == 0);
    CHECK(strcmp(strsignal(99), "Unknown signal 99") == 0);
}

/** run /bin/echo with count arguments; the bytes it wrote, or -1 */
static int echo_many(int count, const char *word)
{
    char **argv = calloc((size_t)count + 2, sizeof(char *));
    int pipe_fds[2];
    if (!argv || pipe(pipe_fds) < 0)
        return -1;
    argv[0] = "echo";
    for (int i = 1; i <= count; i++)
        argv[i] = (char *)word;
    pid_t pid = fork();
    if (pid == 0)
    {
        dup2(pipe_fds[1], 1);
        execv("/bin/echo", argv);
        _exit(errno == E2BIG ? 100 : 101);
    }
    close(pipe_fds[1]);
    free(argv);
    int total = 0;
    char buffer[512];
    ssize_t n;
    while ((n = read(pipe_fds[0], buffer, sizeof(buffer))) > 0)
        total += (int)n;
    close(pipe_fds[0]);
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 100 ? -2 : total;
}

/** exec takes many arguments (more than the 64 it used to keep), up to ARG_MAX bytes */
static void test_exec_args(void)
{
    CHECK(echo_many(300, "ab") == 300 * 3); // "ab " 299 times and "ab\n"
    static char big[1000];
    memset(big, 'x', sizeof(big) - 1);
    CHECK(echo_many(100, big) == 100 * 1000);
    CHECK(echo_many(200, big) == -2); // 200 kB: E2BIG
}

/** more files open at once than the 16 a process could have */
static void test_many_files(void)
{
    int fds[40];
    int opened = 0;
    for (int i = 0; i < 40; i++)
    {
        fds[i] = open("/etc/passwd", O_RDONLY);
        opened += fds[i] >= 0;
    }
    CHECK(opened == 40);
    for (int i = 0; i < 40; i++)
        if (fds[i] >= 0)
            close(fds[i]);
    CHECK(sysconf(_SC_OPEN_MAX) == 64);
}

/** sethostname, clock_settime, ttyname, the syslog names (as root) */
static void test_system_calls(void)
{
    char name[65];
    CHECK(gethostname(name, sizeof(name)) == 0);
    char old[65];
    strcpy(old, name);
    CHECK(sethostname("libctest-host", 13) == 0);
    struct utsname u;
    CHECK(uname(&u) == 0 && strcmp(u.nodename, "libctest-host") == 0);
    CHECK(sethostname(old, strlen(old)) == 0);
    char long_name[80];
    memset(long_name, 'x', sizeof(long_name));
    CHECK(sethostname(long_name, sizeof(long_name)) == -1 && errno == EINVAL);

    // a minute on, then back
    struct timespec now;
    CHECK(clock_gettime(CLOCK_REALTIME, &now) == 0);
    struct timespec later = {now.tv_sec + 60, 0};
    CHECK(clock_settime(CLOCK_REALTIME, &later) == 0);
    struct timespec read_back;
    CHECK(clock_gettime(CLOCK_REALTIME, &read_back) == 0);
    CHECK(read_back.tv_sec >= now.tv_sec + 59 && read_back.tv_sec <= now.tv_sec + 62);
    later.tv_sec = read_back.tv_sec - 60;
    CHECK(clock_settime(CLOCK_REALTIME, &later) == 0);
    CHECK(clock_settime(CLOCK_MONOTONIC, &later) == -1 && errno == EINVAL);

    // on any terminal (the console's /dev/tty0, or a /dev/pts/N in the
    // desktop's terminal), ttyname names the same device as fd 0
    char *tty = ttyname(0);
    struct stat by_fd, by_name;
    if (isatty(0))
    {
        CHECK(tty != NULL && strncmp(tty, "/dev/", 5) == 0 && fstat(0, &by_fd) == 0 &&
              stat(tty, &by_name) == 0 && by_fd.st_rdev == by_name.st_rdev);
    }
    else
    {
        CHECK(tty == NULL && errno == ENOTTY);
    }
    int pipe_fds[2];
    CHECK(pipe(pipe_fds) == 0);
    CHECK(ttyname(pipe_fds[0]) == NULL && errno == ENOTTY);
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    struct stat st;
    CHECK(stat("/dev/tty0", &st) == 0 && major(st.st_rdev) == 4 && minor(st.st_rdev) == 0);

    // a pseudo-terminal: the slave's name is in /dev/pts
    int master;
    int slave;
    char pts[32];
    CHECK(openpty(&master, &slave, pts, NULL, NULL) == 0);
    tty = ttyname(slave);
    CHECK(tty != NULL && strcmp(tty, pts) == 0);
    close(slave);
    close(master);

    CHECK(LOG_FAC(LOG_LOCAL3) == 19 && (LOG_MAKEPRI(LOG_DAEMON, LOG_ERR) & LOG_FACMASK) ==
                                           LOG_DAEMON);
    int found = 0;
    for (CODE *c = facilitynames; c->c_name; c++)
        found += strcmp(c->c_name, "cron") == 0 && c->c_val == LOG_CRON;
    CHECK(found == 1);
    sync();
}

/** nice values: root may raise and lower them; a user only raise */
static void test_priorities(void)
{
    errno = 0;
    CHECK(getpriority(PRIO_PROCESS, 0) == 0 && errno == 0);
    CHECK(nice(5) == 5);
    CHECK(getpriority(PRIO_PROCESS, (id_t)getpid()) == 5);
    CHECK(setpriority(PRIO_PROCESS, 0, 100) == 0 && getpriority(PRIO_PROCESS, 0) == 19);
    CHECK(setpriority(PRIO_PROCESS, 0, 0) == 0 && getpriority(PRIO_PROCESS, 0) == 0);
    CHECK(getpriority(PRIO_PROCESS, 99999) == -1 && errno == ESRCH);
    CHECK(getpriority(7, 0) == -1 && errno == EINVAL);

    // a child that becomes a plain user: it may go to 10, not back to 0
    pid_t pid = fork();
    if (pid == 0)
    {
        int ok = setuid(1000) == 0 && nice(10) == 10 && nice(-5) == -1 && errno == EPERM &&
                 setpriority(PRIO_PROCESS, 1, 5) == -1 && errno == EPERM;
        _exit(ok ? 0 : 1);
    }
    int status;
    CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/** nl_langinfo, and dlopen without a dynamic loader */
static void test_langinfo_dlfcn(void)
{
    CHECK(strcmp(nl_langinfo(CODESET), "UTF-8") == 0);
    CHECK(strcmp(nl_langinfo(D_FMT), "%m/%d/%y") == 0);
    CHECK(strcmp(nl_langinfo(RADIXCHAR), ".") == 0);
    CHECK(strcmp(nl_langinfo(-1), "") == 0 && strcmp(nl_langinfo(1000), "") == 0);

    CHECK(dlerror() == NULL);
    CHECK(dlopen("libfoo.so", RTLD_NOW) == NULL);
    const char *why = dlerror();
    CHECK(why != NULL && why[0] != '\0');
    CHECK(dlerror() == NULL); // the reason is given once
    CHECK(dlsym(RTLD_DEFAULT, "main") == NULL && dlerror() != NULL);
    CHECK(dlclose(NULL) == 0);
}

static void test_getopt(void)
{
    char *argv[] = {"prog", "-ab", "-c3", "-c", "4", "-x", "file", "-d", NULL};
    int argc = 8;
    optind = 0; // start again
    opterr = 0;
    CHECK(getopt(argc, argv, "abc:d") == 'a');
    CHECK(getopt(argc, argv, "abc:d") == 'b');
    CHECK(getopt(argc, argv, "abc:d") == 'c' && strcmp(optarg, "3") == 0);
    CHECK(getopt(argc, argv, "abc:d") == 'c' && strcmp(optarg, "4") == 0);
    CHECK(getopt(argc, argv, "abc:d") == '?' && optopt == 'x');
    CHECK(getopt(argc, argv, "abc:d") == -1 && optind == 6); // stops at "file"

    char *missing[] = {"prog", "-c", NULL};
    optind = 0;
    CHECK(getopt(2, missing, ":c:") == ':' && optopt == 'c');
    optind = 0;
    opterr = 1;
}

void test_posix(void)
{
    test_getopt();
    test_langinfo_dlfcn();
    test_getline();
    test_paths();
    test_numbers();
    test_zone();
    test_fnmatch();
    test_tree();
    test_memory_stream();
    test_strptime();
    test_temp_files();
    test_system();
    test_exec_args();
    test_many_files();
    test_system_calls();
    test_priorities();

    // the limits getconf shows
    CHECK(pathconf("/", _PC_NAME_MAX) == NAME_MAX && pathconf("/etc", _PC_PATH_MAX) == PATH_MAX);
    CHECK(pathconf("/no/such", _PC_NAME_MAX) == -1 && errno == ENOENT);
    CHECK(fpathconf(0, _PC_PIPE_BUF) == PIPE_BUF && fpathconf(0, 99) == -1 && errno == EINVAL);
    char path[8];
    CHECK(confstr(_CS_PATH, path, sizeof(path)) == 5 && strcmp(path, "/bin") == 0);
    CHECK(confstr(_CS_PATH, path, 3) == 5 && strcmp(path, "/b") == 0);
    CHECK(NZERO == 20);
}

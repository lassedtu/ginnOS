/**
 * @file su.c
 * @brief run a shell (or a command) as another user: su [user] [command...]
 *
 * su is set-user-ID root. root changes to any user without a password;
 * others give the target user's password (from /etc/shadow). the user is
 * root when none is given.
 */

#include <crypt.h>
#include <stdbool.h>
#include <errno.h>
#include <pwd.h>
#include <shadow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** true when the password of name is right (or the account needs none) */
static bool password_ok(const char *name)
{
    struct spwd *sp = getspnam(name);
    if (!sp)
        return false;
    char hash[160];
    snprintf(hash, sizeof(hash), "%s", sp->sp_pwdp);
    if (hash[0] == '\0')
        return true; // an account with no password
    if (hash[0] == '!' || hash[0] == '*')
        return false; // locked
    char *typed = getpass("Password: ");
    bool ok = typed && crypt_verify(typed, hash);
    if (typed)
        memset(typed, 0, strlen(typed));
    return ok;
}

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "root";
    struct passwd *found = getpwnam(name);
    if (!found)
    {
        fprintf(stderr, "su: unknown user '%s'\n", name);
        return 1;
    }
    // getpwnam's buffer is used again by getspnam's file reads: copy it
    struct passwd user = *found;
    char dir[128];
    char shell[128];
    snprintf(dir, sizeof(dir), "%s", found->pw_dir);
    snprintf(shell, sizeof(shell), "%s", found->pw_shell);
    user.pw_dir = dir;
    user.pw_shell = shell;

    if (getuid() != 0 && !password_ok(name))
    {
        sleep(2); // a wrong guess costs time
        fprintf(stderr, "su: incorrect password\n");
        return 1;
    }

    // the group first: setuid gives up the right to change it
    if (setgid(user.pw_gid) < 0 || setuid(user.pw_uid) < 0)
    {
        perror("su");
        return 1;
    }
    if (chdir(user.pw_dir) < 0)
        chdir("/");
    setenv("HOME", user.pw_dir, 1);
    setenv("USER", name, 1);
    setenv("SHELL", user.pw_shell, 1);

    char *shell_argv[] = {user.pw_shell, NULL};
    char **run = argc > 2 ? argv + 2 : shell_argv;

    // a command without a slash is looked for in /bin
    char path[128];
    if (!strchr(run[0], '/'))
    {
        snprintf(path, sizeof(path), "/bin/%s", run[0]);
        execve(path, run, environ);
    }
    execve(run[0], run, environ);
    perror(run[0]);
    return 127;
}

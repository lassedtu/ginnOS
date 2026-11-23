/**
 * @file passwd.c
 * @brief change a password: passwd [user]
 *
 * passwd is set-user-ID root, so it can write /etc/shadow. a user changes
 * their own password and gives the old one first; root changes any
 * user's password without the old one. the new hash is "$5$" (SHA-256,
 * crypt.h) with a fresh salt. the file is written whole to a new file
 * and then renamed over the old one, so it is never half written.
 */

#include <crypt.h>
#include <fcntl.h>
#include <pwd.h>
#include <shadow.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SHADOW_MAX 8192
#define NAME_MAX_LEN 64

static char *read_all(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    char *text = calloc(1, SHADOW_MAX + 1);
    ssize_t n = text ? read(fd, text, SHADOW_MAX) : -1;
    close(fd);
    if (n < 0)
    {
        free(text);
        return NULL;
    }
    return text;
}

/** /etc/shadow with name's hash put in (a new line when it has none) */
static bool write_shadow(const char *name, const char *hash)
{
    char *old = read_all(SHADOW_FILE);
    if (!old)
        return false;
    char *out = malloc(SHADOW_MAX + 512);
    if (!out)
    {
        free(old);
        return false;
    }
    size_t used = 0;
    bool done = false;
    size_t name_length = strlen(name);
    for (char *line = old; *line;)
    {
        char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        if (!done && length > name_length && strncmp(line, name, name_length) == 0 &&
            line[name_length] == ':')
        {
            // name:hash:the rest as it was
            char *rest = memchr(line + name_length + 1, ':', length - name_length - 1);
            used += (size_t)snprintf(out + used, SHADOW_MAX + 512 - used, "%s:%s", name, hash);
            if (rest)
            {
                memcpy(out + used, rest, (size_t)(line + length - rest));
                used += (size_t)(line + length - rest);
            }
            else
            {
                used += (size_t)snprintf(out + used, SHADOW_MAX + 512 - used, ":0:0:99999:7:::");
            }
            out[used++] = '\n';
            done = true;
        }
        else if (length > 0)
        {
            memcpy(out + used, line, length);
            used += length;
            out[used++] = '\n';
        }
        line += length + (end ? 1 : 0);
    }
    if (!done)
        used += (size_t)snprintf(out + used, SHADOW_MAX + 512 - used, "%s:%s:0:0:99999:7:::\n",
                                 name, hash);
    free(old);

    const char *temp = SHADOW_FILE ".new";
    int fd = open(temp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    bool ok = fd >= 0 && write(fd, out, used) == (ssize_t)used;
    if (fd >= 0)
    {
        fsync(fd);
        close(fd);
    }
    memset(out, 0, used);
    free(out);
    if (ok)
        ok = rename(temp, SHADOW_FILE) == 0;
    if (!ok)
        unlink(temp);
    return ok;
}

int main(int argc, char **argv)
{
    uid_t me = getuid();
    char name[NAME_MAX_LEN];
    if (argc > 1)
        snprintf(name, sizeof(name), "%s", argv[1]);
    else
    {
        struct passwd *pw = getpwuid(me);
        if (!pw)
        {
            fprintf(stderr, "passwd: you are not in /etc/passwd\n");
            return 1;
        }
        snprintf(name, sizeof(name), "%s", pw->pw_name);
    }
    struct passwd *target = getpwnam(name);
    if (!target)
    {
        fprintf(stderr, "passwd: unknown user '%s'\n", name);
        return 1;
    }
    if (me != 0 && target->pw_uid != me)
    {
        fprintf(stderr, "passwd: only root can change another user's password\n");
        return 1;
    }
    printf("Changing the password of %s.\n", name);

    if (me != 0)
    {
        struct spwd *sp = getspnam(name);
        char hash[160];
        snprintf(hash, sizeof(hash), "%s", sp ? sp->sp_pwdp : "!");
        if (hash[0] != '\0')
        {
            char *typed = getpass("Current password: ");
            bool ok = typed && crypt_verify(typed, hash);
            if (typed)
                memset(typed, 0, strlen(typed));
            if (!ok)
            {
                sleep(2);
                fprintf(stderr, "passwd: incorrect password\n");
                return 1;
            }
        }
    }

    char first[128];
    char *typed = getpass("New password: ");
    if (!typed || !typed[0])
    {
        fprintf(stderr, "passwd: the password cannot be empty\n");
        return 1;
    }
    snprintf(first, sizeof(first), "%s", typed);
    memset(typed, 0, strlen(typed));
    typed = getpass("Retype the new password: ");
    bool same = typed && strcmp(first, typed) == 0;
    if (typed)
        memset(typed, 0, strlen(typed));
    if (!same)
    {
        memset(first, 0, sizeof(first));
        fprintf(stderr, "passwd: the passwords do not match\n");
        return 1;
    }
    char *hash = crypt_new(first, name, strlen(name));
    memset(first, 0, sizeof(first));
    if (!hash || !write_shadow(name, hash))
    {
        perror("passwd: cannot write " SHADOW_FILE);
        return 1;
    }
    printf("The password is changed.\n");
    return 0;
}

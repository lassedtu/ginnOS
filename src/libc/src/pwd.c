#include "pwd.h"
#include "grp.h"

#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"

#define LINE_MAX 256
#define FIELDS_MAX 7

/**
 * read the line of a ':' file whose field key_field matches key (a name,
 * or a number when key is NULL and number matches) and split it.
 * @return the number of fields, or 0 if there is no such line.
 */
static int find_line(const char *path, int key_field, const char *key, unsigned int number,
                     char *line, char **fields)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;

    static char buffer[2048];
    int size = (int)read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (size <= 0)
        return 0;
    buffer[size] = '\0';

    for (char *start = buffer; *start;)
    {
        char *end = strchr(start, '\n');
        int len = end ? (int)(end - start) : (int)strlen(start);
        if (len >= LINE_MAX)
            len = LINE_MAX - 1;
        memcpy(line, start, len);
        line[len] = '\0';
        start = end ? end + 1 : start + strlen(start);

        int count = 0;
        char *field = line;
        while (count < FIELDS_MAX)
        {
            fields[count++] = field;
            char *colon = strchr(field, ':');
            if (!colon)
                break;
            *colon = '\0';
            field = colon + 1;
        }
        if (count <= key_field)
            continue;

        if (key ? strcmp(fields[key_field], key) == 0
                : (unsigned int)atoi(fields[key_field]) == number)
            return count;
    }
    return 0;
}

static struct passwd *user_from(int count, char **fields)
{
    static struct passwd user;
    if (count < 7)
        return NULL;
    user.pw_name = fields[0];
    user.pw_passwd = fields[1];
    user.pw_uid = (uid_t)atoi(fields[2]);
    user.pw_gid = (gid_t)atoi(fields[3]);
    user.pw_gecos = fields[4];
    user.pw_dir = fields[5];
    user.pw_shell = fields[6];
    return &user;
}

static struct group *group_from(int count, char **fields)
{
    static struct group group;
    if (count < 4)
        return NULL;
    group.gr_name = fields[0];
    group.gr_passwd = fields[1];
    group.gr_gid = (gid_t)atoi(fields[2]);
    group.gr_mem_list = fields[3];
    return &group;
}

static char line[LINE_MAX];
static char *fields[FIELDS_MAX];

struct passwd *getpwuid(uid_t uid)
{
    return user_from(find_line("/etc/passwd", 2, NULL, uid, line, fields), fields);
}

struct passwd *getpwnam(const char *name)
{
    return user_from(find_line("/etc/passwd", 0, name, 0, line, fields), fields);
}

struct group *getgrgid(gid_t gid)
{
    return group_from(find_line("/etc/group", 2, NULL, gid, line, fields), fields);
}

struct group *getgrnam(const char *name)
{
    return group_from(find_line("/etc/group", 0, name, 0, line, fields), fields);
}

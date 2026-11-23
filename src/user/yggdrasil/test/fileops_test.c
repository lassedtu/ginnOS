/**
 * @file fileops_test.c
 * @brief host tests of yggdrasil's file operations, in a new folder under
 *        the system's temporary folder.
 */

#include "../fileops.h"
#include "hosttest.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char base[512];

static void path(char *out, const char *name)
{
    snprintf(out, 1024, "%s/%s", base, name);
}

static void write_file(const char *name, const char *text)
{
    char p[1024];
    path(p, name);
    FILE *f = fopen(p, "w");
    fputs(text, f);
    fclose(f);
}

static bool there(const char *name)
{
    char p[1024];
    struct stat st;
    path(p, name);
    return lstat(p, &st) == 0;
}

static bool has_text(const char *name, const char *text)
{
    char p[1024];
    char got[256] = "";
    path(p, name);
    FILE *f = fopen(p, "r");
    if (!f)
        return false;
    size_t n = fread(got, 1, sizeof(got) - 1, f);
    got[n] = '\0';
    fclose(f);
    return strcmp(got, text) == 0;
}

static uint64_t counted;
static int errors;
static void count_bytes(uint64_t n, void *data)
{
    (void)data;
    counted += n;
}
static void count_error(const char *p, int e, void *data)
{
    (void)p;
    (void)e;
    (void)data;
    errors++;
}

static void test_copy_move_remove(void)
{
    char a[1024], b[1024], c[1024];
    mkdir((path(a, "tree"), a), 0755);
    mkdir((path(a, "tree/inner"), a), 0750);
    write_file("tree/one.txt", "one");
    write_file("tree/inner/two.txt", "two two");

    int files = 0;
    path(a, "tree");
    CHECK_EQ(fileops_size(a, &files), 10);
    CHECK_EQ(files, 2);

    // a tree copies with its contents and modes; the bytes are reported
    fileops_report_t report = {NULL, count_bytes, count_error, NULL, NULL};
    path(b, "copy");
    CHECK_EQ(fileops_copy(a, b, &report), 0);
    CHECK_EQ(counted, 10);
    CHECK(has_text("copy/inner/two.txt", "two two"));
    struct stat st;
    path(c, "copy/inner");
    stat(c, &st);
    CHECK_EQ(st.st_mode & 0777, 0750);

    // a copy onto a name that is there fails, and nothing changes
    write_file("taken.txt", "keep");
    path(a, "tree/one.txt");
    path(b, "taken.txt");
    CHECK_EQ(fileops_copy(a, b, &report), EEXIST);
    CHECK(has_text("taken.txt", "keep"));

    // a move is a rename on the same disk
    path(a, "copy");
    path(b, "moved");
    CHECK_EQ(fileops_move(a, b, NULL), 0);
    CHECK(!there("copy") && there("moved/inner/two.txt"));

    // remove takes a whole tree
    CHECK_EQ(fileops_remove(b, NULL), 0);
    CHECK(!there("moved"));
    path(a, "nothing");
    errors = 0;
    CHECK_EQ(fileops_remove(a, &report), ENOENT);
    CHECK_EQ(errors, 1);

    // a stop set before the work: no half file stays
    volatile int stop = 1;
    fileops_report_t stopping = {NULL, NULL, NULL, NULL, &stop};
    path(a, "tree/one.txt");
    path(b, "stopped.txt");
    CHECK_EQ(fileops_copy(a, b, &stopping), ECANCELED);
    CHECK(!there("stopped.txt"));
}

static void test_names(void)
{
    char out[1024];
    write_file("report.txt", "");
    fileops_unique(base, "report.txt", out, sizeof(out));
    CHECK(strcmp(strrchr(out, '/'), "/report (2).txt") == 0);
    write_file("report (2).txt", "");
    fileops_unique(base, "report.txt", out, sizeof(out));
    CHECK(strcmp(strrchr(out, '/'), "/report (3).txt") == 0);
    write_file(".hidden", "");
    fileops_unique(base, ".hidden", out, sizeof(out));
    CHECK(strcmp(strrchr(out, '/'), "/.hidden (2)") == 0);
    fileops_unique(base, "free.txt", out, sizeof(out));
    CHECK(strcmp(strrchr(out, '/'), "/free.txt") == 0);

    CHECK(fileops_inside("/a/b", "/a"));
    CHECK(fileops_inside("/a", "/a"));
    CHECK(fileops_inside("/a/b", "/a/"));
    CHECK(!fileops_inside("/ab", "/a"));
    CHECK(fileops_inside("/x", "/"));
}

static void test_trash(void)
{
    char trash[1024], p[1024], restored[1024];
    path(trash, "Trash");
    mkdir((path(p, "docs"), p), 0755);
    write_file("docs/note.txt", "note");

    // in the trash: files/ has it, info/ says where it was
    path(p, "docs/note.txt");
    CHECK_EQ(trash_put(trash, p, 0), 0);
    CHECK(!there("docs/note.txt"));
    CHECK(there("Trash/files/note.txt"));
    char info[1024];
    path(info, "Trash/info/note.txt.trashinfo");
    FILE *f = fopen(info, "r");
    char text[1200] = "";
    if (f)
    {
        text[fread(text, 1, sizeof(text) - 1, f)] = '\0';
        fclose(f);
    }
    CHECK(strstr(text, "[Trash Info]\nPath=") == text);
    CHECK(strstr(text, "/docs/note.txt\n") != NULL);
    CHECK(strstr(text, "DeletionDate=") != NULL);

    // a second note.txt gets another name in the trash
    write_file("docs/note.txt", "second");
    CHECK_EQ(trash_put(trash, p, 0), 0);
    CHECK(there("Trash/files/note (2).txt"));

    // restore puts it back; when the place is taken, beside it
    write_file("docs/note.txt", "new one");
    CHECK_EQ(trash_restore(trash, "note.txt", restored, sizeof(restored)), 0);
    CHECK(strcmp(strrchr(restored, '/'), "/note (2).txt") == 0);
    CHECK(has_text("docs/note (2).txt", "note"));
    CHECK(!there("Trash/info/note.txt.trashinfo"));

    // a folder that is gone is made again
    char docs[1024];
    path(docs, "docs");
    fileops_remove(docs, NULL);
    CHECK_EQ(trash_restore(trash, "note (2).txt", restored, sizeof(restored)), 0);
    CHECK(has_text("docs/note.txt", "second"));

    // the root is there: making it is no error
    CHECK_EQ(fileops_mkdirs("/"), 0);
    CHECK_EQ(fileops_mkdirs(base), 0);

    // empty: nothing stays
    write_file("old.txt", "old");
    path(p, "old.txt");
    trash_put(trash, p, 0);
    CHECK_EQ(trash_empty(trash), 0);
    CHECK(!there("Trash/files/old.txt") && !there("Trash/info/old.txt.trashinfo"));
    CHECK(there("Trash/files"));
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    snprintf(base, sizeof(base), "%s/yggdrasil-test-XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp(base))
    {
        perror("mkdtemp");
        return 1;
    }
    test_copy_move_remove();
    test_names();
    test_trash();
    fileops_remove(base, NULL);
    return hosttest_done("yggdrasil fileops");
}

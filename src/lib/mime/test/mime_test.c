/**
 * @file mime_test.c
 * @brief host tests of the mime library (make host-test), with the real table.
 */

#include "mime.h"
#include "hosttest.h"

#include <string.h>

static mime_table_t *table;

static const char *type_of(const char *name, const char *head, size_t n)
{
    return mime_lookup(table, name, (const uint8_t *)head, n)->type;
}

static void test_names(void)
{
    CHECK(strcmp(type_of("main.c", NULL, 0), "text/x-csrc") == 0);
    CHECK(strcmp(type_of("/usr/include/stdio.h", NULL, 0), "text/x-csrc") == 0);
    CHECK(strcmp(type_of("PHOTO.JPG", NULL, 0), "image/jpeg") == 0); // letter case
    CHECK(strcmp(type_of("doom1.wad", NULL, 0), "application/x-doom-wad") == 0);
    CHECK(strcmp(mime_lookup(table, "doom1.wad", NULL, 0)->icon, "doom") == 0);
    CHECK(strcmp(type_of("notes.txt", NULL, 0), "text/plain") == 0);
    // the last dot of the last part only
    CHECK(strcmp(type_of("a.d/readme", NULL, 0), "application/octet-stream") == 0);
    CHECK(strcmp(type_of("archive.tar.gz", NULL, 0), "application/gzip") == 0);
    // a name that starts with a dot has no extension; "x." has none either
    CHECK(strcmp(type_of(".c", NULL, 0), "application/octet-stream") == 0);
    CHECK(strcmp(type_of("x.", NULL, 0), "application/octet-stream") == 0);
}

static void test_contents(void)
{
    CHECK(strcmp(type_of("sh", "\x7F" "ELF\x01\x01", 6), "application/x-executable") == 0);
    CHECK(strcmp(type_of("x", "\x89PNG\r\n\x1a\n", 8), "image/png") == 0);
    CHECK(strcmp(type_of("x", "\xFF\xD8\xFF\xE0", 4), "image/jpeg") == 0);
    CHECK(strcmp(type_of("run", "#!/bin/sh\necho hi\n", 18), "text/x-script") == 0);
    CHECK(strcmp(type_of("README", "hello, world\n", 13), "text/plain") == 0);
    CHECK(strcmp(type_of("LICENSE", "caf\xC3\xA9 \xE2\x82", 8), "text/plain") == 0); // cut at the end
    CHECK(strcmp(type_of("blob", "\x00\x01\x02\x03", 4), "application/octet-stream") == 0);
    CHECK(strcmp(type_of("blob", "\xC3\x28", 2), "application/octet-stream") == 0); // bad UTF-8
    CHECK(strcmp(type_of("empty", "", 0), "application/octet-stream") == 0);
    // the name wins over the contents
    CHECK(strcmp(type_of("picture.png", "plain text", 10), "image/png") == 0);
    // a magic longer than the bytes there does not match
    CHECK(strcmp(type_of("x", "\x89P", 2), "application/octet-stream") == 0);
}

static void test_files(void)
{
    const mime_type_t *dir = mime_lookup_file(table, TEST_DIR);
    CHECK(strcmp(dir->type, "inode/directory") == 0 && strcmp(dir->icon, "folder") == 0);
    CHECK(strcmp(mime_lookup_file(table, TEST_DIR "/mime_test.c")->type, "text/x-csrc") == 0);
    CHECK(strcmp(mime_lookup_file(table, TEST_DIR "/Makefile")->type, "text/plain") == 0);
    CHECK(strcmp(mime_lookup_file(table, TEST_DIR "/no-such-file")->type,
                 "application/octet-stream") == 0);
}

static void test_parsing(void)
{
    mime_table_t *t = mime_from_text("# a comment\n"
                                     "a/b  icon-a  .aa magic:\\x41\\x42 bad-rule magic:\\xZZ\n"
                                     "lonely\n"
                                     "\n"
                                     "c/d  icon-c  .cc   # .dd is in a comment\n"
                                     "e/f  icon-e  magic:#!x\n");
    CHECK(t != NULL);
    CHECK(strcmp(mime_lookup(t, "x.aa", NULL, 0)->icon, "icon-a") == 0);
    CHECK(strcmp(mime_lookup(t, "x", (const uint8_t *)"AB", 2)->type, "a/b") == 0);
    CHECK(strcmp(mime_lookup(t, "x.cc", NULL, 0)->type, "c/d") == 0);
    CHECK(strcmp(mime_lookup(t, "x.dd", NULL, 0)->type, "application/octet-stream") == 0);
    CHECK(strcmp(mime_lookup(t, "x", (const uint8_t *)"#!x", 3)->type, "e/f") == 0);
    // no text rule: text is unknown
    CHECK(strcmp(mime_lookup(t, "x", (const uint8_t *)"hello", 5)->type,
                 "application/octet-stream") == 0);
    mime_close(t);
    CHECK(mime_open(TEST_DIR "/no-such.conf") == NULL);
    CHECK(strcmp(mime_lookup(NULL, "x.c", NULL, 0)->type, "application/octet-stream") == 0);
}

int main(void)
{
    table = mime_open(CONF);
    CHECK(table != NULL);
    if (!table)
        return hosttest_done("mime");
    test_names();
    test_contents();
    test_files();
    test_parsing();
    mime_close(table);
    return hosttest_done("mime");
}

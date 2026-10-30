#include "ktest.h"

#include "kernel/vfs/vfs.h"

// resolve input against cwd; returns the result, or "<fail>" on error.
static const char *resolve(const char *cwd, const char *input)
{
    static char out[VFS_PATH_MAX];
    if (!vfs_resolve_path(cwd, input, out, sizeof(out)))
        return "<fail>";
    return out;
}

KTEST(path_absolute_normalisation)
{
    KEXPECT_STREQ(resolve("/", "/"), "/");
    KEXPECT_STREQ(resolve("/", "/a/b/../c"), "/a/c");
    KEXPECT_STREQ(resolve("/", "/a/./b"), "/a/b");
    KEXPECT_STREQ(resolve("/", "//a//b/"), "/a/b");
    KEXPECT_STREQ(resolve("/", "/../.."), "/");
    KEXPECT_STREQ(resolve("/", "/a/b/../../.."), "/");
}

KTEST(path_relative_to_cwd)
{
    KEXPECT_STREQ(resolve("/home", "x"), "/home/x");
    KEXPECT_STREQ(resolve("/home/", "x"), "/home/x");
    KEXPECT_STREQ(resolve("/home/u", "../x"), "/home/x");
    KEXPECT_STREQ(resolve("/home/u", "."), "/home/u");
    KEXPECT_STREQ(resolve("/", "bin/sh"), "/bin/sh");
}

KTEST(path_invalid_inputs)
{
    char small[4];
    KEXPECT_STREQ(resolve("/", ""), "<fail>");
    KEXPECT(!vfs_resolve_path("/", "/abcdef", small, sizeof(small)));
    KEXPECT(!vfs_resolve_path(NULL, "/a", small, sizeof(small)));
}

KTEST(path_join)
{
    char out[32];
    KEXPECT(vfs_join_path("/", "a", out, sizeof(out)));
    KEXPECT_STREQ(out, "/a");
    KEXPECT(vfs_join_path("/b", "a", out, sizeof(out)));
    KEXPECT_STREQ(out, "/b/a");
    KEXPECT(vfs_join_path("/b/", "a", out, sizeof(out)));
    KEXPECT_STREQ(out, "/b/a");

    char tiny[4];
    KEXPECT(!vfs_join_path("/abc", "def", tiny, sizeof(tiny)));
}

#include "ktest.h"

#include "common/memory.h"
#include "common/string.h"
#include "kernel/vfs/vfs.h"

// the test kernel boots from its own copy of the disk image, so writing to
// the root filesystem here never touches the normal build's image.
#define TEST_FILE "/ktest.tmp"
#define TEST_DIR  "/ktestdir"

// larger than one ext2 block so reads and writes cross block boundaries.
#define TEST_BYTES 5000u

// static: test bodies run on the boot stack, keep big buffers off it.
static uint8_t write_buf[TEST_BYTES];
static uint8_t read_buf[TEST_BYTES];

KTEST(ext2_create_write_read_remove)
{
    for (uint32_t i = 0; i < TEST_BYTES; i++)
        write_buf[i] = (uint8_t)(i * 7 + 3);

    vfs_remove(TEST_FILE); // leftover from an aborted earlier run, if any
    KEXPECT(kerr_ok(vfs_create(TEST_FILE)));

    vfs_file_t file;
    KEXPECT(kerr_ok(vfs_open(TEST_FILE, &file)));
    KEXPECT_EQ(vfs_write(&file, TEST_BYTES, write_buf), TEST_BYTES);
    vfs_close(&file);

    vfs_stat_t st;
    KEXPECT(kerr_ok(vfs_stat(TEST_FILE, &st)));
    KEXPECT_EQ(st.size, TEST_BYTES);

    memset(read_buf, 0, sizeof(read_buf));
    KEXPECT(kerr_ok(vfs_open(TEST_FILE, &file)));
    KEXPECT_EQ(vfs_read(&file, TEST_BYTES, read_buf), TEST_BYTES);
    // reading past the end returns nothing.
    KEXPECT_EQ(vfs_read(&file, 16, read_buf), 0);
    vfs_close(&file);
    KEXPECT(memcmp(write_buf, read_buf, TEST_BYTES) == 0);

    KEXPECT(kerr_ok(vfs_remove(TEST_FILE)));
    KEXPECT(kerr_failed(vfs_open(TEST_FILE, &file)));
}

KTEST(ext2_truncate)
{
    vfs_remove(TEST_FILE);
    KEXPECT(kerr_ok(vfs_create(TEST_FILE)));

    vfs_file_t file;
    KEXPECT(kerr_ok(vfs_open(TEST_FILE, &file)));
    KEXPECT_EQ(vfs_write(&file, 100, write_buf), 100);
    KEXPECT(kerr_ok(vfs_truncate(&file, 0)));
    vfs_close(&file);

    vfs_stat_t st;
    KEXPECT(kerr_ok(vfs_stat(TEST_FILE, &st)));
    KEXPECT_EQ(st.size, 0);

    KEXPECT(kerr_ok(vfs_remove(TEST_FILE)));
}

KTEST(ext2_mkdir_rmdir)
{
    vfs_rmdir(TEST_DIR);
    KEXPECT(kerr_ok(vfs_mkdir(TEST_DIR)));

    vfs_stat_t st;
    KEXPECT(kerr_ok(vfs_stat(TEST_DIR, &st)));
    KEXPECT_EQ(st.file_type, FS_TYPE_DIR);

    // creating the same directory twice fails.
    KEXPECT(kerr_failed(vfs_mkdir(TEST_DIR)));

    KEXPECT(kerr_ok(vfs_rmdir(TEST_DIR)));
    KEXPECT(kerr_failed(vfs_stat(TEST_DIR, &st)));
}

KTEST(ext2_missing_file)
{
    vfs_file_t file;
    vfs_stat_t st;
    KEXPECT(kerr_failed(vfs_open("/does/not/exist", &file)));
    KEXPECT(kerr_failed(vfs_stat("/does/not/exist", &st)));
    KEXPECT(kerr_failed(vfs_remove("/does/not/exist")));
}

KTEST(ext2_root_has_bin_sh)
{
    vfs_stat_t st;
    KEXPECT(kerr_ok(vfs_stat("/bin/sh", &st)));
    KEXPECT(st.size > 0);
}

/**
 * write a few bytes to a new file.
 */
static bool make_file(const char *path, const char *text)
{
    vfs_file_t file;
    vfs_remove(path);
    if (kerr_failed(vfs_create(path)) || kerr_failed(vfs_open(path, &file)))
    {
        return false;
    }
    uint32_t length = (uint32_t)strlen(text);
    bool ok = vfs_write(&file, length, text) == length;
    vfs_close(&file);
    return ok;
}

KTEST(ext2_rename_replaces_a_file)
{
    KEXPECT(make_file(TEST_FILE, "new"));
    KEXPECT(make_file(TEST_FILE "2", "old!"));
    KEXPECT(kerr_ok(vfs_rename(TEST_FILE, TEST_FILE "2")));

    vfs_stat_t st;
    KEXPECT(kerr_failed(vfs_stat(TEST_FILE, &st)));
    KEXPECT(kerr_ok(vfs_stat(TEST_FILE "2", &st)));
    KEXPECT_EQ(st.size, 3); // the renamed file, not the replaced one
    KEXPECT(kerr_ok(vfs_remove(TEST_FILE "2")));
}

KTEST(ext2_rename_directories)
{
    vfs_stat_t st;
    vfs_rmdir(TEST_DIR "/a/b");
    vfs_rmdir(TEST_DIR "/a");
    vfs_rmdir(TEST_DIR "/c");
    vfs_rmdir(TEST_DIR);
    KEXPECT(kerr_ok(vfs_mkdir(TEST_DIR)));
    KEXPECT(kerr_ok(vfs_mkdir(TEST_DIR "/a")));
    KEXPECT(kerr_ok(vfs_mkdir(TEST_DIR "/a/b")));
    KEXPECT(kerr_ok(vfs_mkdir(TEST_DIR "/c")));

    // not into itself, nor below itself
    KEXPECT_EQ(vfs_rename(TEST_DIR "/a", TEST_DIR "/a/b/a"), KERR_INVAL);
    // not over a directory that has entries, nor over a file
    KEXPECT_EQ(vfs_rename(TEST_DIR "/c", TEST_DIR "/a"), KERR_NOTEMPTY);
    KEXPECT(make_file(TEST_FILE, "x"));
    KEXPECT_EQ(vfs_rename(TEST_DIR "/c", TEST_FILE), KERR_NOTDIR);
    KEXPECT_EQ(vfs_rename(TEST_FILE, TEST_DIR "/c"), KERR_ISDIR);
    KEXPECT(kerr_ok(vfs_remove(TEST_FILE)));

    // over an empty directory, and to another parent
    KEXPECT(kerr_ok(vfs_rename(TEST_DIR "/a/b", TEST_DIR "/c")));
    KEXPECT(kerr_failed(vfs_stat(TEST_DIR "/a/b", &st)));
    KEXPECT(kerr_ok(vfs_stat(TEST_DIR "/c", &st)));
    KEXPECT_EQ(st.file_type, FS_TYPE_DIR);

    // the moved directory's ".." follows it: it can be removed, then its
    // old and new parents
    KEXPECT(kerr_ok(vfs_rename(TEST_DIR "/c", TEST_DIR "/a/moved")));
    KEXPECT(kerr_ok(vfs_rmdir(TEST_DIR "/a/moved")));
    KEXPECT(kerr_ok(vfs_rmdir(TEST_DIR "/a")));
    KEXPECT(kerr_ok(vfs_rmdir(TEST_DIR)));
}

KTEST(ext2_rmdir_not_empty)
{
    vfs_rmdir(TEST_DIR "/in");
    vfs_rmdir(TEST_DIR);
    KEXPECT(kerr_ok(vfs_mkdir(TEST_DIR)));
    KEXPECT(kerr_ok(vfs_mkdir(TEST_DIR "/in")));
    KEXPECT_EQ(vfs_rmdir(TEST_DIR), KERR_NOTEMPTY);
    KEXPECT(kerr_ok(vfs_rmdir(TEST_DIR "/in")));
    KEXPECT(kerr_ok(vfs_rmdir(TEST_DIR)));
}

KTEST(ext2_file_times)
{
    vfs_stat_t st;
    KEXPECT(make_file(TEST_FILE, "t"));
    KEXPECT(kerr_ok(vfs_stat(TEST_FILE, &st)));
    // the clock is set at boot (2020 or later); new files carry the time
    KEXPECT(st.mtime > 1577836800u);
    KEXPECT(st.ctime >= st.mtime);
    KEXPECT(kerr_ok(vfs_remove(TEST_FILE)));
}

KTEST(ext2_hard_link)
{
    vfs_stat_t st;
    vfs_remove(TEST_FILE "2");
    KEXPECT(make_file(TEST_FILE, "shared"));
    KEXPECT(kerr_ok(vfs_link(TEST_FILE, TEST_FILE "2")));
    KEXPECT_EQ(vfs_link(TEST_FILE, TEST_FILE "2"), KERR_EXIST);
    KEXPECT(kerr_ok(vfs_stat(TEST_FILE "2", &st)));
    KEXPECT_EQ(st.links_count, 2);
    KEXPECT_EQ(st.size, 6);

    // the data stays until the last name goes
    KEXPECT(kerr_ok(vfs_remove(TEST_FILE)));
    KEXPECT(kerr_ok(vfs_stat(TEST_FILE "2", &st)));
    KEXPECT_EQ(st.links_count, 1);
    KEXPECT_EQ(st.size, 6);
    KEXPECT(kerr_ok(vfs_remove(TEST_FILE "2")));

    // a directory has one name only
    vfs_rmdir(TEST_DIR);
    KEXPECT(kerr_ok(vfs_mkdir(TEST_DIR)));
    KEXPECT_EQ(vfs_link(TEST_DIR, TEST_FILE), KERR_ISDIR);
    KEXPECT(kerr_ok(vfs_rmdir(TEST_DIR)));
}

// a target longer than the 60 bytes an inode keeps: it takes a block
#define LONG_TARGET "/a/target/that/is/longer/than/the/sixty/bytes/of/i_block/in/an/inode"

KTEST(ext2_symbolic_links)
{
    char target[VFS_PATH_MAX];
    char path[VFS_PATH_MAX];
    vfs_stat_t st;

    vfs_remove(TEST_FILE "l");
    vfs_remove(TEST_FILE "L");
    KEXPECT(make_file(TEST_FILE, "data"));
    KEXPECT(kerr_ok(vfs_symlink(TEST_FILE "l", "ktest.tmp")));
    KEXPECT(kerr_ok(vfs_symlink(TEST_FILE "L", LONG_TARGET)));

    KEXPECT(kerr_ok(vfs_readlink(TEST_FILE "l", target, sizeof(target))));
    KEXPECT(strcmp(target, "ktest.tmp") == 0);
    KEXPECT(kerr_ok(vfs_readlink(TEST_FILE "L", target, sizeof(target))));
    KEXPECT(strcmp(target, LONG_TARGET) == 0);
    KEXPECT_EQ(vfs_readlink(TEST_FILE, target, sizeof(target)), KERR_INVAL);

    // a link is its own file; following it reaches the target
    KEXPECT(kerr_ok(vfs_stat(TEST_FILE "l", &st)));
    KEXPECT_EQ(st.file_type, FS_TYPE_SYMLINK);
    strcpy(path, TEST_FILE "l");
    KEXPECT(kerr_ok(vfs_follow(path, sizeof(path), true)));
    KEXPECT(strcmp(path, TEST_FILE) == 0);
    strcpy(path, TEST_FILE "l");
    KEXPECT(kerr_ok(vfs_follow(path, sizeof(path), false)));
    KEXPECT(strcmp(path, TEST_FILE "l") == 0);

    // removing a link leaves the target; the long one frees its block
    KEXPECT(kerr_ok(vfs_remove(TEST_FILE "l")));
    KEXPECT(kerr_ok(vfs_remove(TEST_FILE "L")));
    KEXPECT(kerr_ok(vfs_stat(TEST_FILE, &st)));
    KEXPECT(kerr_ok(vfs_remove(TEST_FILE)));
}

KTEST(ext2_follow_directory_links)
{
    char path[VFS_PATH_MAX];
    vfs_remove(TEST_DIR "/in/file");
    vfs_rmdir(TEST_DIR "/in");
    vfs_remove(TEST_DIR "/up");
    vfs_remove(TEST_DIR "/loop");
    vfs_rmdir(TEST_DIR);
    KEXPECT(kerr_ok(vfs_mkdir(TEST_DIR)));
    KEXPECT(kerr_ok(vfs_mkdir(TEST_DIR "/in")));
    KEXPECT(make_file(TEST_DIR "/in/file", "x"));

    // a relative link to a directory, in the middle of a path
    KEXPECT(kerr_ok(vfs_symlink(TEST_DIR "/up", "in/../in")));
    strcpy(path, TEST_DIR "/up/file");
    KEXPECT(kerr_ok(vfs_follow(path, sizeof(path), false)));
    KEXPECT(strcmp(path, TEST_DIR "/in/file") == 0);

    // a link to itself
    KEXPECT(kerr_ok(vfs_symlink(TEST_DIR "/loop", "loop")));
    strcpy(path, TEST_DIR "/loop");
    KEXPECT_EQ(vfs_follow(path, sizeof(path), true), KERR_LOOP);

    KEXPECT(kerr_ok(vfs_remove(TEST_DIR "/loop")));
    KEXPECT(kerr_ok(vfs_remove(TEST_DIR "/up")));
    KEXPECT(kerr_ok(vfs_remove(TEST_DIR "/in/file")));
    KEXPECT(kerr_ok(vfs_rmdir(TEST_DIR "/in")));
    KEXPECT(kerr_ok(vfs_rmdir(TEST_DIR)));
}

// past the direct blocks and the single indirect table (1 KiB blocks: 12
// direct, then 256 through one table): the cut frees part of a table
#define BIG_BYTES (300u * 1024u)

KTEST(ext2_truncate_to_a_length)
{
    vfs_file_t file;
    vfs_stat_t st;
    vfs_remove(TEST_FILE);
    KEXPECT(kerr_ok(vfs_create(TEST_FILE)));
    KEXPECT(kerr_ok(vfs_open(TEST_FILE, &file)));
    for (uint32_t i = 0; i < TEST_BYTES; i++)
        write_buf[i] = (uint8_t)(i * 7 + 3);
    for (uint32_t done = 0; done < BIG_BYTES; done += TEST_BYTES)
        KEXPECT_EQ(vfs_write(&file, TEST_BYTES, write_buf), TEST_BYTES);
    vfs_close(&file);
    KEXPECT(kerr_ok(vfs_stat(TEST_FILE, &st)));
    uint32_t big_blocks = st.blocks;

    // cut inside the single indirect range, mid block
    KEXPECT(kerr_ok(vfs_open(TEST_FILE, &file)));
    KEXPECT(kerr_ok(vfs_truncate(&file, 100u * 1024u + 10u)));
    vfs_close(&file);
    KEXPECT(kerr_ok(vfs_stat(TEST_FILE, &st)));
    KEXPECT_EQ(st.size, 100u * 1024u + 10u);
    KEXPECT(st.blocks < big_blocks);

    // grow again: the old bytes past the cut read as zeros
    KEXPECT(kerr_ok(vfs_open(TEST_FILE, &file)));
    KEXPECT(kerr_ok(vfs_truncate(&file, 100u * 1024u + 4000u)));
    KEXPECT_EQ(vfs_lseek(&file, 100 * 1024 - 100, 0), 100 * 1024 - 100);
    KEXPECT_EQ(vfs_read(&file, 4100, read_buf), 4100);
    vfs_close(&file);
    uint32_t start = 100u * 1024u - 100u;
    bool kept = true;
    for (uint32_t i = 0; i < 110; i++)
        kept = kept && read_buf[i] == (uint8_t)(((start + i) % TEST_BYTES) * 7 + 3);
    KEXPECT(kept);
    bool zeros = true;
    for (uint32_t i = 110; i < 4100; i++)
        zeros = zeros && read_buf[i] == 0;
    KEXPECT(zeros);

    KEXPECT(kerr_ok(vfs_remove(TEST_FILE)));
}

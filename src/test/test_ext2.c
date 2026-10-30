#include "ktest.h"

#include "common/memory.h"
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
    KEXPECT(kerr_ok(vfs_truncate(&file)));
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

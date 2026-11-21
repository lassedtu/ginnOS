#pragma once

/**
 * open-file descriptions.
 *
 * open(), pipe() and the console each make a file_t. a file descriptor is
 * a pointer to one, so dup, dup2, fork and spawn share the description: the
 * same file offset, the same status flags. the description is released
 * (the file closed, the pipe end shut) when its last descriptor closes.
 *
 * each kind of file (a filesystem file, a pipe end, the console) supplies a
 * file_ops_t, so the syscalls never switch on the kind.
 *
 * locking: the reference count is guarded by one global spinlock. the
 * operations do their own locking (pipes their spinlock, ext2 its mutex).
 */

#include "common/stdint.h"
#include "common/error.h"
#include "arch/arch.h"
#include "kernel/vfs/vfs.h"
#include "kernel/vfs/stat.h"
#include "kernel/syscall/pipe.h"

// access modes and status flags (the low bits are ABI, as in libc fcntl.h)
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_ACCMODE   0x0003
#define O_CREAT     0x0040
#define O_EXCL      0x0080
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_NONBLOCK  0x0800
#define O_DIRECTORY 0x10000
#define O_CLOEXEC   0x80000

typedef struct file file_t;

/**
 * what a kind of file can do. a NULL entry means the operation is not
 * supported, and the file_* wrappers return the usual errno for that.
 * every function returns a count or 0 on success, or a negative errno.
 */
typedef struct
{
    int32_t (*read)(file_t *file, void *buf, uint32_t count);
    int32_t (*write)(file_t *file, const void *buf, uint32_t count);
    int32_t (*seek)(file_t *file, int32_t offset, int whence);
    int32_t (*ioctl)(file_t *file, uint32_t request, void *arg);
    int32_t (*mmap)(file_t *file, addr_space_t as, uintptr_t virt, size_t length, int prot,
                    uint32_t offset);
    int32_t (*read_entry)(file_t *file, fs_dirent_t *entry);
    int32_t (*truncate)(file_t *file);
    int32_t (*stat)(file_t *file, struct kstat *st);
    uint32_t (*poll)(file_t *file); // POLL* events that would not block now
    void (*release)(file_t *file); // the last reference is gone
} file_ops_t;

/**
 * an open-file description.
 */
struct file
{
    const file_ops_t *ops;
    uint32_t refcount; // descriptors (and kernel users) referring to it
    uint32_t flags;    // O_ access mode and status flags
    fs_dirent_t *lookahead; // a directory entry read but not yet returned
    union
    {
        vfs_file_t vfs; // a filesystem file or directory (its offset inside)
        struct
        {
            pipe_buf_t *buf;
            pipe_dir_t dir;
        } pipe; // a pipe end
    };
};

/**
 * open a file or directory through the VFS.
 * @param path absolute path.
 * @param flags O_ access mode and status flags to keep in the description.
 * @param out receives the new description (one reference).
 * @return KERR_OK, KERR_NOMEM, or the VFS's error.
 */
kerr_t file_open(const char *path, uint32_t flags, file_t **out);

/**
 * make the two ends of a new pipe.
 * @param read_end receives the read end (one reference).
 * @param write_end receives the write end (one reference).
 * @return KERR_OK or KERR_NOMEM.
 */
kerr_t file_open_pipe(file_t **read_end, file_t **write_end);

/**
 * the console (keyboard in, screen out). there is one console description;
 * every process starts with it on fds 0, 1 and 2.
 * @return the console description, with a new reference.
 */
file_t *file_console(void);

/**
 * add a reference to a description.
 * @param file the description.
 */
void file_ref(file_t *file);

/**
 * drop a reference; the last one releases the description. may sleep
 * (closing a filesystem file takes its mutex).
 * @param file the description (NULL is ignored).
 */
void file_unref(file_t *file);

/**
 * true if the description is a filesystem file (not a pipe or the console).
 */
bool file_is_vfs(const file_t *file);

/**
 * true if the description was opened for reading (or writing).
 */
bool file_readable(const file_t *file);
bool file_writable(const file_t *file);

/**
 * the operations, with an errno for the unsupported ones: read and write
 * EBADF (wrong access mode, or no op), seek ESPIPE, ioctl ENOTTY, mmap
 * ENODEV, read_entry ENOTDIR, truncate EINVAL. the buffers are kernel
 * pointers or user pointers the caller has checked.
 */
int32_t file_read(file_t *file, void *buf, uint32_t count);
int32_t file_write(file_t *file, const void *buf, uint32_t count);
int32_t file_seek(file_t *file, int32_t offset, int whence);
int32_t file_ioctl(file_t *file, uint32_t request, void *arg);
int32_t file_mmap(file_t *file, addr_space_t as, uintptr_t virt, size_t length, int prot,
                  uint32_t offset);
int32_t file_read_entry(file_t *file, fs_dirent_t *entry);

/**
 * give back the entry file_read_entry returned last, so the next call
 * returns it again (getdents, when it does not fit the caller's buffer).
 * @return 0, or -ENOMEM.
 */
int32_t file_unread_entry(file_t *file, const fs_dirent_t *entry);
int32_t file_truncate(file_t *file);
int32_t file_stat(file_t *file, struct kstat *st);

/**
 * the readiness of a file (kernel/vfs/poll.h). a file without a poll op
 * (a regular file) is always ready to read and write.
 * @return POLL* events.
 */
uint32_t file_poll(file_t *file);

/**
 * read or write at an offset without moving the description's offset
 * (pread, pwrite). only for files that can seek (ESPIPE otherwise). not
 * atomic against another process using the same description.
 */
int32_t file_pread(file_t *file, void *buf, uint32_t count, int32_t offset);
int32_t file_pwrite(file_t *file, const void *buf, uint32_t count, int32_t offset);

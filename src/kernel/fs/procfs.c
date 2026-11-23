#include "procfs.h"

#include "common/memory.h"
#include "common/stdio.h"
#include "common/string.h"
#include "kernel/errno.h"
#include "kernel/fs/block_cache.h"
#include "drivers/pci/pci.h"
#include "drivers/pci/pci_irq.h"
#include "kernel/audio/audio.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/pmm.h"
#include "kernel/process/process.h"
#include "kernel/sync/mutex.h"
#include "kernel/time/timer.h"
#include "kernel/vfs/stat.h"

/**
 * @file procfs.c
 * @brief a read-only filesystem that shows kernel state as text.
 *
 *   /proc/meminfo       physical memory and kernel heap use
 *   /proc/uptime        seconds since boot
 *   /proc/pci           the PCI functions (drivers/pci)
 *   /proc/interrupts    the device interrupts and their counts
 *   /proc/audio         the sound device and its counters
 *   /proc/<pid>/status  a process's name, state, parent and resources
 *   /proc/self          the calling process's directory
 *
 * a file's text is made when the file is opened and kept with the open
 * file, so a reader sees one consistent snapshot.
 */

// most bytes of one file's text
#define PROCFS_TEXT_MAX 2048

// most processes a directory listing shows
#define PROCFS_PIDS_MAX PROCESS_MAX

typedef enum
{
    NODE_ROOT,    // /proc
    NODE_PID_DIR, // /proc/<pid>
    NODE_TEXT,    // a file of text
} node_kind_t;

/**
 * what an open procfs file holds (fs_data).
 */
typedef struct
{
    node_kind_t kind;
    uint32_t pid;  // for NODE_PID_DIR
    uint32_t size; // for NODE_TEXT
    char text[PROCFS_TEXT_MAX];
} procfs_node_t;

// the files of the root that are not process directories
static const char *const root_files[] = {"meminfo", "uptime", "pci", "interrupts", "audio", "self"};
#define ROOT_FILE_COUNT 6

/*
 * the kernel's formatter writes one character at a time to a function
 * without context, so text is made into one target at a time, under this
 * mutex.
 */
static mutex_t format_lock = MUTEX_INIT;
static char *format_target;
static uint32_t format_used;

static void format_putchar(char c)
{
    if (format_used < PROCFS_TEXT_MAX - 1)
    {
        format_target[format_used++] = c;
    }
}

#define FORMAT(...) stdio_printf_to(format_putchar, __VA_ARGS__)

static const char *state_name(process_state_t state)
{
    switch (state)
    {
    case PROC_STATE_NEW:     return "new";
    case PROC_STATE_RUNNING: return "running";
    case PROC_STATE_READY:   return "ready";
    case PROC_STATE_BLOCKED: return "sleeping";
    case PROC_STATE_ZOMBIE:  return "zombie";
    case PROC_STATE_STOPPED: return "stopped";
    default:                 return "unknown";
    }
}

/**
 * make the text of a file into node.
 * @return false if there is no such file (a process that has gone).
 */
static bool make_text(procfs_node_t *node, const char *name, uint32_t pid)
{
    process_info_t info;
    if (pid != 0 && !process_info(pid, &info))
    {
        return false;
    }

    mutex_lock(&format_lock);
    format_target = node->text;
    format_used = 0;

    if (pid != 0)
    {
        FORMAT("Name:\t%s\nState:\t%s\nPid:\t%u\nPPid:\t%u\nUid:\t%u\nAreas:\t%u\n"
               "Fds:\t%u\nBrk:\t0x%lx\nVmRSS:\t%u kB\nUtime:\t%u\nStime:\t%u\n",
               info.name, state_name(info.state), info.pid, info.parent_pid, info.uid,
               info.areas, info.open_fds, (unsigned long)info.brk, info.pages * 4, info.utime,
               info.stime);
    }
    else if (strcmp(name, "meminfo") == 0)
    {
        heap_stats_t heap = heap_get_stats();
        FORMAT("MemTotal:\t%u kB\nMemFree:\t%u kB\nHeapUsed:\t%lu kB\nHeapPeak:\t%lu kB\n",
               pmm_total_count() * 4, pmm_free_count() * 4,
               (unsigned long)(heap.current_usage / 1024), (unsigned long)(heap.peak_usage / 1024));
        if (block_cache_root)
        {
            FORMAT("CacheHits:\t%u sectors\nCacheMisses:\t%u sectors\n", block_cache_root->hits,
                   block_cache_root->misses);
        }
    }
    else if (strcmp(name, "pci") == 0)
    {
        // bus:device.function, class, vendor:device, the first memory BAR
        for (uint32_t i = 0; i < pci_device_count(); i++)
        {
            const pci_device_t *dev = pci_device_at(i);
            FORMAT("%02x:%02x.%u %02x%02x%02x %04x:%04x irq %u %s\n", dev->bus, dev->device, dev->function,
                   dev->class_code, dev->subclass, dev->prog_if, dev->vendor_id, dev->device_id,
                   dev->irq_line, pci_class_name(dev));
        }
    }
    else if (strcmp(name, "audio") == 0)
    {
        audio_status_t audio;
        audio_status(&audio);
        if (!audio.present)
        {
            FORMAT("Device:\tnone\n");
        }
        else
        {
            FORMAT("Device:\t%s\nCodec:\t%08x\nDetail:\t%s\nRate:\t%u Hz\nOpen:\t%u\nPlaying:\t%s\n"
                   "Queued:\t%u bytes\nPlayed:\t%lu bytes\nUnderruns:\t%u\n"
                   "Volume:\t%u %u\n",
                   audio.name, audio.codec_id, audio.detail, audio.rate, audio.open,
                   audio.running ? "yes" : "no", audio.queued, (unsigned long)audio.played,
                   audio.underruns, audio.volume & 0xFF, (audio.volume >> 8) & 0xFF);
        }
    }
    else if (strcmp(name, "interrupts") == 0)
    {
        // the device, the mode, the vector or IRQ line, the count
        pci_irq_info_t irq;
        for (uint32_t i = 0; pci_irq_info(i, &irq); i++)
        {
            FORMAT("%s\t%s\t%u\t%u\n", irq.name, pci_irq_mode_name(irq.mode), irq.number,
                   irq.count);
        }
    }
    else
    {
        // the time since boot, and the part of it the CPU was idle (as Linux)
        uint64_t times[2] = {timer_now(), process_idle_ticks()};
        for (int i = 0; i < 2; i++)
        {
            uint32_t seconds = (uint32_t)(times[i] / TIMER_HZ);
            uint32_t hundredths = (uint32_t)(times[i] % TIMER_HZ) * (100 / TIMER_HZ);
            FORMAT("%u.%u%u%s", seconds, hundredths / 10, hundredths % 10, i ? "\n" : " ");
        }
    }

    node->size = format_used;
    mutex_unlock(&format_lock);
    return true;
}

/**
 * parse a decimal pid of exactly len characters; 0 if it is not one.
 */
static uint32_t parse_pid(const char *text, uint32_t len)
{
    uint32_t pid = 0;
    for (uint32_t i = 0; i < len; i++)
    {
        if (text[i] < '0' || text[i] > '9' || pid > 100000000)
        {
            return 0;
        }
        pid = pid * 10 + (uint32_t)(text[i] - '0');
    }
    return pid;
}

/**
 * resolve a mount-relative path into a node.
 * @return KERR_OK, or KERR_NOENT.
 */
static kerr_t resolve(const char *path, procfs_node_t *node)
{
    while (*path == '/')
    {
        path++;
    }
    if (*path == '\0')
    {
        node->kind = NODE_ROOT;
        return KERR_OK;
    }

    uint32_t len = 0;
    while (path[len] && path[len] != '/')
    {
        len++;
    }
    const char *rest = path + len;
    while (*rest == '/')
    {
        rest++;
    }

    // "self" and "<pid>" are process directories
    uint32_t pid = 0;
    if (len == 4 && strncmp(path, "self", 4) == 0)
    {
        process_t *current = process_current();
        pid = current ? current->pid : 0;
    }
    else
    {
        pid = parse_pid(path, len);
    }

    if (pid != 0)
    {
        process_info_t info;
        if (!process_info(pid, &info))
        {
            return KERR_NOENT;
        }
        if (*rest == '\0')
        {
            node->kind = NODE_PID_DIR;
            node->pid = pid;
            return KERR_OK;
        }
        if (strcmp(rest, "status") != 0)
        {
            return KERR_NOENT;
        }
        node->kind = NODE_TEXT;
        return make_text(node, "status", pid) ? KERR_OK : KERR_NOENT;
    }

    if (*rest != '\0' || (strcmp(path, "meminfo") != 0 && strcmp(path, "uptime") != 0 &&
                           strcmp(path, "pci") != 0 && strcmp(path, "interrupts") != 0 &&
                           strcmp(path, "audio") != 0))
    {
        return KERR_NOENT;
    }
    node->kind = NODE_TEXT;
    make_text(node, path, 0);
    return KERR_OK;
}

static kerr_t procfs_open(fs_mount_t *mount, const char *path, fs_file_t *file)
{
    (void)mount;
    procfs_node_t *node = kmalloc(sizeof(procfs_node_t));
    if (!node)
    {
        return KERR_NOMEM;
    }

    kerr_t err = resolve(path, node);
    if (kerr_failed(err))
    {
        kfree(node);
        return err;
    }

    file->fs_data = node;
    file->fs_pos = 0;
    file->file_type = node->kind == NODE_TEXT ? FS_TYPE_FILE : FS_TYPE_DIR;
    return KERR_OK;
}

static void procfs_close(fs_file_t *file)
{
    kfree(file->fs_data);
    file->fs_data = NULL;
}

static void fill_stat(const procfs_node_t *node, fs_stat_t *stat_out)
{
    memset(stat_out, 0, sizeof(*stat_out));
    stat_out->links_count = 1;
    if (node->kind == NODE_TEXT)
    {
        stat_out->file_type = FS_TYPE_FILE;
        stat_out->mode = S_IFREG | 0444;
        stat_out->size = node->size;
    }
    else
    {
        stat_out->file_type = FS_TYPE_DIR;
        stat_out->mode = S_IFDIR | 0555;
        stat_out->inode = node->kind == NODE_PID_DIR ? node->pid : 1;
    }
}

static kerr_t procfs_stat(fs_mount_t *mount, const char *path, fs_stat_t *stat_out)
{
    (void)mount;
    procfs_node_t *node = kmalloc(sizeof(procfs_node_t));
    if (!node)
    {
        return KERR_NOMEM;
    }
    kerr_t err = resolve(path, node);
    if (kerr_ok(err))
    {
        fill_stat(node, stat_out);
    }
    kfree(node);
    return err;
}

static kerr_t procfs_fstat(fs_file_t *file, fs_stat_t *stat_out)
{
    fill_stat(file->fs_data, stat_out);
    return KERR_OK;
}

static uint32_t procfs_read(fs_file_t *file, uint32_t count, void *data_out)
{
    procfs_node_t *node = file->fs_data;
    if (node->kind != NODE_TEXT || file->fs_pos >= node->size)
    {
        return 0;
    }

    uint32_t n = node->size - file->fs_pos;
    if (n > count)
    {
        n = count;
    }
    memcpy(data_out, node->text + file->fs_pos, n);
    file->fs_pos += n;
    return n;
}

/**
 * fill a directory entry.
 */
static void set_entry(fs_dirent_t *entry, uint32_t inode, uint8_t type, const char *name)
{
    entry->inode = inode;
    entry->file_type = type;
    entry->size = 0;
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
}

static kerr_t procfs_read_entry(fs_file_t *file, fs_dirent_t *entry_out)
{
    procfs_node_t *node = file->fs_data;
    uint32_t index = file->fs_pos;

    if (node->kind == NODE_TEXT)
    {
        return KERR_NOTDIR;
    }

    if (node->kind == NODE_PID_DIR)
    {
        if (index > 0)
        {
            return KERR_NOENT;
        }
        set_entry(entry_out, 2, FS_TYPE_FILE, "status");
        file->fs_pos++;
        return KERR_OK;
    }

    // the root: its files, then one directory per process. the pids are
    // listed again on every call, so a listing follows processes coming
    // and going (a pid is skipped or shown twice at worst).
    if (index < ROOT_FILE_COUNT)
    {
        bool is_dir = strcmp(root_files[index], "self") == 0;
        set_entry(entry_out, index + 2, is_dir ? FS_TYPE_DIR : FS_TYPE_FILE, root_files[index]);
        file->fs_pos++;
        return KERR_OK;
    }

    uint32_t pids[PROCFS_PIDS_MAX];
    uint32_t count = process_list_pids(pids, PROCFS_PIDS_MAX);
    uint32_t i = index - ROOT_FILE_COUNT;
    if (i >= count)
    {
        return KERR_NOENT;
    }

    char name[12];
    uint32_t pid = pids[i];
    uint32_t len = 0;
    char digits[12];
    do
    {
        digits[len++] = (char)('0' + pid % 10);
        pid /= 10;
    } while (pid);
    for (uint32_t d = 0; d < len; d++)
    {
        name[d] = digits[len - 1 - d];
    }
    name[len] = '\0';

    set_entry(entry_out, pids[i], FS_TYPE_DIR, name);
    file->fs_pos++;
    return KERR_OK;
}

static int32_t procfs_seek(fs_file_t *file, int32_t offset, int whence)
{
    procfs_node_t *node = file->fs_data;
    int32_t base = whence == 1 ? (int32_t)file->fs_pos : whence == 2 ? (int32_t)node->size : 0;
    int32_t position = base + offset;
    if (whence < 0 || whence > 2 || position < 0)
    {
        return -EINVAL;
    }
    file->fs_pos = (uint32_t)position;
    return position;
}

/**
 * every change to the namespace or a file is refused.
 */
static kerr_t procfs_refuse_path(fs_mount_t *mount, const char *path)
{
    (void)mount;
    (void)path;
    return KERR_PERM;
}

static kerr_t procfs_refuse_rename(fs_mount_t *mount, const char *old_path, const char *new_path)
{
    (void)mount;
    (void)old_path;
    (void)new_path;
    return KERR_PERM;
}

static uint32_t procfs_refuse_write(fs_file_t *file, uint32_t count, const void *data_in)
{
    (void)file;
    (void)count;
    (void)data_in;
    return 0;
}

static kerr_t procfs_refuse_truncate(fs_file_t *file)
{
    (void)file;
    return KERR_PERM;
}

static const fs_ops_t procfs_ops = {
    .open = procfs_open,
    .create = procfs_refuse_path,
    .mkdir = procfs_refuse_path,
    .remove = procfs_refuse_path,
    .rmdir = procfs_refuse_path,
    .rename = procfs_refuse_rename,
    .stat = procfs_stat,
    .fstat = procfs_fstat,
    .read = procfs_read,
    .write = procfs_refuse_write,
    .truncate = procfs_refuse_truncate,
    .read_entry = procfs_read_entry,
    .close = procfs_close,
    .seek = procfs_seek,
};

kerr_t procfs_mount(fs_mount_t *mount)
{
    if (!mount)
    {
        return KERR_INVAL;
    }
    mount->ops = &procfs_ops;
    mount->fs_data = NULL;
    mount->is_mounted = 1;
    return KERR_OK;
}

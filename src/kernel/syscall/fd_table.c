#include "fd_table.h"

#include "kernel/errno.h"
#include "kernel/process/process.h"
#include "kernel/memory/heap.h"
#include "arch/arch.h"

fd_table_t *fd_table_create(void)
{
    fd_table_t *table = kcalloc(1, sizeof(fd_table_t));
    if (table)
    {
        table->users = 1;
    }
    return table;
}

fd_table_t *fd_table_get(fd_table_t *table)
{
    uint32_t flags = arch_irq_save();
    table->users++;
    arch_irq_restore(flags);
    return table;
}

void fd_table_put(fd_table_t *table)
{
    if (!table)
    {
        return;
    }
    uint32_t flags = arch_irq_save();
    bool last = --table->users == 0;
    arch_irq_restore(flags);
    if (!last)
    {
        return;
    }
    for (int i = 0; i < FD_MAX; i++)
    {
        fd_close_entry(&table->fd[i]);
    }
    kfree(table);
}

int fd_install(process_t *proc, file_t *file, uint32_t fd_flags, int min_fd)
{
    for (int fd = min_fd < 0 ? 0 : min_fd; fd < FD_MAX; fd++)
    {
        if (!proc->files->fd[fd].file)
        {
            proc->files->fd[fd].file = file;
            proc->files->fd[fd].flags = fd_flags;
            return fd;
        }
    }
    return -EMFILE;
}

fd_entry_t *fd_get(int fd)
{
    process_t *proc = process_current();
    if (!proc || !proc->files || fd < 0 || fd >= FD_MAX || !proc->files->fd[fd].file)
    {
        return NULL;
    }
    fd_entry_t *entry = &proc->files->fd[fd];

    // with a shared table another thread can close fd while this syscall
    // sleeps on its file: hold a reference until the syscall ends. a table
    // of one user cannot change meanwhile, so it needs none.
    if (proc->files->users > 1 && proc->held_count < FD_MAX)
    {
        file_ref(entry->file);
        proc->held_files[proc->held_count++] = entry->file;
    }
    return entry;
}

void fd_release_held(process_t *proc)
{
    while (proc->held_count > 0)
    {
        file_unref(proc->held_files[--proc->held_count]);
    }
}

int fd_close(int fd)
{
    fd_entry_t *entry = fd_get(fd);
    if (!entry)
    {
        return -EBADF;
    }
    fd_close_entry(entry);
    return 0;
}

void fd_close_entry(fd_entry_t *entry)
{
    // free the slot first: the release may sleep, and the slot must not
    // look open meanwhile
    file_t *file = entry->file;
    entry->file = NULL;
    entry->flags = 0;
    file_unref(file);
}

void fd_copy_entry(fd_entry_t *dst, const fd_entry_t *src)
{
    dst->file = src->file;
    dst->flags = src->flags;
    if (dst->file)
    {
        file_ref(dst->file);
    }
}

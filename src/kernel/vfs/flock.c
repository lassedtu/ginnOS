#include "flock.h"

#include "kernel/errno.h"
#include "kernel/process/process.h"
#include "kernel/signal/signal.h"
#include "kernel/sync/spinlock.h"
#include "kernel/sync/wait_queue.h"

// most locks held at once on the whole system
#define FLOCK_MAX 128

typedef struct
{
    fs_mount_t *mount; // the file: its filesystem and inode (NULL: free)
    uint32_t inode;
    file_t *holder;    // the open-file description that holds the lock
    bool exclusive;
} flock_record_t;

// a zeroed spinlock and wait queue are ready to use
static flock_record_t records[FLOCK_MAX];
static spinlock_t flock_lock;
static wait_queue_t flock_waiters; // all who wait for some lock to go

/**
 * the record a description holds, or NULL.
 */
static flock_record_t *held_by(const file_t *file)
{
    for (int i = 0; i < FLOCK_MAX; i++)
    {
        if (records[i].mount && records[i].holder == file)
        {
            return &records[i];
        }
    }
    return NULL;
}

/**
 * true when another description holds a lock on the file that the asked
 * lock may not share.
 */
static bool conflicts(const flock_record_t *key, bool exclusive)
{
    for (int i = 0; i < FLOCK_MAX; i++)
    {
        const flock_record_t *r = &records[i];
        if (r->mount == key->mount && r->inode == key->inode && r->holder != key->holder &&
            (r->exclusive || exclusive))
        {
            return true;
        }
    }
    return false;
}

static flock_record_t *free_record(void)
{
    for (int i = 0; i < FLOCK_MAX; i++)
    {
        if (!records[i].mount)
        {
            return &records[i];
        }
    }
    return NULL;
}

int32_t flock_apply(file_t *file, uint32_t operation)
{
    uint32_t kind = operation & ~(uint32_t)LOCK_NB;
    if (kind != LOCK_SH && kind != LOCK_EX && kind != LOCK_UN)
    {
        return -EINVAL;
    }
    if (kind == LOCK_UN)
    {
        flock_release(file);
        return 0;
    }

    // the inode, read before the spinlock (a filesystem may sleep)
    fs_stat_t st;
    kerr_t err = vfs_fstat(&file->vfs, &st);
    if (kerr_failed(err))
    {
        return kerr_to_errno(err);
    }
    flock_record_t key = {.mount = file->vfs.mount, .inode = st.inode, .holder = file};
    bool exclusive = kind == LOCK_EX;

    uint32_t flags = spin_lock_irqsave(&flock_lock);
    int32_t result = 0;
    for (;;)
    {
        if (!conflicts(&key, exclusive))
        {
            flock_record_t *r = held_by(file);
            if (!r)
            {
                r = free_record();
            }
            if (!r)
            {
                result = -ENOLCK;
                break;
            }
            *r = key;
            r->exclusive = exclusive;
            // a shared lock made from an exclusive one lets others in
            wait_queue_wake_all(&flock_waiters);
            break;
        }
        if (operation & LOCK_NB)
        {
            result = -EWOULDBLOCK;
            break;
        }
        if (signal_pending(process_current()))
        {
            result = -ERESTARTSYS;
            break;
        }
        wait_queue_block_locked_interruptible(&flock_waiters, &flock_lock);
    }
    spin_unlock_irqrestore(&flock_lock, flags);
    return result;
}

void flock_release(file_t *file)
{
    uint32_t flags = spin_lock_irqsave(&flock_lock);
    flock_record_t *r = held_by(file);
    if (r)
    {
        r->mount = NULL;
        r->holder = NULL;
        wait_queue_wake_all(&flock_waiters);
    }
    spin_unlock_irqrestore(&flock_lock, flags);
}

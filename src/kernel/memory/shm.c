/**
 * shared memory objects (see shm.h).
 *
 * the object holds one reference to each of its frames. a mapping adds a
 * reference per frame (arch_map_page with MMU_FLAG_SHARED, so fork shares
 * the page writable instead of copying it); unmapping and process exit
 * drop it through the normal paths (mm_unmap, the address space teardown).
 */

#include "shm.h"

#include "common/memory.h"
#include "kernel/errno.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/mman.h"
#include "kernel/memory/page.h"
#include "kernel/memory/phys.h"
#include "kernel/memory/pmm.h"
#include "kernel/vfs/file.h"
#include "kernel/vfs/stat.h"

typedef struct
{
    uint32_t pages;
    void **frames; // the kernel address of each page
} shm_object_t;

static void free_object(shm_object_t *object)
{
    for (uint32_t i = 0; i < object->pages; i++)
    {
        if (object->frames[i])
        {
            pmm_free_page(object->frames[i]);
        }
    }
    kfree(object->frames);
    kfree(object);
}

static int32_t shm_mmap(file_t *file, addr_space_t as, uintptr_t virt, size_t length, int prot,
                        uint32_t offset)
{
    shm_object_t *object = file->device_data;
    uint32_t first = offset / PAGE_SIZE;
    uint32_t count = (uint32_t)(length / PAGE_SIZE);
    if (first > object->pages || count > object->pages - first)
    {
        return -EINVAL; // past the end of the object
    }
    if ((prot & PROT_WRITE) && !file_writable(file))
    {
        return -EACCES;
    }

    uint32_t flags = MMU_FLAG_PRESENT | MMU_FLAG_USER | MMU_FLAG_SHARED;
    if (prot & PROT_WRITE)
    {
        flags |= MMU_FLAG_WRITE;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        void *frame = object->frames[first + i];
        pmm_page_ref(frame); // the mapping's own reference
        if (arch_map_page(as, virt + i * PAGE_SIZE, virt_to_phys(frame), flags) != 0)
        {
            pmm_free_page(frame);
            return -ENOMEM; // the caller removes the area and what is mapped
        }
    }
    return 0;
}

static int32_t shm_stat(file_t *file, struct kstat *st)
{
    shm_object_t *object = file->device_data;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0600;
    st->st_nlink = 1;
    st->st_size = (int32_t)(object->pages * PAGE_SIZE);
    st->st_blksize = PAGE_SIZE;
    st->st_blocks = object->pages * (PAGE_SIZE / 512);
    return 0;
}

static void shm_release(file_t *file)
{
    free_object(file->device_data);
    kfree(file);
}

static const file_ops_t shm_file_ops = {
    .mmap = shm_mmap,
    .stat = shm_stat,
    .release = shm_release,
};

kerr_t shm_create(size_t size, file_t **out)
{
    if (size == 0 || size > SHM_MAX_SIZE)
    {
        return KERR_INVAL;
    }

    shm_object_t *object = kmalloc(sizeof(*object));
    if (!object)
    {
        return KERR_NOMEM;
    }
    object->pages = (uint32_t)(PAGE_ALIGN_UP(size) / PAGE_SIZE);
    object->frames = kmalloc(object->pages * sizeof(void *));
    if (!object->frames)
    {
        kfree(object);
        return KERR_NOMEM;
    }
    memset(object->frames, 0, object->pages * sizeof(void *));

    for (uint32_t i = 0; i < object->pages; i++)
    {
        object->frames[i] = pmm_alloc_page();
        if (!object->frames[i])
        {
            free_object(object);
            return KERR_NOMEM;
        }
        memset(object->frames[i], 0, PAGE_SIZE);
    }

    file_t *file = file_new(&shm_file_ops, O_RDWR);
    if (!file)
    {
        free_object(object);
        return KERR_NOMEM;
    }
    file->device_data = object;
    *out = file;
    return KERR_OK;
}

bool shm_is_file(const file_t *file)
{
    return file && file->ops == &shm_file_ops;
}

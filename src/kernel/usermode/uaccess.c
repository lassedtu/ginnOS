#include "uaccess.h"

#include "arch/arch.h"
#include "common/memory.h"
#include "kernel/memory/memory_layout.h"
#include "kernel/process/process.h"

#define UACCESS_PAGE_SIZE 4096u

bool user_access_ok(const void *ptr, uint32_t len, bool write)
{
    if (len == 0)
    {
        return true;
    }

    // cheap bounds first: user space only, no wrap-around
    if (!is_user_ptr(ptr, len))
    {
        return false;
    }

    process_t *proc = process_current();
    if (!proc || !proc->page_directory)
    {
        return false;
    }

    return arch_user_access_ok(proc->page_directory, (uint32_t)ptr, len, write);
}

kerr_t copy_from_user(void *dst, const void *user_src, uint32_t len)
{
    if (!user_access_ok(user_src, len, false))
    {
        return KERR_FAULT;
    }

    memcpy(dst, user_src, len);
    return KERR_OK;
}

kerr_t copy_to_user(void *user_dst, const void *src, uint32_t len)
{
    if (!user_access_ok(user_dst, len, true))
    {
        return KERR_FAULT;
    }

    memcpy(user_dst, src, len);
    return KERR_OK;
}

int32_t strncpy_from_user(char *dst, const char *user_src, uint32_t size)
{
    if (size == 0)
    {
        return KERR_RANGE;
    }

    uint32_t addr = (uint32_t)user_src;
    uint32_t checked_end = addr; // bytes before this address are known readable

    for (uint32_t i = 0; i < size; i++)
    {
        if (addr + i >= checked_end)
        {
            // check the rest of this page before reading any of it
            uint32_t page_end = ((addr + i) & ~(UACCESS_PAGE_SIZE - 1)) + UACCESS_PAGE_SIZE;
            if (!user_access_ok((const void *)(addr + i), page_end - (addr + i), false))
            {
                return KERR_FAULT;
            }
            checked_end = page_end;
        }

        dst[i] = user_src[i];
        if (dst[i] == '\0')
        {
            return (int32_t)i;
        }
    }

    dst[size - 1] = '\0';
    return KERR_RANGE;
}

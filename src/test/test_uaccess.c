#include "ktest.h"

#include "arch/arch.h"
#include "common/memory.h"
#include "kernel/memory/page.h"
#include "kernel/memory/pmm.h"
#include "kernel/process/process.h"
#include "kernel/usermode/uaccess.h"

// a user range for the test: page 0 writable, page 1 read-only, page 2 unmapped
#define UA_BASE 0x40000000u

static uint32_t kernel_value = 0x1234;

/**
 * create a process with the test mappings, make it current and switch to it.
 */
static process_t *enter_test_process(void)
{
    process_t *proc = process_create();
    if (!proc)
        return NULL;

    void *rw = pmm_alloc_page();
    void *ro = pmm_alloc_page();
    if (!rw || !ro)
        return NULL;
    memset(rw, 0, PAGE_SIZE);
    // no NUL anywhere except after "readonly": a string near its end runs
    // into the unmapped page
    memset(ro, 'r', PAGE_SIZE);
    memcpy(ro, "readonly", 9);

    arch_map_page(proc->page_directory, UA_BASE, (uint32_t)rw, MMU_USER_RW);
    arch_map_page(proc->page_directory, UA_BASE + PAGE_SIZE, (uint32_t)ro,
                  MMU_FLAG_PRESENT | MMU_FLAG_USER);

    process_set_current(proc);
    arch_switch_address_space(proc->page_directory);
    return proc;
}

/**
 * leave the test process and free it (its mapped frames go with it).
 */
static void leave_test_process(process_t *proc)
{
    arch_switch_address_space(arch_kernel_address_space());
    process_set_current(NULL);
    process_destroy(proc);
}

KTEST(uaccess_checks_mappings)
{
    uint32_t free_before = pmm_free_count();
    process_t *proc = enter_test_process();
    KEXPECT(proc != NULL);
    if (!proc)
        return;

    char buf[16];

    // writable page: both directions work
    KEXPECT(kerr_ok(copy_to_user((void *)UA_BASE, "hello", 6)));
    KEXPECT(kerr_ok(copy_from_user(buf, (const void *)UA_BASE, 6)));
    KEXPECT(memcmp(buf, "hello", 6) == 0);

    // read-only page: readable, not writable
    KEXPECT(kerr_ok(copy_from_user(buf, (const void *)(UA_BASE + PAGE_SIZE), 9)));
    KEXPECT_EQ(copy_to_user((void *)(UA_BASE + PAGE_SIZE), "x", 1), KERR_FAULT);

    // a range running from a mapped page into the unmapped one
    KEXPECT(user_access_ok((const void *)(UA_BASE + 2 * PAGE_SIZE - 4), 4, false));
    KEXPECT(!user_access_ok((const void *)(UA_BASE + 2 * PAGE_SIZE - 4), 8, false));
    KEXPECT_EQ(copy_from_user(buf, (const void *)(UA_BASE + 2 * PAGE_SIZE), 1), KERR_FAULT);

    // kernel memory and null are never user memory
    KEXPECT_EQ(copy_from_user(buf, &kernel_value, sizeof(kernel_value)), KERR_FAULT);
    KEXPECT_EQ(copy_to_user(&kernel_value, "x", 1), KERR_FAULT);
    KEXPECT_EQ(copy_from_user(buf, NULL, 1), KERR_FAULT);
    KEXPECT_EQ(kernel_value, 0x1234);

    leave_test_process(proc);
    KEXPECT_EQ(pmm_free_count(), free_before);
}

KTEST(uaccess_strncpy_from_user)
{
    process_t *proc = enter_test_process();
    KEXPECT(proc != NULL);
    if (!proc)
        return;

    char buf[16];

    // a string that fits
    copy_to_user((void *)UA_BASE, "path", 5);
    KEXPECT_EQ(strncpy_from_user(buf, (const char *)UA_BASE, sizeof(buf)), 4);
    KEXPECT(memcmp(buf, "path", 5) == 0);

    // longer than the buffer
    copy_to_user((void *)UA_BASE, "0123456789abcdefXYZ", 20);
    KEXPECT_EQ(strncpy_from_user(buf, (const char *)UA_BASE, sizeof(buf)), KERR_RANGE);

    // no NUL before the mapping ends: three readable 'r' bytes, then the
    // unmapped page
    KEXPECT_EQ(strncpy_from_user(buf, (const char *)(UA_BASE + 2 * PAGE_SIZE - 3), sizeof(buf)),
               KERR_FAULT);

    // a string in the read-only page reads fine
    KEXPECT_EQ(strncpy_from_user(buf, (const char *)(UA_BASE + PAGE_SIZE), sizeof(buf)), 8);

    // kernel memory is rejected
    KEXPECT_EQ(strncpy_from_user(buf, (const char *)&kernel_value, sizeof(buf)), KERR_FAULT);

    leave_test_process(proc);
}

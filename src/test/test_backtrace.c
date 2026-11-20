#include "ktest.h"

#include "arch/arch.h"
#include "common/stddef.h"
#include "kernel/debug/ksyms.h"

KTEST(ksyms_lookup_finds_functions)
{
    uintptr_t addr = (uintptr_t)&ksym_lookup;
    uintptr_t offset = 99;

    KEXPECT_STREQ(ksym_lookup(addr, &offset), "ksym_lookup");
    KEXPECT_EQ(offset, 0);

    // an address inside a function maps to that function plus an offset.
    KEXPECT_STREQ(ksym_lookup(addr + 1, &offset), "ksym_lookup");
    KEXPECT_EQ(offset, 1);

    // static functions are in the table too.
    KEXPECT_STREQ(ksym_lookup((uintptr_t)&ktest_fn_ksyms_lookup_finds_functions, NULL),
                  "ktest_fn_ksyms_lookup_finds_functions");
}

KTEST(ksyms_lookup_rejects_non_code)
{
    KEXPECT(ksym_lookup((uintptr_t)&ksym_count, NULL) == NULL);
    KEXPECT(ksym_lookup(0, NULL) == NULL);
    KEXPECT(ksym_lookup(0xFFFFFFF0u, NULL) == NULL);
    KEXPECT(ksym_count > 100);
}

// the walker test needs real frames, so these must not be inlined or
// turned into tail calls (the empty asm after each call prevents that).
#define BT_DEPTH 8

static int walk_depth;
static uintptr_t walk_pcs[BT_DEPTH];

__attribute__((noinline)) static void bt_inner(void)
{
    walk_depth = arch_stack_walk((uintptr_t)__builtin_frame_address(0), walk_pcs, BT_DEPTH);
    __asm__ volatile("" ::: "memory");
}

__attribute__((noinline)) static void bt_outer(void)
{
    bt_inner();
    __asm__ volatile("" ::: "memory");
}

static const char *frame_name(int i)
{
    // look up pc - 1 so the call instruction is what gets attributed.
    return i < walk_depth ? ksym_lookup(walk_pcs[i] - 1, NULL) : NULL;
}

KTEST(backtrace_walks_frame_pointers)
{
    bt_outer();
    __asm__ volatile("" ::: "memory");

    KEXPECT(walk_depth >= 3);
    KEXPECT_STREQ(frame_name(0), "bt_outer");
    KEXPECT_STREQ(frame_name(1), "ktest_fn_backtrace_walks_frame_pointers");
    KEXPECT_STREQ(frame_name(2), "ktest_run_all");
}

KTEST(backtrace_stops_on_bad_frames)
{
    uintptr_t pcs[4];

    KEXPECT_EQ(arch_stack_walk(0, pcs, 4), 0);
    // misaligned and unmapped frames are rejected rather than followed.
    KEXPECT_EQ(arch_stack_walk(0x1003u, pcs, 4), 0);
    KEXPECT_EQ(arch_stack_walk(0xFFFFF000u, pcs, 4), 0);
}

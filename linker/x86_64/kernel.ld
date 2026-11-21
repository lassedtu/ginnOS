/* the x86_64 kernel. limine loads it at any physical address and maps it
 * at KERNEL_IMAGE_BASE (kernel/memory/memory_layout.h). the limine boot
 * protocol needs the kernel at or above 0xffffffff80000000. */

ENTRY(_start)

KERNEL_IMAGE_BASE = 0xFFFFFFFF80000000;

PHDRS
{
    text   PT_LOAD FLAGS(5);  /* read, execute */
    rodata PT_LOAD FLAGS(4);  /* read */
    data   PT_LOAD FLAGS(6);  /* read, write */
}

SECTIONS
{
    . = KERNEL_IMAGE_BASE;
    kernel_start = .;

    .text :
    {
        __text_start = .;
        *(.text*)
        __text_end = .;
    } :text

    . = ALIGN(0x1000);

    .rodata :
    {
        *(.rodata*)
    } :rodata

    .shell_cmds :
    {
        __start_shell_cmds = .;
        KEEP(*(.shell_cmds))
        __stop_shell_cmds = .;
    } :rodata

    /* in-kernel test descriptors (src/test, KTEST macro) */
    .ktests ALIGN(8) :
    {
        __start_ktests = .;
        KEEP(*(.ktests))
        __stop_ktests = .;
    } :rodata

    /* symbol table for backtraces (tools/gen_ksyms.py). it comes after
     * .text, so its size does not move a function between the two links */
    .ksyms ALIGN(8) :
    {
        KEEP(*(.ksyms))
    } :rodata

    . = ALIGN(0x1000);

    .data :
    {
        /* limine finds its requests between the two markers */
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
        *(.data*)
    } :data

    .bss :
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } :data

    kernel_end = .;

    /DISCARD/ :
    {
        *(.eh_frame*)
        *(.note*)
        *(.comment)
    }
}

ENTRY(_start)

/* stage2 loads kernel.bin at this physical address and calls it. a
 * multiboot 2 bootloader loads kernel.elf at the same address. */
KERNEL_PHYS_BASE = 0x10000;

/* the kernel runs in the higher half: RAM is direct-mapped from here, so
 * physical address P is at virtual P + KERNEL_VIRT_BASE (kernel/memory/phys.h) */
KERNEL_VIRT_BASE = 0xC0000000;

PHDRS
{
    boot PT_LOAD FLAGS(5);   /* the boot page directory is only read (by the MMU) */
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    /* the entry stub and its boot page directory run before paging is on, so
     * they are linked at their physical address (main.asm) */
    . = KERNEL_PHYS_BASE;
    .boot :
    {
        *(.boot.text)
        *(.boot.header)
        *(.boot.data)
    } :boot

    /* everything else is linked high but loaded right after .boot: kernel.bin
     * keeps the same layout it always had */
    . += KERNEL_VIRT_BASE;

    /* link address of the start of the image (physical KERNEL_PHYS_BASE) */
    kernel_start = KERNEL_PHYS_BASE + KERNEL_VIRT_BASE;

    .text : AT(ADDR(.text) - KERNEL_VIRT_BASE)
    {
        __text_start = .;
        *(.text*)
        __text_end = .;
    } :text

    .rodata : AT(ADDR(.rodata) - KERNEL_VIRT_BASE)
    {
        *(.rodata*)
    } :text

    .shell_cmds : AT(ADDR(.shell_cmds) - KERNEL_VIRT_BASE)
    {
        __start_shell_cmds = .;
        KEEP(*(.shell_cmds))
        __stop_shell_cmds = .;
    } :text

    /* in-kernel test descriptors (src/test, KTEST macro). empty in the
     * normal kernel; only the make test kernel links any tests in. */
    .ktests ALIGN(4) : AT(ADDR(.ktests) - KERNEL_VIRT_BASE)
    {
        __start_ktests = .;
        KEEP(*(.ktests))
        __stop_ktests = .;
    } :text

    /* symbol table for backtraces (tools/gen_ksyms.py). it must come after
     * .text: the kernel is linked twice and the table's size must not move
     * any function between the two passes. */
    .ksyms ALIGN(4) : AT(ADDR(.ksyms) - KERNEL_VIRT_BASE)
    {
        KEEP(*(.ksyms))
    } :text

    .data : AT(ADDR(.data) - KERNEL_VIRT_BASE)
    {
        *(.data*)
    } :data

    /* not in kernel.bin: the entry stub zeroes it before any C runs */
    .bss : AT(ADDR(.bss) - KERNEL_VIRT_BASE)
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } :data

    kernel_end = .;
}

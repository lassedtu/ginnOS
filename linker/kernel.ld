ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x10000;
    kernel_start = .;

    .text :
    {
        __text_start = .;
        *(.text*)
        __text_end = .;
    } :text

    .rodata :
    {
        *(.rodata*)
    } :text

    .shell_cmds :
    {
        __start_shell_cmds = .;
        KEEP(*(.shell_cmds))
        __stop_shell_cmds = .;
    } :text

    /* in-kernel test descriptors (src/test, KTEST macro). empty in the
     * normal kernel; only the make test kernel links any tests in. */
    .ktests : ALIGN(4)
    {
        __start_ktests = .;
        KEEP(*(.ktests))
        __stop_ktests = .;
    } :text

    /* symbol table for backtraces (tools/gen_ksyms.py). it must come after
     * .text: the kernel is linked twice and the table's size must not move
     * any function between the two passes. */
    .ksyms : ALIGN(4)
    {
        KEEP(*(.ksyms))
    } :text

    .data :
    {
        *(.data*)
    } :data

    /* not in kernel.bin: the entry stub zeroes it before any C runs */
    .bss :
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } :data

    kernel_end = .;
}

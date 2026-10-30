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
        *(.text*)
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

    .data :
    {
        *(.data*)
    } :data

    .bss :
    {
        *(.bss*)
        *(COMMON)
    } :data

    kernel_end = .;
}

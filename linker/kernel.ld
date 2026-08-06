ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x10000;

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

    .data :
    {
        *(.data*)
    } :data

    .bss :
    {
        *(.bss*)
        *(COMMON)
    } :data
}

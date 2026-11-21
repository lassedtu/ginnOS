ENTRY(_start)

/* code and constants in one read+execute segment, data and bss in a
 * read+write one starting on its own page, so the two can be mapped with
 * different permissions */
PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    /* USER_LOAD_ADDR in kernel/memory/memory_layout.h */
    . = 0x00400000;

    .text :
    {
        *(.text*)
    } :text

    .rodata :
    {
        *(.rodata*)
    } :text

    . = ALIGN(0x1000);

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

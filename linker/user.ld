ENTRY(_start)

/* code and constants in one read+execute segment, data and bss in a
 * read+write one starting on its own page, so the two can be mapped with
 * different permissions. the first segment also holds the ELF and
 * program headers: libc finds the TLS template (PT_TLS) through them
 * (__ehdr_start). */
PHDRS
{
    text PT_LOAD FILEHDR PHDRS FLAGS(5);
    data PT_LOAD FLAGS(6);
    tls PT_TLS;
}

SECTIONS
{
    /* USER_LOAD_ADDR in kernel/memory/memory_layout.h */
    . = 0x00400000 + SIZEOF_HEADERS;

    .text :
    {
        *(.text*)
    } :text

    .rodata :
    {
        *(.rodata*)
    } :text

    . = ALIGN(0x1000);

    /* the template of each thread's thread-local storage */
    .tdata :
    {
        *(.tdata .tdata.*)
    } :data :tls

    .tbss :
    {
        *(.tbss .tbss.*)
    } :data :tls

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

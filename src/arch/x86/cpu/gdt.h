#ifndef GDT_H
#define GDT_H

/** kernel code segment selector offset in the GDT. */
#define GDT_CODE_SEGMENT 0x08

/** kernel data segment selector offset in the GDT. */
#define GDT_DATA_SEGMENT 0x10

/**
 * initialize the Global Descriptor Table (GDT) and load it into the GDTR register.
 * sets up a flat memory model with code and data segments.
 * also sets up a Task State Segment (TSS) for handling privilege level transitions.
 * @return void
 */
void gdt_initialize(void);

#endif
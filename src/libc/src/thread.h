/**
 * @file thread.h
 * @brief libc's own view of a thread: its control block, which the thread
 *        pointer points to (%fs:0 on x86_64, %gs:0 on i686).
 */

#ifndef LIBC_THREAD_H
#define LIBC_THREAD_H

#include <stddef.h>
#include <stdint.h>

/**
 * the thread control block. the thread's TLS block lies just below it
 * (x86 "variant II": GCC's code reaches a TLS variable at a negative
 * offset from the thread pointer). the first word points to the block
 * itself, as GCC's code reads the thread pointer from %fs:0 (%gs:0).
 * canary is where GCC's stack protector looks (%fs:0x28 and %gs:0x14:
 * the sixth word on both).
 */
struct thread
{
    struct thread *self;
    void *dtv;         // unused: there are no dynamic modules
    void *unused[3];
    uintptr_t canary;
    void *area;        // the memory of the TLS block and this block
    size_t area_size;
};

/** the calling thread's control block */
static inline struct thread *thread_self(void)
{
    struct thread *self;
#ifdef __x86_64__
    __asm__("mov %%fs:0, %0" : "=r"(self));
#else
    __asm__("mov %%gs:0, %0" : "=r"(self));
#endif
    return self;
}

/**
 * the bytes one thread needs for its TLS block and control block,
 * alignment included.
 */
size_t __tls_area_size(void);

/**
 * set up a thread's TLS block (a copy of the program's template, the rest
 * zeroed) and control block in area.
 * @param area __tls_area_size() bytes.
 * @return the control block: the new thread's thread pointer.
 */
struct thread *__tls_setup(void *area);

/**
 * give the first thread its TLS: called by crt0 before main.
 */
void __init_tls(void);

#endif

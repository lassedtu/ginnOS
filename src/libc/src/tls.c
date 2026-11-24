/**
 * @file tls.c
 * @brief thread-local storage: each thread's copy of the program's TLS
 *        template (the PT_TLS segment), and its control block.
 *
 * the template is found through the program headers, which the first
 * loaded segment holds; the linker gives their place as __ehdr_start.
 */

#include "thread.h"

#include <string.h>
#include <sys/mman.h>
#include <syscall.h>

#define PT_TLS 7

// the parts of the ELF header and of a program header that are read here
// (the address-sized fields are 32 or 64 bits wide, as a pointer is)
typedef struct
{
    unsigned char ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uintptr_t entry;
    uintptr_t phoff;
    uintptr_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
} elf_header_t;

#ifdef __x86_64__
typedef struct
{
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} program_header_t;
#else
typedef struct
{
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
} program_header_t;
#endif

extern const elf_header_t __ehdr_start __attribute__((weak));

/** the template: what to copy, how much of the block that is, the block */
static struct
{
    const void *data;
    size_t data_size;
    size_t size;  // the block, rounded up to align
    size_t align;
} template_ = {NULL, 0, 0, sizeof(void *)};

static size_t round_up(size_t value, size_t align)
{
    return (value + align - 1) & ~(align - 1);
}

/** read the PT_TLS segment, if the program has one */
static void find_template(void)
{
    const elf_header_t *header = &__ehdr_start;
    if (!header)
    {
        return;
    }
    const char *headers = (const char *)header + header->phoff;
    for (uint16_t i = 0; i < header->phnum; i++)
    {
        const program_header_t *ph = (const program_header_t *)(headers + i * header->phentsize);
        if (ph->type == PT_TLS)
        {
            size_t align = ph->align > template_.align ? ph->align : template_.align;
            template_.data = (const void *)(uintptr_t)ph->vaddr;
            template_.data_size = ph->filesz;
            template_.align = align;
            template_.size = round_up(ph->memsz, align);
            return;
        }
    }
}

/** the alignment of the thread pointer */
static size_t tp_align(void)
{
    size_t align = _Alignof(struct thread);
    return template_.align > align ? template_.align : align;
}

size_t __tls_area_size(void)
{
    return template_.size + sizeof(struct thread) + tp_align();
}

struct thread *__tls_setup(void *area)
{
    // the thread pointer is aligned, with the TLS block just below it
    uintptr_t tp = round_up((uintptr_t)area + template_.size, tp_align());
    char *block = (char *)tp - template_.size;
    memcpy(block, template_.data, template_.data_size);
    memset(block + template_.data_size, 0, template_.size - template_.data_size);

    struct thread *self = (struct thread *)tp;
    memset(self, 0, sizeof(*self));
    self->self = self;
    self->area = area;
    self->area_size = __tls_area_size();
    return self;
}

void __init_tls(void)
{
    find_template();
    size_t size = __tls_area_size();
    void *area = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (area == MAP_FAILED)
    {
        _syscall(SYS_EXIT_GROUP, 127, 0, 0, 0, 0); // no memory before main
    }
    struct thread *self = __tls_setup(area);
    self->tid = (int)_syscall(SYS_GETTID, 0, 0, 0, 0, 0);
    _syscall(SYS_SET_TLS, (long)self, 0, 0, 0, 0);
}

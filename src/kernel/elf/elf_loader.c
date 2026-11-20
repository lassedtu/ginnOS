#include "elf_loader.h"
#include "elf.h"

#include "kernel/vfs/vfs.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/heap.h"
#include "arch/arch.h"
#include "common/memory.h"
#include "kernel/memory/page.h"
#include "kernel/memory/mman.h"

/**
 * validate an ELF32 header for a static i686 executable.
 */
static bool elf_validate(const Elf32_Ehdr *ehdr)
{
    if (ehdr->e_ident[EI_MAG0] != ELF_MAGIC_0 ||
        ehdr->e_ident[EI_MAG1] != ELF_MAGIC_1 ||
        ehdr->e_ident[EI_MAG2] != ELF_MAGIC_2 ||
        ehdr->e_ident[EI_MAG3] != ELF_MAGIC_3)
    {
        return false;
    }

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS32)
    {
        return false;
    }

    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB)
    {
        return false;
    }

    if (ehdr->e_type != ET_EXEC)
    {
        return false;
    }

    if (ehdr->e_machine != EM_386)
    {
        return false;
    }

    if (ehdr->e_phnum == 0 || ehdr->e_phentsize < sizeof(Elf32_Phdr))
    {
        return false;
    }

    return true;
}

/**
 * align an address up to the next page boundary.
 */
static uintptr_t page_align_up(uintptr_t addr)
{
    return PAGE_ALIGN_UP(addr);
}

/**
 * map a range of virtual pages as user-accessible in the given page directory,
 * allocating physical frames. pages already mapped are skipped.
 */
static bool map_user_range(addr_space_t as, uintptr_t vaddr_start, uintptr_t vaddr_end)
{
    uintptr_t page;

    vaddr_start = PAGE_ALIGN_DOWN(vaddr_start);
    vaddr_end = page_align_up(vaddr_end);

    for (page = vaddr_start; page < vaddr_end; page += PAGE_SIZE)
    {
        void *frame = pmm_alloc_page();
        if (!frame)
        {
            return false;
        }

        memset(frame, 0, PAGE_SIZE);
        arch_map_page(as, page, virt_to_phys(frame), MMU_USER_RW);
    }

    return true;
}

/**
 * copy data into a process's virtual address space.
 * resolves each page through the process's address space and writes
 * to the physical frame via identity mapping.
 */
static void copy_to_process(addr_space_t as, uintptr_t vaddr, const void *src, size_t size)
{
    const uint8_t *s = (const uint8_t *)src;

    while (size > 0)
    {
        phys_addr_t phys = arch_translate_in(as, vaddr);
        size_t offset_in_page = vaddr & PAGE_OFFSET_MASK;
        size_t chunk = PAGE_SIZE - offset_in_page;
        if (chunk > size)
        {
            chunk = size;
        }

        memcpy(phys_to_virt(phys), s, chunk);

        vaddr += chunk;
        s += chunk;
        size -= chunk;
    }
}

/**
 * zero a range in a process's virtual address space.
 */
static void zero_in_process(addr_space_t as, uintptr_t vaddr, size_t size)
{
    while (size > 0)
    {
        phys_addr_t phys = arch_translate_in(as, vaddr);
        size_t offset_in_page = vaddr & PAGE_OFFSET_MASK;
        size_t chunk = PAGE_SIZE - offset_in_page;
        if (chunk > size)
        {
            chunk = size;
        }

        memset(phys_to_virt(phys), 0, chunk);

        vaddr += chunk;
        size -= chunk;
    }
}

bool elf_load(const char *path, addr_space_t as, vma_table_t *vmas, elf_load_result_t *result)
{
    vfs_file_t file;
    vfs_stat_t stat;
    uint8_t *file_data;
    uint32_t file_size;
    Elf32_Ehdr *ehdr;
    Elf32_Phdr *phdr;
    uintptr_t highest_addr;
    uint32_t i;

    if (!path || !result)
    {
        return false;
    }

    /* stat the file to get its size */
    if (kerr_failed(vfs_stat(path, &stat)))
    {
        return false;
    }

    file_size = stat.size;
    if (file_size < sizeof(Elf32_Ehdr))
    {
        return false;
    }

    /* open the file */
    if (kerr_failed(vfs_open(path, &file)))
    {
        return false;
    }

    /* read entire file into kernel heap */
    file_data = (uint8_t *)kmalloc(file_size);
    if (!file_data)
    {
        vfs_close(&file);
        return false;
    }

    uint32_t bytes_read = vfs_read(&file, file_size, file_data);
    vfs_close(&file);

    if (bytes_read != file_size)
    {
        kfree(file_data);
        return false;
    }

    /* validate ELF header */
    ehdr = (Elf32_Ehdr *)file_data;
    if (!elf_validate(ehdr))
    {
        kfree(file_data);
        return false;
    }

    /* load PT_LOAD segments */
    highest_addr = 0;

    for (i = 0; i < ehdr->e_phnum; i++)
    {
        phdr = (Elf32_Phdr *)(file_data + ehdr->e_phoff + i * ehdr->e_phentsize);

        if (phdr->p_type != PT_LOAD)
        {
            continue;
        }

        if (phdr->p_memsz == 0)
        {
            continue;
        }

        uintptr_t seg_start = phdr->p_vaddr;
        uintptr_t seg_end = seg_start + phdr->p_memsz;

        /* record the segment as an area; two segments sharing a page would
         * overwrite each other's frames, so overlap fails the load */
        uint32_t prot = ((phdr->p_flags & PF_R) ? PROT_READ : 0) |
                        ((phdr->p_flags & PF_W) ? PROT_WRITE : 0) |
                        ((phdr->p_flags & PF_X) ? PROT_EXEC : 0);
        if (kerr_failed(vma_insert(vmas, PAGE_ALIGN_DOWN(seg_start), PAGE_ALIGN_UP(seg_end),
                                   prot, VMA_ELF)))
        {
            kfree(file_data);
            return false;
        }

        /* allocate and map pages for this segment */
        if (!map_user_range(as, seg_start, seg_end))
        {
            kfree(file_data);
            return false;
        }

        /* copy file data into the mapped pages */
        if (phdr->p_filesz > 0)
        {
            if (phdr->p_offset + phdr->p_filesz > file_size)
            {
                kfree(file_data);
                return false;
            }

            copy_to_process(as, seg_start, file_data + phdr->p_offset, phdr->p_filesz);
        }

        /* zero BSS region (memsz > filesz) */
        if (phdr->p_memsz > phdr->p_filesz)
        {
            zero_in_process(as, seg_start + phdr->p_filesz,
                           phdr->p_memsz - phdr->p_filesz);
        }

        /* track highest loaded address for program break */
        if (seg_end > highest_addr)
        {
            highest_addr = seg_end;
        }
    }

    if (highest_addr == 0)
    {
        kfree(file_data);
        return false;
    }

    result->entry = ehdr->e_entry;
    result->brk = page_align_up(highest_addr);

    kfree(file_data);

    return true;
}

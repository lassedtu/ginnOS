#include "elf_loader.h"
#include "elf.h"

#include "../vfs/vfs.h"
#include "../memory/pmm.h"
#include "../memory/heap.h"
#include "../../arch/x86/cpu/paging.h"
#include "../../common/memory.h"
#include "../../common/stdio.h"

#define PAGE_SIZE 4096u

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
static uint32_t page_align_up(uint32_t addr)
{
    return (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

/**
 * map a range of virtual pages as user-accessible in the given page directory,
 * allocating physical frames. pages already mapped are skipped.
 */
static bool map_user_range(uint32_t pd_phys, uint32_t vaddr_start, uint32_t vaddr_end)
{
    uint32_t page;

    vaddr_start &= ~(PAGE_SIZE - 1);
    vaddr_end = page_align_up(vaddr_end);

    for (page = vaddr_start; page < vaddr_end; page += PAGE_SIZE)
    {
        void *frame = pmm_alloc_page();
        if (!frame)
        {
            return false;
        }

        memset(frame, 0, PAGE_SIZE);
        paging_map_in(pd_phys, page, (uint32_t)frame, PTE_USER_RW);
    }

    return true;
}

/**
 * translate a virtual address through a given page directory.
 * used to find the physical frame backing a virtual address in a
 * process's address space (for copying data before switching to it).
 */
static uint32_t pd_get_physical(uint32_t pd_phys, uint32_t virt)
{
    uint32_t *dir = (uint32_t *)pd_phys;
    uint32_t dir_index = (virt >> 22) & 0x3FF;
    uint32_t tbl_index = (virt >> 12) & 0x3FF;

    if (!(dir[dir_index] & PTE_PRESENT))
    {
        return 0;
    }

    uint32_t *table = (uint32_t *)(dir[dir_index] & 0xFFFFF000u);

    if (!(table[tbl_index] & PTE_PRESENT))
    {
        return 0;
    }

    return (table[tbl_index] & 0xFFFFF000u) | (virt & 0xFFF);
}

/**
 * copy data into a process's virtual address space.
 * resolves each page through the process's page directory and writes
 * to the physical frame via identity mapping.
 */
static void copy_to_process(uint32_t pd_phys, uint32_t vaddr, const void *src, uint32_t size)
{
    const uint8_t *s = (const uint8_t *)src;

    while (size > 0)
    {
        uint32_t phys = pd_get_physical(pd_phys, vaddr);
        uint32_t offset_in_page = vaddr & (PAGE_SIZE - 1);
        uint32_t chunk = PAGE_SIZE - offset_in_page;
        if (chunk > size)
        {
            chunk = size;
        }

        memcpy((void *)phys, s, chunk);

        vaddr += chunk;
        s += chunk;
        size -= chunk;
    }
}

/**
 * zero a range in a process's virtual address space.
 */
static void zero_in_process(uint32_t pd_phys, uint32_t vaddr, uint32_t size)
{
    while (size > 0)
    {
        uint32_t phys = pd_get_physical(pd_phys, vaddr);
        uint32_t offset_in_page = vaddr & (PAGE_SIZE - 1);
        uint32_t chunk = PAGE_SIZE - offset_in_page;
        if (chunk > size)
        {
            chunk = size;
        }

        memset((void *)phys, 0, chunk);

        vaddr += chunk;
        size -= chunk;
    }
}

bool elf_load(const char *path, uint32_t pd_phys, elf_load_result_t *result)
{
    VFS_FILE file;
    VFS_STAT stat;
    uint8_t *file_data;
    uint32_t file_size;
    Elf32_Ehdr *ehdr;
    Elf32_Phdr *phdr;
    uint32_t highest_addr;
    uint32_t i;

    if (!path || !result)
    {
        return false;
    }

    /* stat the file to get its size */
    if (vfs_stat(path, &stat) != VFS_OK)
    {
        printf("elf: cannot stat %s\r\n", path);
        return false;
    }

    file_size = stat.size;
    if (file_size < sizeof(Elf32_Ehdr))
    {
        printf("elf: file too small\r\n");
        return false;
    }

    /* open the file */
    if (!vfs_open(path, &file))
    {
        printf("elf: cannot open %s\r\n", path);
        return false;
    }

    /* read entire file into kernel heap */
    file_data = (uint8_t *)kmalloc(file_size);
    if (!file_data)
    {
        printf("elf: out of memory (%u bytes)\r\n", file_size);
        vfs_close(&file);
        return false;
    }

    uint32_t bytes_read = vfs_read(&file, file_size, file_data);
    vfs_close(&file);

    if (bytes_read != file_size)
    {
        printf("elf: short read (%u of %u)\r\n", bytes_read, file_size);
        kfree(file_data);
        return false;
    }

    /* validate ELF header */
    ehdr = (Elf32_Ehdr *)file_data;
    if (!elf_validate(ehdr))
    {
        printf("elf: invalid ELF header\r\n");
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

        uint32_t seg_start = phdr->p_vaddr;
        uint32_t seg_end = seg_start + phdr->p_memsz;

        /* allocate and map pages for this segment */
        if (!map_user_range(pd_phys, seg_start, seg_end))
        {
            printf("elf: failed to map segment at 0x%x\r\n", seg_start);
            kfree(file_data);
            return false;
        }

        /* copy file data into the mapped pages */
        if (phdr->p_filesz > 0)
        {
            if (phdr->p_offset + phdr->p_filesz > file_size)
            {
                printf("elf: segment extends beyond file\r\n");
                kfree(file_data);
                return false;
            }

            copy_to_process(pd_phys, seg_start, file_data + phdr->p_offset, phdr->p_filesz);
        }

        /* zero BSS region (memsz > filesz) */
        if (phdr->p_memsz > phdr->p_filesz)
        {
            zero_in_process(pd_phys, seg_start + phdr->p_filesz,
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
        printf("elf: no loadable segments\r\n");
        kfree(file_data);
        return false;
    }

    result->entry = ehdr->e_entry;
    result->brk = page_align_up(highest_addr);

    kfree(file_data);

    printf("elf: loaded %s (entry=0x%x brk=0x%x)\r\n",
           path, result->entry, result->brk);

    return true;
}

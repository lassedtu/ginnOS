#include "common/stdint.h"
#include "common/stdio.h"
#include "common/boot/boot_info.h"

// lock the layout that stage2 assembly fills in by hand (main.asm). if any of
// these ever fail, the asm offsets and the C struct have drifted apart and the
// kernel would read the memory map from the wrong place.
_Static_assert(__builtin_offsetof(boot_info_t, boot_drive) == 0, "boot_drive @ 0");
_Static_assert(__builtin_offsetof(boot_info_t, memory_map) == 4, "memory_map @ 4");
_Static_assert(__builtin_offsetof(memory_map_t, regions) == 4, "regions @ 4");
_Static_assert(sizeof(memory_region_t) == 20, "region is 20 bytes");

// framebuffer fields the stage2 VBE path writes by offset (main.asm). these
// come after the memory map, so the shared prefix above is unaffected.
_Static_assert(__builtin_offsetof(boot_info_t, framebuffer_addr) == 648, "fb_addr @ 648");
_Static_assert(__builtin_offsetof(boot_info_t, framebuffer_width) == 652, "fb_width @ 652");
_Static_assert(__builtin_offsetof(boot_info_t, framebuffer_height) == 656, "fb_height @ 656");
_Static_assert(__builtin_offsetof(boot_info_t, framebuffer_pitch) == 664, "fb_pitch @ 664");
_Static_assert(__builtin_offsetof(boot_info_t, framebuffer_bpp) == 668, "fb_bpp @ 668");
_Static_assert(__builtin_offsetof(boot_info_t, framebuffer_type) == 669, "fb_type @ 669");
_Static_assert(__builtin_offsetof(boot_info_t, framebuffer_red_size) == 670, "fb_red_size @ 670");

void memory_print_map(boot_info_t *boot)
{
    uint32_t i;
    for (i = 0; i < boot->memory_map.count && i < 32u; i++)
    {
        memory_region_t *region = &boot->memory_map.regions[i];
        printf(
            "[%u] base=0x%x%x len=0x%x%x type=%u\r\n",
            i,
            (uint32_t)(region->base >> 32),
            (uint32_t)region->base,
            (uint32_t)(region->length >> 32),
            (uint32_t)region->length,
            region->type);
    }
}
#include "../../shell/command.h"
#include "../../memory/heap.h"

#include "../../../common/stdio.h"

/**
 * main function of the heapinfo command.
 * displays kernel heap statistics.
 * @param argc number of arguments.
 * @param argv array of argument strings.
 * @return return code of the command.
 */
static int heapinfo_main(
    int argc,
    char **argv)
{
    (void)argv;

    if (argc != 1)
    {
        printf("heapinfo: too many arguments\r\n");
        return -1;
    }

    uint32_t total = heap_total_size();
    uint32_t free = heap_free_size();
    uint32_t used = total - free - heap_block_count() * sizeof(heap_block_t);

    printf("Kernel Heap\r\n");
    printf("  region:  0x%x - 0x%x\r\n", heap_start_address(), heap_end_address());
    printf("  total:   %u bytes (%u KiB)\r\n", total, total / 1024);
    printf("  used:    %u bytes\r\n", used);
    printf("  free:    %u bytes\r\n", free);
    printf("  blocks:  %u\r\n", heap_block_count());
    printf("  overhead: %u bytes (headers)\r\n",
           heap_block_count() * (uint32_t)sizeof(heap_block_t));

    return 0;
}

command_t heapinfo_command =
    {
        .name = "heapinfo",
        .description = "display kernel heap statistics",
        .usage = "heapinfo",
        .main = heapinfo_main,
};

COMMAND_REGISTER(heapinfo_command);

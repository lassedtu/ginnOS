#include "../../shell/command.h"
#include "../../memory/pmm.h"
#include "../../memory/pmm_layout.h"

#include "../../../arch/x86/cpu/paging.h"
#include "../../../common/stdio.h"

/**
 * main function of the vminfo command.
 * displays virtual memory / paging status and statistics.
 * @param argc number of arguments.
 * @param argv array of argument strings.
 * @return return code of the command.
 */
static int vminfo_main(
    int argc,
    char **argv)
{
    (void)argv;

    if (argc != 1)
    {
        printf("vminfo: too many arguments\r\n");
        return -1;
    }

    bool enabled = paging_is_enabled();

    printf("Virtual Memory\r\n");
    printf("  paging:       %s\r\n", enabled ? "enabled" : "disabled");
    printf("  CR3 (PD):     0x%x\r\n", paging_directory_address());
    printf("  page tables:  %u\r\n", paging_table_count());
    printf("  mapped:       %u MiB (identity)\r\n",
           (paging_table_count() * PAGE_ENTRIES * PAGE_SIZE) / (1024 * 1024));

    printf("\r\nPhysical Memory\r\n");
    printf("  total pages:  %u (%u MiB)\r\n",
           pmm_total_count(),
           (pmm_total_count() * PAGE_SIZE) / (1024 * 1024));
    printf("  free pages:   %u (%u KiB)\r\n",
           pmm_free_count(),
           pmm_free_count() * 4);
    printf("  used pages:   %u (%u KiB)\r\n",
           pmm_total_count() - pmm_free_count(),
           (pmm_total_count() - pmm_free_count()) * 4);

    return 0;
}

command_t vminfo_command =
    {
        .name = "vminfo",
        .description = "display virtual memory and paging status",
        .usage = "vminfo",
        .main = vminfo_main,
};

COMMAND_REGISTER(vminfo_command);

#include "../../shell/command.h"
#include "../../memory/region.h"

#include "../../../common/stdio.h"

/**
 * main function of the memmap command.
 * prints all reserved memory regions tracked by the kernel.
 * @param argc number of arguments.
 * @param argv array of argument strings.
 * @return return code of the command.
 */
static int memmap_main(
    int argc,
    char **argv)
{
    (void)argv;

    if (argc != 1)
    {
        printf("memmap: too many arguments\r\n");
        return -1;
    }

    region_print_all();

    return 0;
}

command_t memmap_command =
    {
        .name = "memmap",
        .description = "display reserved memory regions",
        .usage = "memmap",
        .main = memmap_main,
};

COMMAND_REGISTER(memmap_command);

#include "../../shell/command.h"

#include "../../../common/stdio.h"
#include "../../../drivers/video/vga/vga.h"

/**
 * main function of the clear command
 * @param argc number of arguments
 * @param argv array of argument strings
 * @return return code of the command
 */
static int clear_main(int argc, char **argv)
{
    (void)argv;

    if (argc != 1)
    {
        printf("Usage: clear\r\n");
        return -1;
    }

    vga_clear();

    return 0;
}

static command_t clear_command =
    {
        .name = "clear",
        .description = "Clear the terminal screen",
        .usage = "clear",
        .main = clear_main};

COMMAND_REGISTER(clear_command);
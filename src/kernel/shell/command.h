#pragma once

#include "../../common/stdint.h"

/**
 * signature of a shell command main function.
 * @param argc number of arguments.
 * @param argv array of argument strings.
 * @return return code of the command.
 */
typedef int (*command_main_t)(
    int argc,
    char **argv);

/**
 * structure representing a shell command.
 * each command has a name, a description, and a main function that is called when the
 * command is executed.
 */
typedef struct
{
    const char *name;

    const char *description;

    const char *usage;

    command_main_t main;

} command_t;

/**
 * place a command in the linker-discoverable command set.
 */
#define COMMAND_REGISTER(_cmd)                \
    static command_t *const _cmd##_link_entry \
        __attribute__((used, section(".shell_cmds"))) = &(_cmd)

/**
 * register a command with the shell.
 * @param command pointer to the command structure to register.
 */
void command_register(command_t *command);

/**
 * look up a command by name.
 * @param name the name of the command to look up.
 * @return pointer to the command structure if found, or NULL if not found.
 */
command_t *command_lookup(const char *name);

/**
 * initialize and register all statically linked commands.
 * this function is the single entry point used by kernel startup.
 */
void commands_initialize(void);
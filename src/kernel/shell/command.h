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

    command_main_t main;

} command_t;

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
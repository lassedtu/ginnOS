#include "shell.h"

#include <stddef.h>

#include "printk.h"
#include "vga.h"

#define SHELL_BUFFER_SIZE 80
#define SHELL_PROMPT "> "

typedef void (*shell_command_handler)(const char *arguments);

static void shell_print_help(const char *arguments);
static void shell_handle_clear(const char *arguments);
static void shell_handle_echo(const char *arguments);
static void shell_handle_gfetch(const char *arguments);

typedef struct
{
    const char *name;
    const char *help;
    shell_command_handler handler;
} shell_command;

static const shell_command shell_commands[] = {
    {"help", "Show this help message", shell_print_help},
    {"clear", "Clear the screen", shell_handle_clear},
    {"echo", "Echo the provided arguments", shell_handle_echo},
    {"gfetch", "Fetch system information", shell_handle_gfetch},
};

static char shell_buffer[SHELL_BUFFER_SIZE];
static unsigned int shell_buffer_length = 0;
static int shell_initialized = 0;

static void shell_print_prompt(void)
{
    vga_write_string(SHELL_PROMPT);
}

static void shell_clear_buffer(void)
{
    for (unsigned int index = 0; index < shell_buffer_length; index++)
    {
        shell_buffer[index] = '\0';
    }

    shell_buffer_length = 0;
}

static void shell_print_help(const char *arguments)
{
    (void)arguments;

    printk("Available commands:\n");

    for (unsigned int index = 0; index < sizeof(shell_commands) / sizeof(shell_commands[0]); index++)
    {
        printk("  %s - %s\n", shell_commands[index].name, shell_commands[index].help);
    }
}

static void shell_handle_clear(const char *arguments)
{
    (void)arguments;
    vga_clear();
}

static void shell_handle_echo(const char *arguments)
{
    printk("%s\n", arguments);
}

static void shell_handle_gfetch(const char *arguments)
{
    const char *gfetch_lines[] = {
        "     ______       user@ginnungOS",
        "    /  ____|      --------------",
        "   |  |  ___      OS: ginnungOS x86",
        "   |  | |_  |     Kernel: 0.1.0-dev",
        "   |  |___| |     Uptime: 9m",
        "    \\______/      Shell: gsh",
        "                  Display: VGA 80x25 Text",
        "                  Memory: 1240KB / 16MB",
        NULL};

    (void)arguments;
    for (unsigned int index = 0; gfetch_lines[index] != NULL; index++)
    {
        printk("%s\n", gfetch_lines[index]);
    }
}

static const char *shell_extract_arguments(const char *input)
{
    unsigned int index = 0;

    while (input[index] != '\0' && input[index] != ' ')
    {
        index++;
    }

    if (input[index] == ' ')
    {
        while (input[index] == ' ')
        {
            index++;
        }

        return &input[index];
    }

    return "";
}

static int shell_match_command(const char *input, const char *name)
{
    unsigned int index = 0;

    while (input[index] != '\0' && input[index] != ' ' && name[index] != '\0')
    {
        if (input[index] != name[index])
        {
            return 0;
        }

        index++;
    }

    return (name[index] == '\0') && (input[index] == '\0' || input[index] == ' ');
}

static void shell_execute_command(void)
{
    if (shell_buffer_length == 0)
    {
        printk("\n");
        shell_print_prompt();
        return;
    }

    shell_buffer[shell_buffer_length] = '\0';

    printk("\n");

    for (unsigned int index = 0; index < sizeof(shell_commands) / sizeof(shell_commands[0]); index++)
    {
        if (shell_match_command(shell_buffer, shell_commands[index].name))
        {
            const char *arguments = shell_extract_arguments(shell_buffer);

            if (shell_commands[index].handler != NULL)
            {
                shell_commands[index].handler(arguments);
            }

            shell_clear_buffer();
            shell_print_prompt();
            return;
        }
    }

    printk("Unknown command: %s\n", shell_buffer);
    shell_clear_buffer();
    shell_print_prompt();
}

static void shell_handle_backspace(void)
{
    if (shell_buffer_length == 0)
    {
        return;
    }

    shell_buffer[--shell_buffer_length] = '\0';
    vga_backspace();
}

void shell_init(void)
{
    if (shell_initialized)
    {
        return;
    }

    shell_clear_buffer();
    shell_initialized = 1;
    shell_print_prompt();
}

void shell_handle_char(char character)
{
    if (!shell_initialized)
    {
        shell_init();
    }

    if (character == '\b')
    {
        shell_handle_backspace();
        return;
    }

    if (character == '\n' || character == '\r')
    {
        shell_execute_command();
        return;
    }

    if (character == 0)
    {
        return;
    }

    if (shell_buffer_length >= SHELL_BUFFER_SIZE - 1)
    {
        return;
    }

    shell_buffer[shell_buffer_length++] = character;
    vga_write_char(character);
}

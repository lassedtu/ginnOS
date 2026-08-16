#include "../../shell/command.h"
#include "../../shell/context.h"
#include "../../vfs/vfs.h"
#include "../../usermode/usermode.h"

#include "../../../common/stdio.h"

static int exec_main(int argc, char **argv)
{
    shell_context_t *ctx;
    char resolved_path[SHELL_PATH_MAX];

    if (argc < 2)
    {
        printf("exec: usage: exec <path>\r\n");
        return -1;
    }

    ctx = shell_context_get();

    if (!vfs_resolve_path(ctx->cwd, argv[1], resolved_path, sizeof(resolved_path)))
    {
        printf("exec: invalid path %s\r\n", argv[1]);
        return -1;
    }

    int result = exec_program(resolved_path, (const char **)0);

    if (result < 0)
    {
        printf("exec: failed to execute %s\r\n", argv[1]);
    }

    return result;
}

command_t exec_command =
    {
        .name = "exec",
        .description = "execute an ELF binary",
        .usage = "exec <path>",
        .main = exec_main,
};

COMMAND_REGISTER(exec_command);

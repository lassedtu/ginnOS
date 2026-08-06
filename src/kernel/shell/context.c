#include "context.h"

static shell_context_t context =
    {
        .cwd = "/"};

shell_context_t *shell_context_get(void)
{
    return &context;
}
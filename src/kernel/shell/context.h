#pragma once

#include "../../common/stdint.h"

#define SHELL_PATH_MAX 256

/**
 * structure representing the shell context, including the current working directory.
 */
typedef struct
{
    char cwd[SHELL_PATH_MAX];

} shell_context_t;

/**
 * get a pointer to the global shell context structure.
 * @return pointer to the shell_context_t structure.
 */
shell_context_t *shell_context_get(void);
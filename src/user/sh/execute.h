/**
 * @file execute.h
 * @brief Header for executing external commands.
 */

#pragma once

#include "sh.h"

// execute an external command by searching /bin/<cmd>.
// forks a child process and waits for it to complete.
void execute(token_list_t *tokens);

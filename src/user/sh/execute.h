// execute.h - external command execution

#pragma once

#include "sh.h"

// execute an external command by searching /bin/<cmd>.
// forks a child process and waits for it to complete.
void execute(token_list_t *tokens);

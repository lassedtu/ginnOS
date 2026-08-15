// builtin.h - shell built-in commands

#pragma once

#include "sh.h"

// attempt to run a built-in command.
// returns 1 if the command was handled, 0 if not a builtin.
int builtin_run(token_list_t *tokens);

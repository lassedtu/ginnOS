#pragma once

#include "lexer.h"

/**
 * execute a command represented by a list of tokens.
 * the first token is the command name, and the remaining tokens are the arguments.
 * @param tokens pointer to a token_list_t structure containing the command and arguments.
 */
void executor_execute(token_list_t *tokens);
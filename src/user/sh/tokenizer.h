/**
 * @file tokenizer.h
 * @brief Header for shell input tokenizer.
 */

#pragma once

#include "sh.h"

// tokenize an input line by whitespace.
// modifies the input string in place (inserts null terminators).
// fills tokens->tokens[] with pointers into the input buffer.
void tokenize(char *input, token_list_t *tokens);

/**
 * @file expand.h
 * @brief Variable expansion for shell tokens.
 */

#pragma once

#include "sh.h"

/**
 * expand variables ($VAR, ${VAR}) in all tokens.
 * modifies the token list in place.
 * @param tokens the token list to expand.
 */
void expand_variables(token_list_t *tokens);

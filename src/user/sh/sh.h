/**
 * @file sh.h
 * @brief Header for shell implementation.
 */

#pragma once

#define TOKEN_MAX 256 // words in one command line

typedef struct
{
    int count;
    char *tokens[TOKEN_MAX];
} token_list_t;

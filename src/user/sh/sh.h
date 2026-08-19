/**
 * @file sh.h
 * @brief Header for shell implementation.
 */

#pragma once

#define TOKEN_MAX 32
#define TOKEN_LEN 128

typedef struct
{
    int count;
    char *tokens[TOKEN_MAX];
} token_list_t;

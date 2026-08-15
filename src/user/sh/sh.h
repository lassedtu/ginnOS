// sh.h - shared definitions for the ginnOS shell

#pragma once

#define TOKEN_MAX  32
#define TOKEN_LEN  128

typedef struct
{
    int count;
    char *tokens[TOKEN_MAX];
} token_list_t;

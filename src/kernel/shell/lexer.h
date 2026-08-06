#pragma once

#define MAX_TOKENS 32 // maximum number of tokens that can be produced by the lexer.

/**
 * structure representing a list of tokens produced by the lexer.
 */
typedef struct
{
    int count;
    char *tokens[MAX_TOKENS];

} token_list_t;

/**
 * tokenize a string into a list of tokens.
 * tokens are separated by whitespace, and quoted strings are treated as a single token.
 * @param input the input string to tokenize.
 * @param tokens pointer to a token_list_t structure to receive the tokens.
 * @return void.
 */
void lexer_tokenize(
    char *input,
    token_list_t *tokens);
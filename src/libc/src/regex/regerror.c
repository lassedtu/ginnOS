/**
 * @file regerror.c
 * @brief the messages of the regular expression errors.
 */

#include "regex.h"
#include "stdio.h"

static const char *const messages[] = {
    "No error",
    "No match",
    "Invalid regular expression",
    "Unknown collating element",
    "Unknown character class name",
    "Trailing backslash",
    "Invalid back reference",
    "Missing ']'",
    "Missing ')'",
    "Missing '}'",
    "Invalid contents of {}",
    "Invalid character range",
    "Out of memory",
    "Repetition not preceded by a valid expression",
};

#define MESSAGE_COUNT ((int)(sizeof(messages) / sizeof(messages[0])))

size_t regerror(int error, const regex_t *restrict preg, char *restrict buffer, size_t size)
{
    (void)preg;
    const char *message = error >= 0 && error < MESSAGE_COUNT ? messages[error] : "Unknown error";
    return 1 + (size_t)snprintf(buffer, size, "%s", message);
}

#pragma once

#include "common/stdint.h"

/**
 * unified kernel error codes.
 *
 * all kernel subsystems return kerr_t from fallible operations.
 * KERR_OK (0) indicates success, all other values indicate failure.
 * values are negative to allow direct mapping to userspace errno at
 * the syscall boundary.
 */
typedef enum
{
    KERR_OK       =  0, // success
    KERR_NOMEM    = -1, // out of memory
    KERR_IO       = -2, // i/o error
    KERR_NOTFOUND = -3, // file or resource not found
    KERR_PERM     = -4, // permission denied
    KERR_INVAL    = -5, // invalid argument
    KERR_BUSY     = -6, // resource busy
    KERR_NOSPC    = -7, // no space left on device
    KERR_RANGE    = -8, // value out of range
    KERR_EXIST    = -9, // resource already exists
    KERR_ISDIR    = -10, // is a directory (expected file)
    KERR_NOTDIR   = -11, // not a directory (expected directory)
    KERR_PIPE     = -12, // broken pipe
    KERR_NOENT    = -13, // no such entry
    KERR_FAULT    = -14, // bad address (pointer validation failed)
} kerr_t;

/**
 * check if a kerr_t value indicates success.
 */
static inline bool kerr_ok(kerr_t err)
{
    return err == KERR_OK;
}

/**
 * check if a kerr_t value indicates failure.
 */
static inline bool kerr_failed(kerr_t err)
{
    return err != KERR_OK;
}

/**
 * convert a kerr_t to a human-readable string for debug output.
 * @param err the error code.
 * @return static string describing the error.
 */
const char *status_to_str(kerr_t err);

#include "common/error.h"

const char *status_to_str(kerr_t err)
{
    switch (err)
    {
    case KERR_OK:       return "ok";
    case KERR_NOMEM:    return "out of memory";
    case KERR_IO:       return "i/o error";
    case KERR_NOTFOUND: return "not found";
    case KERR_PERM:     return "permission denied";
    case KERR_INVAL:    return "invalid argument";
    case KERR_BUSY:     return "resource busy";
    case KERR_NOSPC:    return "no space left";
    case KERR_RANGE:    return "out of range";
    case KERR_EXIST:    return "already exists";
    case KERR_ISDIR:    return "is a directory";
    case KERR_NOTDIR:   return "not a directory";
    case KERR_PIPE:     return "broken pipe";
    case KERR_NOENT:    return "no such entry";
    case KERR_FAULT:    return "bad address";
    }
    return "unknown error";
}

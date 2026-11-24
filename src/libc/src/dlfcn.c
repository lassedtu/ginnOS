/**
 * @file dlfcn.c
 * @brief dlopen and its kin, without a dynamic loader (dlfcn.h).
 */

#include "dlfcn.h"

#include <stddef.h>

static __thread const char *last_error;

void *dlopen(const char *file, int mode)
{
    (void)file;
    (void)mode;
    last_error = "dynamic loading is not supported";
    return NULL;
}

void *dlsym(void *handle, const char *name)
{
    (void)handle;
    (void)name;
    last_error = "dynamic loading is not supported";
    return NULL;
}

int dlclose(void *handle)
{
    (void)handle;
    return 0;
}

char *dlerror(void)
{
    const char *error = last_error;
    last_error = NULL;
    return (char *)error;
}

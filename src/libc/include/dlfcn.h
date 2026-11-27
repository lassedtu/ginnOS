#pragma once

/**
 * @file dlfcn.h
 * @brief loading shared libraries at run time. ginnOS programs are static
 *        and there is no dynamic loader yet: dlopen always fails, and
 *        dlerror says why. programs that can do without a library (SDL's
 *        optional parts) go on without it.
 */

#define RTLD_LAZY   0x0001
#define RTLD_NOW    0x0002
#define RTLD_GLOBAL 0x0100
#define RTLD_LOCAL  0x0000
#define RTLD_NOLOAD 0x0004

#define RTLD_DEFAULT ((void *)0)

/** @return NULL: there is no dynamic loader */
void *dlopen(const char *file, int mode);

/** @return NULL */
void *dlsym(void *handle, const char *name);

/** @return 0 */
int dlclose(void *handle);

/** @return why the last call failed, once (then NULL) */
char *dlerror(void);

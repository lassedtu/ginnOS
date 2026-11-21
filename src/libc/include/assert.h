#pragma once

/**
 * @file assert.h
 * @brief assert(): with NDEBUG defined it checks nothing.
 */

void __assert_fail(const char *expression, const char *file, int line, const char *function)
    __attribute__((noreturn));

#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) \
    ((expression) ? (void)0 : __assert_fail(#expression, __FILE__, __LINE__, __func__))
#endif

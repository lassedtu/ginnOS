#pragma once

/**
 * @file stdlib.h
 * @brief This file contains the declarations of general utility functions.
 */

#ifndef NULL
#define NULL ((void *)0)
#endif

/**
 * convert a string to an integer.
 * @param s null-terminated string.
 * @return the integer value.
 */
int atoi(const char *s);

/**
 * terminate the process with an exit code.
 * @param status exit code.
 */
void exit(int status) __attribute__((noreturn));

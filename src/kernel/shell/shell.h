#pragma once

/**
 * initialize the shell. this function must be called before any other shell functions are used.
 */
void shell_initialize(void);

/**
 * run the shell. this function will block until the shell exits.
 * this function will not return under normal operation.
 */
void shell_run(void);
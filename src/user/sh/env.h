/**
 * @file env.h
 * @brief Environment variable management for the shell.
 */

#pragma once

#define ENV_MAX 64      // maximum number of environment variables
#define ENV_KEY_MAX 64  // maximum key length
#define ENV_VAL_MAX 256 // maximum value length

/**
 * initialize the environment subsystem.
 */
void env_init(void);

/**
 * set an environment variable. creates it if it doesn't exist.
 * @param key variable name.
 * @param value variable value.
 * @return 0 on success, -1 if the table is full.
 */
int env_set(const char *key, const char *value);

/**
 * get the value of an environment variable.
 * @param key variable name.
 * @return pointer to the value string, or NULL if not found.
 */
const char *env_get(const char *key);

/**
 * remove an environment variable.
 * @param key variable name.
 * @return 0 on success, -1 if not found.
 */
int env_unset(const char *key);

/**
 * get the number of environment variables set.
 * @return count of variables.
 */
int env_count(void);

/**
 * get a variable by index (for iteration).
 * @param index 0-based index.
 * @param key_out buffer to receive the key (at least ENV_KEY_MAX bytes).
 * @param val_out buffer to receive the value (at least ENV_VAL_MAX bytes).
 * @return 0 on success, -1 if index out of range.
 */
int env_get_by_index(int index, char *key_out, char *val_out);

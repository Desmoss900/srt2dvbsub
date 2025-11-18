#ifndef DELAY_PARSE_H
#define DELAY_PARSE_H

#include <stdlib.h>

/**
 * parse_single_delay
 *
 * Parse a single delay value from a string with robust error handling.
 * Replaces atoi() with better validation and error messages.
 *
 * @param str Input string to parse (e.g., "1500", "-500", "abc")
 * @param delay Pointer to int where parsed value is stored
 * @param errmsg Pointer to string buffer for error message (size >= 256 bytes)
 *               If NULL, no error message is written.
 *               On error, contains message like "Invalid delay value: 'abc' (expected milliseconds)"
 *
 * @return 0 on success, negative on error (-EINVAL for parse error, -ERANGE for out of range)
 *
 * Acceptable range: -2147483648 to 2147483647 (int min/max)
 * Typical usage range: -10000 to +10000 ms recommended
 *
 * @note Trims leading/trailing whitespace before parsing.
 */
int parse_single_delay(const char *str, int *delay, char *errmsg);

/**
 * parse_delay_list
 *
 * Parse a comma-separated list of delay values with per-element error tracking.
 * Handles whitespace and empty entries gracefully.
 *
 * @param delay_str Comma-separated string (e.g., "500,1000,750" or "500, 1000 , 750")
 * @param delay_vals Pointer to int* array pointer (allocated by function, caller must free)
 * @param delay_count Pointer to count (number of successfully parsed values)
 * @param errmsg Pointer to string buffer for error details (size >= 256 bytes)
 *               Contains details about which entry failed and why.
 *               On success, contains nothing; on error, populated with diagnostic info.
 *
 * @return 0 on success (all entries parsed), negative on error
 *         Returns -EINVAL if any entry fails to parse
 *         Returns -ENOMEM if memory allocation fails
 *         On error, delay_vals is still allocated; caller must free it
 *
 * @note Empty entries (consecutive commas) are reported and return error
 * @note Whitespace around values is trimmed automatically
 * @note If error occurs, *delay_count contains number of entries parsed before failure
 */
int parse_delay_list(const char *delay_str, int **delay_vals, int *delay_count, char *errmsg);

#endif

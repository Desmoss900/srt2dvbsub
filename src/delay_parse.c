#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>

#include "delay_parse.h"
#include "utils.h"

/**
 * trim_string
 *
 * Helper: trim leading and trailing whitespace from a string.
 * Modifies string in-place by adjusting start pointer and null-terminating at end.
 *
 * @return Pointer to trimmed string (may be same as input if no whitespace)
 */
static char* trim_string(char *str)
{
    return trim_string_inplace(str);
}

int parse_single_delay(const char *str, int *delay, char *errmsg)
{
    if (!str || !delay)
        return -EINVAL;

    /* Make a trimmed copy */
    char *tmp = strdup(str);
    if (!tmp) {
        if (errmsg)
            snprintf(errmsg, 256, "Out of memory");
        return -ENOMEM;
    }

    char *trimmed = trim_string(tmp);

    /* Check for empty string after trimming */
    if (*trimmed == '\0') {
        if (errmsg)
            snprintf(errmsg, 256, "Empty delay value");
        free(tmp);
        return -EINVAL;
    }

    /* Use strtol for robust parsing */
    char *endptr = NULL;
    errno = 0;
    long val = strtol(trimmed, &endptr, 10);

    /* Check for various error conditions */
    if (errno == ERANGE) {
        if (errmsg)
            snprintf(errmsg, 256, "Delay value out of range: '%s' (exceeds int limits)", str);
        free(tmp);
        return -ERANGE;
    }

    if (endptr == trimmed || *endptr != '\0') {
        if (errmsg)
            snprintf(errmsg, 256, "Invalid delay value: '%s' (expected milliseconds, got '%s')", 
                     str, trimmed);
        free(tmp);
        return -EINVAL;
    }

    /* Verify value fits in int range */
    if (val < INT_MIN || val > INT_MAX) {
        if (errmsg)
            snprintf(errmsg, 256, "Delay value out of range: '%s' (exceeds int limits)", str);
        free(tmp);
        return -ERANGE;
    }

    *delay = (int)val;
    free(tmp);
    return 0;
}

int parse_delay_list(const char *delay_str, int **delay_vals, int *delay_count, char *errmsg)
{
    if (!delay_str || !delay_vals || !delay_count)
        return -EINVAL;

    *delay_vals = NULL;
    *delay_count = 0;

    /* Make a working copy for strtok_r */
    char *dlcopy = strdup(delay_str);
    if (!dlcopy) {
        if (errmsg)
            snprintf(errmsg, 256, "Out of memory");
        return -ENOMEM;
    }

    int entry_index = 0;
    char *saveptr = NULL;
    char *token = strtok_r(dlcopy, ",", &saveptr);

    while (token) {
        entry_index++;

        /* Trim token */
        char *trimmed = trim_string(token);

        /* Check for empty entry */
        if (*trimmed == '\0') {
            if (errmsg)
                snprintf(errmsg, 256, "Empty delay entry at position %d (consecutive commas or leading/trailing comma)",
                         entry_index);
            free(*delay_vals);
            free(dlcopy);
            *delay_vals = NULL;
            return -EINVAL;
        }

        /* Parse the delay value */
        int val = 0;
        char parse_err[256] = {0};
        int ret = parse_single_delay(trimmed, &val, parse_err);
        if (ret != 0) {
            if (errmsg)
                snprintf(errmsg, 256, "Delay entry %d parse error: %s", entry_index, parse_err);
            free(*delay_vals);
            free(dlcopy);
            *delay_vals = NULL;
            return ret;
        }

        /* Grow allocation */
        int *nv = realloc(*delay_vals, sizeof(*nv) * (*delay_count + 1));
        if (!nv) {
            if (errmsg)
                snprintf(errmsg, 256, "Out of memory parsing delay list");
            free(*delay_vals);
            free(dlcopy);
            *delay_vals = NULL;
            return -ENOMEM;
        }

        nv[*delay_count] = val;
        *delay_count += 1;
        *delay_vals = nv;

        token = strtok_r(NULL, ",", &saveptr);
    }

    free(dlcopy);
    return 0;
}

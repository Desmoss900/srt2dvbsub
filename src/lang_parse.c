#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "lang_parse.h"
#include "dvb_lang.h"
#include "utils.h"

/**
 * trim_string
 *
 * Helper: trim leading and trailing whitespace from a string in-place.
 * Returns pointer to trimmed content and null-terminates the trimmed region.
 */
static char* trim_string(char *str)
{
    return trim_string_inplace(str);
}

int validate_language_list(const char *lang_str, char *errmsg)
{
    if (!lang_str || !errmsg)
        return -EINVAL;

    char *working_copy = strdup(lang_str);
    if (!working_copy) {
        snprintf(errmsg, 512, "Out of memory");
        return -ENOMEM;
    }

    int position = 0;

    char *saveptr = NULL;
    char *token = strtok_r(working_copy, ",", &saveptr);

    while (token) {
        position++;

        /* Trim token */
        char *trimmed = trim_string(token);

        /* Check for empty entry */
        if (*trimmed == '\0') {
            snprintf(errmsg, 512, "Position %d: empty language code (consecutive commas or leading/trailing comma)",
                     position);
            free(working_copy);
            return -EINVAL;
        }

        /* Validate language code format and existence */
        if (!is_valid_dvb_lang(trimmed)) {
            snprintf(errmsg, 512, "Position %d: invalid language code '%s' (must be 3-letter DVB language code)",
                     position, trimmed);
            free(working_copy);
            return -EINVAL;
        }

        token = strtok_r(NULL, ",", &saveptr);
    }

    free(working_copy);
    return 0;
}

int parse_language_list(const char *lang_str, lang_entry **lang_entries, int *count, char *errmsg)
{
    if (!lang_str || !lang_entries || !count || !errmsg)
        return -EINVAL;

    *lang_entries = NULL;
    *count = 0;
    errmsg[0] = '\0';

    char *working_copy = strdup(lang_str);
    if (!working_copy) {
        snprintf(errmsg, 512, "Out of memory");
        return -ENOMEM;
    }

    int position = 0;
    int duplicate_detected = 0;

    char *saveptr = NULL;
    char *token = strtok_r(working_copy, ",", &saveptr);

    while (token) {
        position++;

        /* Trim token */
        char *trimmed = trim_string(token);

        /* Check for empty entry */
        if (*trimmed == '\0') {
            snprintf(errmsg, 512, "Position %d: empty language code (consecutive commas or leading/trailing comma)",
                     position);
            free(*lang_entries);
            free(working_copy);
            *lang_entries = NULL;
            *count = 0;
            return -EINVAL;
        }

        /* Validate language code format and existence */
        if (!is_valid_dvb_lang(trimmed)) {
            snprintf(errmsg, 512, "Position %d: invalid language code '%s' (must be 3-letter DVB language code)",
                     position, trimmed);
            free(*lang_entries);
            free(working_copy);
            *lang_entries = NULL;
            *count = 0;
            return -EINVAL;
        }

        /* Check for duplicates (but allow them for now) */
        for (int i = 0; i < *count; i++) {
            if (strcmp((*lang_entries)[i].code, trimmed) == 0) {
                duplicate_detected = 1;
                /* Log warning but continue processing */
                snprintf(errmsg, 512, "Duplicate language code '%s' at positions %d and %d (allowed if tracks have different flags)",
                         trimmed, i + 1, position);
                break;
            }
        }

        /* Grow allocation */
        lang_entry *nv = realloc(*lang_entries, sizeof(**lang_entries) * (*count + 1));
        if (!nv) {
            snprintf(errmsg, 512, "Out of memory parsing language list");
            free(*lang_entries);
            free(working_copy);
            *lang_entries = NULL;
            *count = 0;
            return -ENOMEM;
        }

        /* Add entry */
        strncpy(nv[*count].code, trimmed, sizeof(nv[*count].code) - 1);
        nv[*count].code[sizeof(nv[*count].code) - 1] = '\0';
        nv[*count].position = position;
        *count += 1;
        *lang_entries = nv;

        token = strtok_r(NULL, ",", &saveptr);
    }

    free(working_copy);

    /* Return success even if duplicates found - validation happens at track level */
    return duplicate_detected ? -ENOTSUP : 0;
}

int get_language_count(const char *lang_str)
{
    if (!lang_str || *lang_str == '\0')
        return 0;

    int count = 1;  /* At least one if string is not empty */
    for (const char *p = lang_str; *p; p++) {
        if (*p == ',')
            count++;
    }
    return count;
}

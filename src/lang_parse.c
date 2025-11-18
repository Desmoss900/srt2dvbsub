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

    char *seen_codes[256];  /* Track seen codes for duplicate detection */
    int seen_count = 0;
    int position = 0;
    int has_duplicates = 0;

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

        /* Check for duplicates */
        for (int i = 0; i < seen_count; i++) {
            if (strcmp(seen_codes[i], trimmed) == 0) {
                if (!has_duplicates) {
                    /* First duplicate found; report both positions */
                    snprintf(errmsg, 512, "Duplicate language code '%s' at positions %d and %d",
                             trimmed, i + 1, position);  /* i+1 because i is 0-based but position is 1-based */
                    has_duplicates = 1;
                }
                free(working_copy);
                return -ENOTSUP;
            }
        }

        /* Add to seen list if not a duplicate */
        if (seen_count < 256) {
            /* Make a copy of the trimmed code */
            char *code_copy = strdup(trimmed);
            if (!code_copy) {
                snprintf(errmsg, 512, "Out of memory tracking language codes");
                free(working_copy);
                return -ENOMEM;
            }
            seen_codes[seen_count++] = code_copy;
        }

        token = strtok_r(NULL, ",", &saveptr);
    }

    /* Cleanup seen_codes array */
    for (int i = 0; i < seen_count; i++) {
        free(seen_codes[i]);
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

    char *working_copy = strdup(lang_str);
    if (!working_copy) {
        snprintf(errmsg, 512, "Out of memory");
        return -ENOMEM;
    }

    char *seen_codes[256];  /* Track seen codes for duplicate detection */
    int seen_count = 0;
    int position = 0;
    int has_duplicates = 0;
    int first_dup_pos1 = -1, first_dup_pos2 = -1;
    char first_dup_code[4] = "";

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

        /* Check for duplicates */
        for (int i = 0; i < seen_count; i++) {
            if (strcmp(seen_codes[i], trimmed) == 0) {
                if (!has_duplicates) {
                    first_dup_pos1 = i + 1;  /* 1-based */
                    first_dup_pos2 = position;
                    strncpy(first_dup_code, trimmed, sizeof(first_dup_code) - 1);
                    first_dup_code[sizeof(first_dup_code) - 1] = '\0';
                    has_duplicates = 1;
                }
                break;  /* Don't report every duplicate after the first */
            }
        }

        /* Add to seen list (for duplicate tracking, even if duplicate) */
        if (seen_count < 256) {
            char *code_copy = strdup(trimmed);
            if (!code_copy) {
                snprintf(errmsg, 512, "Out of memory tracking language codes");
                free(*lang_entries);
                free(working_copy);
                *lang_entries = NULL;
                *count = 0;
                return -ENOMEM;
            }
            seen_codes[seen_count++] = code_copy;
        }

        /* Grow allocation */
        lang_entry *nv = realloc(*lang_entries, sizeof(**lang_entries) * (*count + 1));
        if (!nv) {
            snprintf(errmsg, 512, "Out of memory parsing language list");
            for (int i = 0; i < seen_count; i++)
                free(seen_codes[i]);
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

    /* Cleanup seen_codes array */
    for (int i = 0; i < seen_count; i++) {
        free(seen_codes[i]);
    }

    free(working_copy);

    /* Report duplicates if found */
    if (has_duplicates) {
        snprintf(errmsg, 512, "Duplicate language code '%s' at positions %d and %d",
                 first_dup_code, first_dup_pos1, first_dup_pos2);
        return -ENOTSUP;
    }

    return 0;
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

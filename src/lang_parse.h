#ifndef LANG_PARSE_H
#define LANG_PARSE_H

#include <stdlib.h>

/**
 * lang_entry
 *
 * Represents a single parsed language code entry with position and content.
 * Used by parse_language_list() to return detailed information about each
 * language code including its position for diagnostic purposes.
 */
typedef struct {
    char code[4];   /* Three-letter code + null terminator */
    int position;   /* 1-based position in the original comma-separated list */
} lang_entry;

/**
 * validate_language_list
 *
 * Validate a comma-separated language code list with detailed diagnostics.
 * Handles whitespace trimming, duplicate detection, and position tracking.
 *
 * @param lang_str Comma-separated string (e.g., "eng,fra,deu" or "eng, fra , deu")
 * @param errmsg Pointer to string buffer for error details (size >= 512 bytes)
 *               On error, contains message like:
 *               "Position 2: invalid language code 'xx' (must be 3 letters)"
 *               or "Duplicate language code 'eng' at positions 1 and 3"
 *
 * @return 0 on success (all codes valid, no duplicates), negative on error:
 *         -EINVAL: invalid language code or empty entry
 *         -ENOTSUP: duplicate language codes detected
 *
 * @note This function is for CLI validation only. Duplicates are treated as errors.
 * @note Whitespace is automatically trimmed from each token.
 * @note Case-sensitive comparison; "eng" and "ENG" are treated as different.
 */
int validate_language_list(const char *lang_str, char *errmsg);

/**
 * parse_language_list
 *
 * Parse a comma-separated language code list into an array.
 * Handles whitespace, empty entries, and provides position tracking.
 * Unlike validate_language_list, duplicates are reported but not fatal.
 *
 * @param lang_str Comma-separated string (e.g., "eng, fra , deu")
 * @param lang_entries Pointer to lang_entry* array pointer (allocated by function, caller must free)
 * @param count Pointer to count (number of parsed entries)
 * @param errmsg Pointer to string buffer for warnings/errors (size >= 512 bytes)
 *               Empty on success; on error/warning, contains diagnostic info.
 *
 * @return 0 on success (all codes valid), -ENOTSUP if duplicates detected (still returns entries),
 *         -EINVAL if invalid entry, -ENOMEM if memory allocation fails
 *         On any error, lang_entries is still allocated; caller must free it.
 *
 * @note Duplicates are reported via return code and errmsg but do NOT stop processing.
 * @note Whitespace is automatically trimmed from each token.
 * @note Position field in entries is 1-based for user-friendly reporting.
 * @note Empty entries are detected and reported; returns -EINVAL.
 */
int parse_language_list(const char *lang_str, lang_entry **lang_entries, int *count, char *errmsg);

/**
 * get_language_count
 *
 * Quick utility to count language codes in a list without full parsing.
 * Useful for pre-checking list lengths before allocation.
 *
 * @param lang_str Comma-separated string
 *
 * @return Number of comma-separated fields (may include invalid entries)
 *         Returns 0 for empty/NULL string
 *         Does NOT validate language codes; just counts commas + 1
 *
 * @note This is a simple count; use parse_language_list() for full validation.
 */
int get_language_count(const char *lang_str);

#endif

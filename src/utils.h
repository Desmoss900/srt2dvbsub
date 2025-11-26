/*
* Copyright (c) 2025 Mark E. Rosche, Chili IPTV Systems
* All rights reserved.
*
* PERSONAL USE LICENSE - NON-COMMERCIAL ONLY
* ────────────────────────────────────────────────────────────────
* This software is provided for personal, educational, and non-commercial
* use only. You are granted permission to use, copy, and modify this
* software for your own personal or educational purposes, provided that
* this copyright and license notice appears in all copies or substantial
* portions of the software.
*
* PERMITTED USES:
*   ✓ Personal projects and experimentation
*   ✓ Educational purposes and learning
*   ✓ Non-commercial testing and evaluation
*   ✓ Individual hobbyist use
*
* PROHIBITED USES:
*   ✗ Commercial use of any kind
*   ✗ Incorporation into products or services sold for profit
*   ✗ Use within organizations or enterprises for revenue-generating activities
*   ✗ Modification, redistribution, or hosting as part of any commercial offering
*   ✗ Licensing, selling, or renting this software to others
*   ✗ Using this software as a foundation for commercial services
*
* No commercial license is available. For inquiries regarding any use not
* explicitly permitted above, contact:
*   Mark E. Rosche, Chili IPTV Systems
*   Email: license@chili-iptv.de
*   Website: www.chili-iptv.de
*
* ────────────────────────────────────────────────────────────────
* DISCLAIMER
* ────────────────────────────────────────────────────────────────
* THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
* DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*
* ────────────────────────────────────────────────────────────────
* By using this software, you agree to these terms and conditions.
* ────────────────────────────────────────────────────────────────
*/

#pragma once
#ifndef UTILS_H
#define UTILS_H

#include <signal.h>
#include "runtime_opts.h"

/**
 * @brief Prints the version information of the program to the standard output.
 *
 * This function outputs the current version details, such as version number and
 * possibly build information, to the console. It does not take any parameters
 * and does not return any value.
 */
void print_version(void);

/**
 * @brief Prints the help message to the standard output.
 *
 * This function displays usage instructions and available options
 * for the program to assist users.
 */
void print_help(void);

/**
 * @brief Prints the license information to the standard output.
 *
 * This function outputs the software license details, such as copyright
 * and usage terms, to the console. It does not take any parameters and
 * does not return a value.
 */
void print_license(void);

/**
 * @brief Prints the usage information for the program to the standard output.
 *
 * This function displays instructions on how to use the program, including
 * available command-line options and their descriptions.
 */
void print_usage(void);

/**
 * Calculates the display width of a UTF-8 encoded string.
 *
 * This function determines how many columns the input string `s` will occupy
 * when displayed, taking into account multi-byte UTF-8 characters and their
 * respective display widths.
 *
 * @param s A pointer to a null-terminated UTF-8 encoded string.
 * @return The number of display columns required for the string.
 */
int utf8_display_width(const char *s);

/**
 * Replaces the string pointed to by dest with a duplicate of src.
 *
 * Frees the memory pointed to by *dest (if not NULL), allocates new memory,
 * and copies the contents of src into it. Updates *dest to point to the new string.
 *
 * @param dest Pointer to the destination string pointer to be replaced.
 * @param src  Source string to duplicate.
 * @return     0 on success, non-zero on failure (e.g., memory allocation error).
 */
int replace_strdup(const char **dest, const char *src);

/**
 * Replaces the string pointed to by `*dest` with a duplicate of `src`.
 * Frees the memory previously pointed to by `*dest` if necessary.
 *
 * @param dest Pointer to the destination string pointer. The memory pointed to by `*dest` will be freed and replaced.
 * @param src  Source string to duplicate and assign to `*dest`.
 * @return     0 on success, non-zero on failure (e.g., memory allocation error).
 */
int replace_strdup_owned(const char **dest, const char *src);

/**
 * @brief Handles a signal by setting a stop request flag.
 *
 * This function is intended to be used as a signal handler. When invoked,
 * it sets the value pointed to by stop_requested to indicate that a stop
 * has been requested, typically in response to signals such as SIGINT or SIGTERM.
 *
 * @param sig The signal number received.
 * @param stop_requested Pointer to a sig_atomic_t flag that will be set to request stopping.
 */
void handle_signal(int sig, volatile sig_atomic_t *stop_requested);

/**
 * @brief Validates the length of a given file path.
 *
 * This function checks whether the provided path string meets the required length constraints.
 * It can be used to ensure that file paths do not exceed system or application limits.
 *
 * @param path The file path to validate.
 * @param label A descriptive label for the path, used in error messages.
 * @return Returns 0 if the path length is valid, or a non-zero error code if invalid.
 */
int validate_path_length(const char *path, const char *label);

/**
 * trim_string_inplace
 *
 * Trim leading and trailing whitespace from a string in-place.
 * 
 * Modifies the input string by advancing the start pointer past leading
 * whitespace and null-terminating at the end of trailing whitespace.
 * The caller's pointer is updated to point to the first non-whitespace character.
 *
 * @param str Pointer to string to trim (may be NULL or empty)
 * @return Pointer to first non-whitespace character, or original pointer
 *         if already at start. Returns str unchanged if str is NULL.
 *
 * Example:
 *   char buf[] = "  hello world  ";
 *   char *p = trim_string_inplace(buf);
 *   // p now points to 'h', buf contains "hello world\0...", rest is garbage
 *
 * Note: This modifies the string in-place and may be unsafe for string
 * literals. Use on modifiable buffers only.
 */
char* trim_string_inplace(char *str);

/**
 * @brief Parse and validate a PID list string.
 *
 * Parses a PID list specification string into an array of integers. Supports:
 * - Single value (e.g., "150") for auto-increment starting from that PID
 * - Comma-separated list (e.g., "150,151,152") for explicit PID assignment
 *
 * @param pid_str Input string containing PIDs to parse (may be NULL)
 * @param pids_out Pointer to array of parsed PIDs (allocates memory)
 * @param count_out Pointer to store count of parsed PIDs
 * @param errmsg Buffer (at least 256 bytes) for error message if validation fails
 * @return 0 on success, -1 on parsing error, 1 if invalid range
 */
int parse_pid_list(const char *pid_str, int **pids_out, int *count_out, char *errmsg);

/**
 * @brief Parse subtitle positioning specification string into per-track configs.
 *
 * Parses positioning specification format:
 *   "position[,margin_top,margin_left,margin_bottom,margin_right];..."
 * Example:
 *   "bottom-center,5.0;top-left,3.0,2.0"
 *   "center" (uses defaults for all margins)
 *
 * Valid positions: top-left, top-center, top-right, middle-left, middle-center,
 *                  middle-right, bottom-left, bottom-center, bottom-right
 *
 * Margins are percentages (0.0-50.0) of canvas dimensions.
 *
 * @param spec_str Positioning specification string (may be NULL for defaults)
 * @param configs Array of SubtitlePositionConfig[8] to populate
 * @param ntracks Number of tracks to configure (1-8)
 * @param errmsg Buffer (at least 256 bytes) for error message if validation fails
 * @return 0 on success, -1 on parse error, 1 if invalid values
 */
int parse_subtitle_positions(const char *spec_str, SubtitlePositionConfig *configs, 
                             int ntracks, char *errmsg);

/**
 * Extract ASS/SSA alignment number from Pango markup and remove the tag.
 *
 * Searches for {\an<digit>} tags in the markup string (ASS alignment override).
 * ASS alignment values map to numeric keypad positions:
 *   7 8 9  (TOP_LEFT, TOP_CENTER, TOP_RIGHT)
 *   4 5 6  (MID_LEFT, MID_CENTER, MID_RIGHT)
 *   1 2 3  (BOT_LEFT, BOT_CENTER, BOT_RIGHT)
 *   0      (DEFAULT - use CLI positioning)
 *
 * If found, returns the alignment value (1-9), converts it to SubtitlePosition,
 * and MODIFIES the input markup string by removing the {\an<digit>} tag.
 * If not found or value is 0, returns NULL (use CLI positioning).
 * 
 * IMPORTANT: The markup string MUST be modifiable (not const). The function
 * modifies it in-place to remove the ASS alignment tag before rendering.
 *
 * @param markup Pango markup string (WILL BE MODIFIED to remove {\an<digit>} tag)
 * @return Allocated SubtitlePositionConfig with ASS position if found, NULL otherwise
 */
SubtitlePositionConfig* extract_ass_alignment(char *markup);

#endif

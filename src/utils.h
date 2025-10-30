/*  
* Copyright (c) 2025 Mark E. Rosche, Chili IPTV Systems
* All rights reserved.
*
* This software is licensed under the "Personal Use License" described below.
*
* ────────────────────────────────────────────────────────────────
* PERSONAL USE LICENSE
* ────────────────────────────────────────────────────────────────
* Permission is hereby granted, free of charge, to any individual person
* using this software for personal, educational, or non-commercial purposes,
* to use, copy, modify, merge, publish, and/or build upon this software,
* provided that this copyright and license notice appears in all copies
* or substantial portions of the Software.
*
* ────────────────────────────────────────────────────────────────
* COMMERCIAL USE
* ────────────────────────────────────────────────────────────────
* Commercial use of this software, including but not limited to:
*   • Incorporation into a product or service sold for profit,
*   • Use within an organization or enterprise in a revenue-generating activity,
*   • Modification, redistribution, or hosting as part of a commercial offering,
* requires a separate **Commercial License** from the copyright holder.
*
* To obtain a commercial license, please contact:
*   [Mark E. Rosche | Chili-IPTV Systems]
*   Email: [license@chili-iptv.info]  
*   Website: [www.chili-iptv.info]
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
* Summary:
*   ✓ Free for personal, educational, and hobbyist use.
*   ✗ Commercial use requires a paid license.
* ────────────────────────────────────────────────────────────────
*/

#pragma once
#ifndef UTILS_H
#define UTILS_H

#include <signal.h>

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

int validate_path_length(const char *path, const char *label);

#endif

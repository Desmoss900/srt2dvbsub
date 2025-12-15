/*
* Copyright (c) 2025 Mark E. Rosche, Capsaworks Project
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
*   Mark E. Rosche, Capsaworks Project
*   Email: license@capsaworks-project.de
*   Website: www.capsaworks-project.de
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

#ifndef RENDER_PARAMS_H
#define RENDER_PARAMS_H

/**
 * @file render_params.h
 * @brief Rendering parameter validation for font size and color strings.
 *
 * Provides robust validation and parsing of CLI rendering parameters (fontsize,
 * foreground color, outline color, shadow color) with clear error reporting.
 *
 * Design:
 * - Validation happens at CLI parsing time (early error detection)
 * - Uses strtol() for numeric parsing (not atoi())
 * - Color format validation (#RRGGBB or #AARRGGBB only)
 * - No silent fallback to defaults; all errors reported to user
 * - Bounds checking for fontsize (6-200 points, with 0 = adaptive)
 */

/**
 * validate_fontsize
 *
 * Parse and validate a font size string from CLI argument.
 *
 * Accepts:
 * - "0": Special case for adaptive sizing based on display height
 * - "6" to "200": Fixed font size in points
 *
 * Rejects:
 * - Negative values (e.g., "-10")
 * - Values > 200 (e.g., "250")
 * - Non-numeric input (e.g., "abc")
 * - Empty string
 *
 * @param fontsize_str Input string from CLI (e.g., "--fontsize 20")
 * @param out_fontsize Pointer to int where validated fontsize is stored
 * @param errmsg Pointer to string buffer for error details (size >= 256 bytes)
 *               On error, contains message like:
 *               "Font size must be 0 (adaptive) or 6-200 points"
 *
 * @return 0 on success, -1 on validation error
 *
 * @note fontsize=0 is valid and triggers adaptive sizing based on display height
 * @note fontsize=6-200 are fixed sizes; values outside this range are rejected
 * @note Uses strtol() with proper error handling (not atoi())
 * @note Rejects leading zeros for clarity (0 is OK, 00 is rejected)
 */
int validate_fontsize(const char *fontsize_str, int *out_fontsize, char *errmsg);

/**
 * validate_color
 *
 * Validate a color string from CLI argument.
 *
 * Accepted formats:
 * - "#RRGGBB": 6-character hex (3 bytes RGB)
 * - "#AARRGGBB": 8-character hex (4 bytes ARGB, alpha-first)
 *
 * Rejected formats:
 * - Named colors (e.g., "red", "white")
 * - CSS format (e.g., "rgb(255,0,0)")
 * - Alternate hex (e.g., "0xFF0000", "#FFF", "#FF00FF00")
 * - Malformed hex (e.g., "#GGGGGG", "#RGB")
 *
 * @param color_str Input string from CLI (e.g., "--fgcolor #FF0000")
 * @param errmsg Pointer to string buffer for error details (size >= 256 bytes)
 *               On error, contains message like:
 *               "Color must be in #RRGGBB or #AARRGGBB format (got: #GGGGGG)"
 *
 * @return 0 on success (color is valid), -1 on validation error
 *
 * @note Validation is strict: no silent fallback to default colors
 * @note Does NOT validate against actual color values (only format)
 * @note Case-insensitive hex digits (both #FF0000 and #ff0000 accepted)
 * @note No color space validation (caller may choose palette optimization)
 */
int validate_color(const char *color_str, char *errmsg);

/**
 * get_fontsize_usage
 *
 * Get a formatted string describing valid font size options for help/error output.
 *
 * @return Pointer to static string (do NOT free)
 *         Example: "0 (adaptive) or 6-200 (fixed)"
 */
const char *get_fontsize_usage(void);

/**
 * get_color_usage
 *
 * Get a formatted string describing valid color formats for help/error output.
 *
 * @return Pointer to static string (do NOT free)
 *         Example: "#RRGGBB or #AARRGGBB format"
 */
const char *get_color_usage(void);

#endif

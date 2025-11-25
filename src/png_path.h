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

#ifndef PNG_PATH_H
#define PNG_PATH_H

/**
 * @file png_path.h
 * @brief PNG debug output path management and validation.
 *
 * Provides configurable management of PNG debug output directory with:
 * - Path validation and creation
 * - Permission checking
 * - Safe filename generation
 * - Error handling with clear messages
 *
 * Usage:
 * @code
 *   char errmsg[256] = {0};
 *   if (init_png_path("/tmp/debug", errmsg) < 0) {
 *       fprintf(stderr, "Error: %s\n", errmsg);
 *       return 1;
 *   }
 *   // PNG output now goes to /tmp/debug/
 *   char pngfn[PATH_MAX];
 *   make_png_filename(pngfn, sizeof(pngfn), seq++, track, cue);
 *   save_bitmap_png(&bitmap, pngfn);
 * @endcode
 */

/**
 * init_png_path
 *
 * Initialize PNG output directory with validation and creation.
 *
 * If custom_path is provided:
 * - Validates path format and safety
 * - Attempts to create directory if it doesn't exist
 * - Checks write permissions
 * - Falls back to /tmp if path is not writable
 *
 * If custom_path is NULL:
 * - Uses default "pngs/" (relative path)
 * - Creates directory in current working directory
 * - Falls back to /tmp if creation fails
 *
 * @param custom_path Desired PNG output directory (NULL for default "pngs/")
 *                    Can be relative (e.g., "./debug") or absolute (e.g., "/tmp/debug")
 * @param errmsg Pointer to string buffer for error details (size >= 256 bytes)
 *               On error, contains message like:
 *               "Cannot create directory /readonly: Permission denied"
 *               or "Directory /nonexistent/path cannot be created: No such file or directory"
 *
 * @return 0 on success (directory ready for use), -1 on fatal error
 *         Warning: Even on fallback to /tmp, returns 0 (not fatal)
 *
 * @note Should be called once at startup before PNG files are generated
 * @note Multiple calls are safe; reconfigures PNG path
 * @note Default path is "./pngs/" (relative to current working directory)
 */
int init_png_path(const char *custom_path, char *errmsg);

/**
 * get_png_output_dir
 *
 * Get the current PNG output directory.
 *
 * Returns the directory configured by init_png_path() or CLI option.
 * Useful for logging and diagnostics.
 *
 * @return Pointer to static string containing PNG output directory path
 *         Default: "pngs/" (relative path)
 *         Do NOT free this pointer
 */
const char *get_png_output_dir(void);

/**
 * make_png_filename
 *
 * Generate a safe PNG filename with full path.
 *
 * Creates filename in format: <png-dir>/srt_<seq>_t<track>_c<cue>.png
 *
 * Example output:
 * - pngs/srt_001_t00_c042.png
 * - /tmp/debug/srt_001_t00_c042.png
 *
 * @param output Buffer where filename path is stored
 * @param output_len Length of output buffer (should be >= PATH_MAX)
 * @param sequence Sequence number for PNG (0-999, wraps if needed)
 * @param track Track index (0-7, clipped to range)
 * @param cue Cue index (0-999, clipped to range)
 *
 * @return 0 on success (output buffer contains valid path)
 *         -1 if output buffer too small or invalid parameters
 *
 * @note Validates input ranges; clamps track (0-7) and cue (0-999)
 * @note Path includes configured PNG output directory
 * @note Does NOT create file; just generates safe filename
 */
int make_png_filename(char *output, size_t output_len,
                      int sequence, int track, int cue);

/**
 * cleanup_png_path
 *
 * Free any resources allocated by PNG path management.
 *
 * Call this at program exit or when shutting down PNG output.
 * Safe to call multiple times.
 */
void cleanup_png_path(void);

/**
 * get_png_path_usage
 *
 * Get formatted usage string for PNG path configuration.
 *
 * @return Pointer to static string describing valid PNG path formats
 *         Example: "Relative (./pngs) or absolute (/tmp/debug)"
 *         Do NOT free this pointer
 */
const char *get_png_path_usage(void);

#endif

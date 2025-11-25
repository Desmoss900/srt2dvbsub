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
#ifndef DEBUG_PNG_H
#define DEBUG_PNG_H

#include "render_pango.h"

/**
 * @file debug_png.h
 * @brief Debug helper to write internal Bitmaps to PNG files.
 *
 * This API is a small development helper used to persist rendered
 * subtitle bitmaps (the project's internal `Bitmap` structure) as
 * PNG images. It is intended for diagnostics and should only be used
 * when the main program enables debug output (for example via a
 * `--debug` flag).
 *
 * The implementation attempts to create parent directories for the
 * target filename and prints a concise status message to stderr on
 * success or failure. The function is intentionally non-fatal: it
 * returns silently when given invalid input and reports errors only
 * via stderr.
 *
 * Example:
 * @code
 *   Bitmap *bm = render_some_subtitle(...);
 *   save_bitmap_png(bm, "pngs/sub_0001.png");
 * @endcode
 */

/**
 * Save a rendered Bitmap as a PNG file.
 *
 * @param bm Pointer to a Bitmap produced by the renderer. The function
 *           expects `bm->idxbuf` and `bm->palette` to be present and
 *           valid. If `bm` is NULL or missing required data, the
 *           function returns without side effects.
 * @param filename Path where the PNG should be written (relative or
 *                 absolute). Parent directories will be created when
 *                 possible.
 *
 * Behavior:
 *   - Creates an ARGB Cairo surface, expands palette indices into
 *     ARGB pixels, writes the PNG, and prints a status message to
 *     stderr. On failure, an explanatory message is also printed.
 */
void save_bitmap_png(const Bitmap *bm, const char *filename);

#endif
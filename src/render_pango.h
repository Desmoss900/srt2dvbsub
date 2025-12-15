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

#pragma once
#ifndef RENDER_PANGO_H
#define RENDER_PANGO_H

#include <stdint.h>
#include <stddef.h>
#include "runtime_opts.h"

/**
 * @file render_pango.h
 * @brief Pango/Cairo-based subtitle rendering helpers.
 *
 * This header declares a small renderer that converts Pango markup into an
 * indexed Bitmap (palette + index buffer) suitable for DVB subtitle
 * packaging. The implementation uses a thread-local PangoFontMap and
 * performs adaptive supersampled rendering, small linear-light blurs,
 * error-diffusion dithering, and cleanup passes to produce compact
 * 16-color indexed output.
 *
 * Ownership: callers receive an allocated `Bitmap` and must free
 * `idxbuf` and `palette` when finished.
 */

/**
 * Bitmap
 * ------
 * Internal indexed bitmap returned by the rendering pipeline. Consumers
 * receive `idxbuf` (width*height bytes, one palette index per pixel) and
 * `palette` (array of 32-bit ARGB entries). The caller is responsible for
 * freeing both `idxbuf` and `palette` when finished.
 */
typedef struct {
    uint8_t *idxbuf;    /**< One-byte-per-pixel palette indices (row-major) */
    uint32_t *palette;  /**< Array of 32-bit ARGB palette entries (host endianness) */
    int w,h,x,y;        /**< Width/height and top-left position in video coords */
    int nb_colors;      /**< Number of valid colors in `palette` (typically 16) */
    size_t idxbuf_len;  /**< Number of bytes allocated in idxbuf (width*height) */
    size_t palette_bytes; /**< Number of bytes allocated in palette (nb_colors * 4) */
} Bitmap;

/**
 * Render Pango markup text into an indexed Bitmap.
 *
 * @param markup        Pango markup string (may be produced by srt_to_pango_markup()).
 * @param disp_w        Display width in pixels.
 * @param disp_h        Display height in pixels.
 * @param fontsize      If >0, force font size; otherwise an adaptive size is chosen.
 * @param fontfam       Font family to use (NULL -> default).
 * @param fontstyle     Optional font style/variant string appended to the family (NULL -> default).
 * @param fgcolor       Foreground color string in "#RRGGBB" or "#AARRGGBB" form.
 * @param outlinecolor  Outline/stroke color string.
 * @param shadowcolor   Shadow color string.
 * @param pos_config    Positioning configuration with position (1-9) and margins. If NULL, uses default.
 * @param palette_mode  Palette hint forwarded to init_palette() (e.g., "broadcast").
 * @return Bitmap with allocated idxbuf and palette on success. Caller must free both.
 */
Bitmap render_text_pango(const char *markup,
                         int disp_w, int disp_h,
                         int fontsize, const char *fontfam,
                         const char *fontstyle,
                         const char *fgcolor,
                         const char *outlinecolor,
                         const char *shadowcolor,
                         const char *bgcolor,
                         SubtitlePositionConfig *pos_config,
                         const char *palette_mode);

/**
 * Convert SRT cue text to Pango markup.
 *
 * This converts a small subset of inline SRT/HTML tags (e.g. <b>, <i>,
 * <u>, and <font color="#RRGGBB">) into Pango-compatible markup and
 * escapes XML entities. Returns a newly-allocated NUL-terminated string
 * which the caller must free with free().
 *
 * @param srt_text   Input SRT cue text (UTF-8).
 * @return Allocated Pango markup string, or an empty string on allocation failure.
 */
char* srt_to_pango_markup(const char *srt_text);

/**
 * Parse a hex color string (#RRGGBB or #AARRGGBB) to normalized RGBA.
 *
 * Missing or malformed input defaults to opaque white (1.0,1.0,1.0,1.0).
 *
 * @param hex  Color string to parse (may be NULL).
 * @param r,g,b,a  Output pointers receiving components in [0,1].
 */
void parse_hex_color(const char *hex, double *r, double *g, double *b, double *a);

/**
 * Parse background color in #RRGGBB format (RGB only, always opaque).
 *
 * Accepts only 6-digit hex colors (#RRGGBB). Background colors are always
 * fully opaque (alpha=1.0). Malformed input defaults to opaque white.
 *
 * @param hex  Color string to parse in #RRGGBB format (may be NULL).
 * @param r,g,b,a  Output pointers (a will always be 1.0).
 */
void parse_bgcolor(const char *hex, double *r, double *g, double *b, double *a);

/**
 * Cleanup per-thread resources allocated by the Pango renderer.
 *
 * Call this from the main thread (or the thread that owns fontconfig
 * finalization) before calling FcFini() to deterministically release
 * Pango/Cairo objects associated with this thread.
 */
void render_pango_cleanup(void);

/**
 * Force a specific supersample factor for rendering. When >0, supersampling
 * is fixed to this value instead of being chosen adaptively.
 */
void render_pango_set_ssaa_override(int ssaa);

/**
 * Disable the unsharp sharpening pass when non-zero. Useful for testing
 * or to avoid potential artifacts on some content.
 */
void render_pango_set_no_unsharp(int no_unsharp);

/**
 * Validate and resolve font family and style.
 *
 * Attempts to use the specified font. If it doesn't exist:
 *   1. If user specified a font (user_font != NULL), try preferred defaults
 *   2. If no user font specified, check preferred defaults
 *   3. If preferred fonts fail, error out
 *
 * For style validation, attempts to use specified style. If it doesn't exist,
 * falls back to "Thin", "Light", "Regular", or "Medium" in that order.
 *
 * @param user_font     User-specified font (NULL if using defaults)
 * @param user_style    User-specified style (NULL if using defaults)
 * @param out_font      Output: pointer to resolved font name (caller must free)
 * @param out_style     Output: pointer to resolved style (caller must free), may be NULL
 * @return              0 on success, -1 on error (fonts not available)
 */
int validate_and_resolve_font(const char *user_font, const char *user_style,
                               char **out_font, char **out_style);

/**
 * Check if a specific font family exists on the system.
 *
 * @param font_name     Font family name to check
 * @return              1 if font exists, 0 if not
 */
int font_exists(const char *font_name);

/**
 * Check if a specific font style exists for a font family.
 *
 * @param font_name     Font family name
 * @param style_name    Style name to check (e.g., "Bold", "Italic", "Light")
 * @return              1 if style exists, 0 if not
 */
int font_style_exists(const char *font_name, const char *style_name);

#endif

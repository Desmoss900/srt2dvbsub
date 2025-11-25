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
#ifndef RENDER_ASS_H
#define RENDER_ASS_H

/**
 * @file render_ass.h
 * @brief libass-based renderer wrapper and helpers.
 *
 * This header declares a small wrapper around libass to create a renderer,
 * manage ASS tracks/events, and rasterize ASS subtitles into the project's
 * `Bitmap` format used by the DVB muxing pipeline. The implementation
 * provides stub typedefs when libass is unavailable so the project can be
 * built without libass support.
 *
 * Ownership: callers receiving a `Bitmap` must free `idxbuf` and `palette`.
 */

#include <stdint.h>
#include "dvb_sub.h"   // for Bitmap struct

/* If libass is available (configure --enable-ass) include its headers.
 * Otherwise provide opaque typedefs so callers can compile and link against
 * a stub implementation (render_ass_stub.c) without requiring libass.
 */
#ifdef HAVE_LIBASS
#include <ass/ass.h>
#else
typedef struct ASS_Library ASS_Library;
typedef struct ASS_Renderer ASS_Renderer;
typedef struct ASS_Track ASS_Track;
#endif

/**
 * render_ass_init - initialize the libass library
 *
 * Initialize libass and allocate an ASS_Library instance used by the
 * renderer and track APIs. On success the returned pointer must be
 * released via render_ass_done() (or render_ass_free_lib()).
 *
 * Returns:
 *  - pointer to an initialized ASS_Library on success
 *  - NULL on failure
 */
ASS_Library* render_ass_init();

/**
 * Create and configure an ASS_Renderer sized to the given width and height.
 *
 * @param lib Initialized ASS_Library pointer.
 * @param w   Frame width in pixels.
 * @param h   Frame height in pixels.
 * @return Pointer to a configured ASS_Renderer on success, or NULL on failure.
 */
ASS_Renderer* render_ass_renderer(ASS_Library *lib, int w, int h);

/**
 * Allocate a new ASS_Track container for styles and events.
 *
 * @param lib Initialized ASS_Library pointer.
 * @return Pointer to an ASS_Track on success, or NULL on failure.
 *
 * Note: callers should free duplicated event text via render_ass_free_track()
 * and release track resources according to libass usage rules.
 */
ASS_Track* render_ass_new_track(ASS_Library *lib);

/**
 * Append a timed subtitle event to a track.
 *
 * @param track    ASS_Track to receive the event (must be non-NULL).
 * @param text     NUL-terminated UTF-8 text (may be NULL; treated as empty).
 * @param start_ms Start time in milliseconds.
 * @param end_ms   End time in milliseconds.
 *
 * The function duplicates `text` into libass-managed memory; the caller
 * retains ownership of the original pointer. If event allocation fails the
 * function returns silently.
 */
void render_ass_add_event(ASS_Track *track,
                          const char *text,
                          int64_t start_ms,
                          int64_t end_ms);

/**
 * Render the ASS track at a timestamp into a Bitmap.
 *
 * @param renderer    Configured ASS_Renderer (must be non-NULL).
 * @param track       ASS_Track to render (may be NULL -> empty Bitmap).
 * @param now_ms      Timestamp in milliseconds.
 * @param palette_mode Textual hint forwarded to init_palette() (may be NULL).
 * @return A Bitmap with allocated `idxbuf` and `palette` on success, or an
 *         empty Bitmap (w==0) on no-content or failure. Caller must free
 *         `bm.idxbuf` and `bm.palette` when done.
 */
Bitmap render_ass_frame(ASS_Renderer *renderer,
                        ASS_Track *track,
                        int64_t now_ms,
                        const char *palette_mode);

/**
 * Cleanup renderer and library resources.
 *
 * @param lib      ASS_Library pointer previously returned by render_ass_init().
 * @param renderer ASS_Renderer pointer previously returned by render_ass_renderer().
 *
 * The function calls ass_renderer_done(renderer) and ass_library_done(lib)
 * when the respective pointers are non-NULL.
 */
void render_ass_done(ASS_Library *lib, ASS_Renderer *renderer);

/**
 * Install a minimal Default ASS style for a track.
 *
 * @param track   ASS_Track to receive the style (must be non-NULL).
 * @param font    Font family name passed to ASS style.
 * @param size    Font size in ASS units.
 * @param fg      Foreground color string in "#RRGGBB" or "#AARRGGBB" form.
 * @param outline Outline color string in the same format.
 * @param shadow  Shadow/back color string in the same format.
 *
 * The function builds an in-memory ASS header with a "Default" style and
 * feeds it to libass via ass_process_data().
 */
void render_ass_set_style(ASS_Track *track,
                          const char *font, int size,
                          const char *fg, const char *outline, const char *shadow);


/**
 * @brief Converts a hexadecimal color string to ASS (Advanced SubStation Alpha) color format.
 *
 * This function takes a color specified as a hexadecimal string (e.g., "#RRGGBB" or "RRGGBB")
 * and converts it to the ASS color format, storing the result in the provided output buffer.
 *
 * @param hex      The input hexadecimal color string.
 * @param out      The output buffer to store the ASS color string.
 * @param outsz    The size of the output buffer.
 */
void render_ass_hex_to_ass_color(const char *hex, char *out, size_t outsz);

/**
 * Print a human-readable dump of styles in a track to stderr.
 *
 * @param track ASS_Track pointer to inspect. If NULL the function prints an
 *              error message and returns.
 */
void render_ass_debug_styles(ASS_Track *track);

/** Convenience/free wrappers matching older name expectations */
void render_ass_free_track(ASS_Track *track);
void render_ass_free_renderer(ASS_Renderer *renderer);
void render_ass_free_lib(ASS_Library *lib);

/*
 * Thread-safety helpers
 * ---------------------
 * libass' objects (ASS_Renderer, ASS_Track) are not guaranteed to be
 * safe for concurrent access from multiple threads when the same object
 * is shared. Preferred patterns are:
 *  - Create one ASS_Renderer per thread and avoid sharing it, or
 *  - Serialize access to shared renderer/track objects using a mutex.
 *
 * The following coarse-grained lock/unlock helpers provide a simple
 * serialization primitive callers may use when they cannot avoid sharing
 * libass objects. They implement a global mutex and are intentionally
 * lightweight. For higher concurrency, callers should implement a
 * per-renderer locking scheme or use one renderer per thread.
 */
void render_ass_lock(void);
void render_ass_unlock(void);

/* Validate an ASS image tile's basic fields. Returns 1 if the tile appears
 * sane for rasterization (bitmap non-NULL, stride >= width, reasonable
 * size), or 0 if the tile should be skipped.
 *
 * This helper is public to allow unit testing without requiring a full
 * libass ASS_Image allocation in the test harness.
 */
int render_ass_validate_image_tile(int w, int h, int stride, const void *bitmap);

#endif
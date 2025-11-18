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

/*
 * AUDIT SUMMARY - render_ass.c
 * ---------------------------
 * This file has been audited and hardened as part of the subtitle
 * rendering review. The following defensive and correctness changes were
 * implemented and verified with unit tests in `testharness/`:
 *
 *  - NULL checks & ownership fixes: public APIs validate inputs and
 *    `render_ass_free_track()` only frees per-event duplicated strings.
 *  - Integer overflow safety: guarded w*h multiplications and casts,
 *    with reverse-division checks to detect overflow before allocations.
 *  - Magic-sentinel removal: uses <limits.h> (INT_MIN/INT_MAX) instead
 *    of arbitrary sentinel values.
 *  - Allocation failure handling: malloc/calloc/strdup return values are
 *    checked and resources cleaned up on error paths.
 *  - Robust hex parsing: added `render_ass_hex_to_ass_color()` that
 *    safely parses `#RRGGBB` and `#AARRGGBB` inputs (unit-tested).
 *  - Header truncation safety: snprintf return values are checked and
 *    dynamic allocation fallback is used on buffer truncation.
 *  - Palette & color caches: small mutex-protected LRU palette cache
 *    and per-frame color->palette-index cache to limit repeated work.
 *  - ASS_Image validation: `render_ass_validate_image_tile()` checks
 *    bitmap/stride/size to avoid rasterizing corrupted tiles (unit-tested).
 *  - Consistent logging: replaced ad-hoc fprintf() with LOG() macro;
 *    `DEBUG_MODULE` is defined at the top of this file.
 *  - Thread-safety helpers: `render_ass_lock()` / `render_ass_unlock()`
 *    provide a coarse-grained serialization primitive for shared use.
 *
 * Unit tests added and passing (see `testharness/test_render_ass.c`):
 *  - Hex color parsing
 *  - Tile validation
 *  - Locking concurrency check
 *  - Optional libass smoke test (skipped if libass unavailable)
 *
 * The audit is considered complete for the items tracked in the
 * repository TODO list. If you want additional coverage (e.g., more
 * rasterization edge-cases or performance benches) we can add more
 * targeted unit tests.
 */

/*
 * render_ass.c
 *
 * A thin wrapper around libass to allow rendering ASS/SSA formatted subtitles
 * into the project's `Bitmap` structure. The wrapper converts libass image
 * frames into a palette-indexed bitmap using the project's palette helpers
 * (init_palette / nearest_palette_index).
 *
 * Ownership notes:
 *  - Bitmaps returned by render_ass_frame allocate `idxbuf` and `palette`.
 *    Callers must free both when finished (free(bitmap.idxbuf); free(bitmap.palette)).
 *  - render_ass_add_event duplicates the text string into libass-managed
 *    event structures; callers retain ownership of the original text pointer.
 */

#define _POSIX_C_SOURCE 200809L
#include "render_ass.h"
#include "palette.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <pthread.h>

/* Provide a short module name used by LOG() in debug.h */
#define DEBUG_MODULE "render_ass"
#include "debug.h"


/*
 * Coarse-grained mutex for serializing libass operations when callers
 * share renderer/track objects across threads. Prefer per-thread
 * renderers where possible; these helpers are provided for the
 * convenience of callers that must share objects.
 */
static pthread_mutex_t render_ass_global_lock = PTHREAD_MUTEX_INITIALIZER;

void render_ass_lock(void) {
    pthread_mutex_lock(&render_ass_global_lock);
}

void render_ass_unlock(void) {
    pthread_mutex_unlock(&render_ass_global_lock);
}


/*
 * Converts a single hexadecimal character to its integer value.
 * Accepts characters '0'-'9', 'a'-'f', and 'A'-'F'.
 * Returns the corresponding integer value (0-15) if valid,
 * or -1 if the character is not a valid hexadecimal digit.
 */
static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

/**
 * Parses two hexadecimal characters from the input string and stores the result as a byte.
 *
 * param s   Pointer to a string containing at least two hexadecimal characters.
 * param out Pointer to an unsigned variable where the parsed byte will be stored.
 * return    0 on success, -1 if either character is not a valid hexadecimal digit.
 */
static int parse_hex_byte(const char *s, unsigned *out) {
    int hi = hex_val(s[0]);
    int lo = hex_val(s[1]);
    if (hi < 0 || lo < 0) return -1;
    *out = (unsigned)((hi << 4) | lo);
    return 0;
}


/**
 * Converts a hex color string to ASS (Advanced SubStation Alpha) color format.
 *
 * This function takes a color string in the format "#RRGGBB" or "#AARRGGBB" and converts it
 * to the ASS color format "&HAABBGGRR", where AA is the inverted alpha channel, BB is blue,
 * GG is green, and RR is red. If the input is invalid, it defaults to white ("&H00FFFFFF").
 *
 * param hex   Input color string in hex format ("#RRGGBB" or "#AARRGGBB").
 * param out   Output buffer to store the ASS color string.
 * param outsz Size of the output buffer.
 */
void render_ass_hex_to_ass_color(const char *hex, char *out, size_t outsz) {
    unsigned r = 255, g = 255, b = 255, a = 0;

    if (!hex || hex[0] != '#') {
        snprintf(out, outsz, "&H00FFFFFF");
        return;
    }
    size_t len = strlen(hex);
    if (len == 7) {
        /* #RRGGBB */
        unsigned vr, vg, vb;
        if (parse_hex_byte(hex + 1, &vr) < 0 ||
            parse_hex_byte(hex + 3, &vg) < 0 ||
            parse_hex_byte(hex + 5, &vb) < 0) {
            snprintf(out, outsz, "&H00FFFFFF");
            return;
        }
        r = vr; g = vg; b = vb; a = 0x00;
    } else if (len == 9) {
        /* #AARRGGBB */
        unsigned va, vr, vg, vb;
        if (parse_hex_byte(hex + 1, &va) < 0 ||
            parse_hex_byte(hex + 3, &vr) < 0 ||
            parse_hex_byte(hex + 5, &vg) < 0 ||
            parse_hex_byte(hex + 7, &vb) < 0) {
            snprintf(out, outsz, "&H00FFFFFF");
            return;
        }
        a = va; r = vr; g = vg; b = vb;
    } else {
        snprintf(out, outsz, "&H00FFFFFF");
        return;
    }

    unsigned inv_a = 0xFF - (a & 0xFF);
    snprintf(out, outsz, "&H%02X%02X%02X%02X", inv_a, b & 0xFF, g & 0xFF, r & 0xFF);
}

/* Backwards-compatibility wrapper for internal callers that referenced
 * the older static helper name `hex_to_ass_color`. Keep a thin wrapper
 * so existing call-sites need not be modified. */
static void hex_to_ass_color(const char *hex, char *out, size_t outsz) {
    render_ass_hex_to_ass_color(hex, out, outsz);
}

/**
 * Validates the parameters for an ASS image tile.
 *
 * Performs basic sanity checks on the width, height, stride, and bitmap pointer.
 * Ensures that the stride is at least as large as the width, and that all values are positive.
 * Applies an upper limit to the total number of pixels to prevent excessive allocations or loops,
 * guarding against corrupted or malicious input.
 *
 * param w       Width of the image tile in pixels.
 * param h       Height of the image tile in pixels.
 * param stride  Number of bytes per row in the bitmap.
 * param bitmap  Pointer to the bitmap data.
 * return        1 if the parameters are valid, 0 otherwise.
 */
int render_ass_validate_image_tile(int w, int h, int stride, const void *bitmap) {
    /* Basic sanity checks */
    if (!bitmap) return 0;
    if (w <= 0 || h <= 0) return 0;
    if (stride <= 0) return 0;
    if (stride < w) return 0;
    /* Prevent absurdly large allocations / loops; arbitrary cap to avoid
     * unbounded work if libass produced corrupted dimensions. This value
     * is intentionally conservative; tiles larger than this are unlikely
     * to appear in real-world subtitles. */
    const size_t MAX_TILE_PIXELS = 10 * 1000 * 1000; /* 10M pixels */
    size_t pixels = (size_t)stride * (size_t)h;
    if (pixels == 0 || pixels > MAX_TILE_PIXELS) return 0;
    return 1;
}

/*
 * render_ass_init - initialize the libass library instance
 *
 * Initialize and return an ASS_Library instance using libass'
 * ass_library_init(). This allocates global libass state used by the
 * renderer and tracks. If initialization fails, NULL is returned and
 * the caller should disable ASS rendering.
 *
 * Return value:
 *  - pointer to an initialized ASS_Library on success
 *  - NULL on failure (allocation or internal libass errors)
 *
 * Side-effects:
 *  - The function sets libass' message callback to NULL to silence
 *    libass-native logging (this module controls diagnostics).
 */
ASS_Library* render_ass_init(void) {
    ASS_Library *lib = ass_library_init();
    if (!lib) {
        LOG(0, "libass: failed to init\n");
        return NULL;
    }
    /* Silence libass logging (we control diagnostics through debug_level). */
    ass_set_message_cb(lib, NULL, NULL);
    return lib;
}

/*
 * render_ass_renderer - create and configure an ASS_Renderer
 *
 * Create a renderer instance bound to a specific output size (width
 * `w`, height `h`). The renderer holds font/shaping/renderer state and
 * must be sized to the target surface so libass can compute layout and
 * wrapping correctly.
 *
 * Parameters:
 *  - lib: ASS_Library pointer returned by render_ass_init()
 *  - w: target frame width in pixels
 *  - h: target frame height in pixels
 *
 * Return value:
 *  - pointer to a configured ASS_Renderer on success
 *  - NULL on failure
 *
 * Notes:
 *  - The function configures the default font family hint to "Sans"
 *    and enables fontconfig fallback to keep rendering deterministic
 *    across systems.
 */
ASS_Renderer* render_ass_renderer(ASS_Library *lib, int w, int h) {
    if (!lib) {
        LOG(0, "render_ass_renderer: NULL lib provided\n");
        return NULL;
    }
    ASS_Renderer *r = ass_renderer_init(lib);
    if (!r) return NULL;
    /* Set frame size so libass can compute glyph layout & wrapping correctly. */
    ass_set_frame_size(r, w, h);
    /* Configure fonts: NULL=fontconfig default, "Sans" as family hint,
     * 1=use fontconfig fallback. This keeps behavior deterministic across
     * systems while allowing font substitution. */
    ass_set_fonts(r, NULL, "Sans", 1, NULL, 1);
    return r;
}

/*
 * render_ass_new_track - allocate and initialize an ASS_Track container
 *
 * Create a new ASS_Track suitable for holding events and styles. The
 * returned track is allocated by libass (ass_new_track) and must be
 * managed according to libass usage rules by the caller. Typical usage
 * is to call render_ass_set_style() to install a Default style and then
 * add events via render_ass_add_event().
 *
 * Parameters:
 *  - lib: pointer to an initialized ASS_Library returned by
 *         render_ass_init(); passing NULL may be accepted by the
 *         libass implementation but callers should generally pass a
 *         valid library pointer.
 *
 * Return value:
 *  - On success, returns a pointer to an ASS_Track. The caller is
 *    responsible for freeing any duplicated event Text via
 *    render_ass_free_track() and for releasing the track itself if the
 *    libass version in use requires explicit cleanup.
 *  - Returns NULL on allocation failure.
 */
ASS_Track* render_ass_new_track(ASS_Library *lib) {
    ASS_Track *track = ass_new_track(lib);
    if (track) {
        /* mark the container as ASS/SSA type; some libass internals inspect this */
        track->track_type = TRACK_TYPE_ASS;
    }
    return track;
}

/*
 * render_ass_add_event - append a timed text event to an ASS_Track
 *
 * Create a new event in `track` representing `text` that will be shown
 * from `start_ms` (inclusive) to `end_ms` (exclusive). The function
 * allocates/initializes the ASS_Event slot and duplicates the text into
 * libass-managed memory.
 *
 * Parameters:
 *  - track: pointer to an ASS_Track to receive the event (must be non-NULL)
 *  - text: NUL-terminated UTF-8 text to display (may be NULL -> treated as "")
 *  - start_ms: start time in milliseconds
 *  - end_ms: end time in milliseconds (should be >= start_ms)
 *
 * Behavior and ownership:
 *  - The function calls ass_alloc_event(track) and on success writes the
 *    Start, Duration, Style and Text fields of the newly allocated event.
 *  - The Text field is set via strdup(), and therefore the duplicated
 *    string is owned by libass/track and will be freed by
 *    render_ass_free_track() (or libass cleanup) when appropriate.
 *  - The caller retains ownership of the original `text` pointer.
 *
 * Error handling:
 *  - If ass_alloc_event() fails (returns negative), the function returns
 *    silently and no event is added.
 *  - No additional validation is performed on timestamps; callers should
 *    ensure `end_ms >= start_ms` to avoid negative durations.
 *
 * Diagnostics:
 *  - When the global `debug_level` is greater than 1, a debug line is
 *    printed to stderr describing the added event index and timing.
 */
void render_ass_add_event(ASS_Track *track,
                          const char *text,
                          int64_t start_ms,
                          int64_t end_ms)
{
    if (!track) {
        LOG(0, "render_ass_add_event: NULL track provided\n");
        return;
    }

    /* Allocate an event slot; ass_alloc_event returns a non-negative
     * index on success. */
    int ev = ass_alloc_event(track);
    if (ev < 0) return; /* allocation failure; silently skip event */

    /* Defensive check: ensure events array exists and index is in range. */
    if (!track->events || ev < 0 || ev >= track->n_events) {
        /* Unexpected: libass did not provide a writable event slot. */
        return;
    }

    /* Populate minimal event fields: start time and duration (ms). The
     * library stores times as ints; cast from int64_t accordingly with
     * clamping to avoid truncation issues. */
    if (start_ms < INT_MIN) track->events[ev].Start = INT_MIN;
    else if (start_ms > INT_MAX) track->events[ev].Start = INT_MAX;
    else track->events[ev].Start = (int)start_ms;

    int64_t dur = end_ms - start_ms;
    if (dur < INT_MIN) track->events[ev].Duration = INT_MIN;
    else if (dur > INT_MAX) track->events[ev].Duration = INT_MAX;
    else track->events[ev].Duration = (int)dur;

    /* Use style index 0 (Default) unless a different style is injected. */
    track->events[ev].Style = 0;
    /* Duplicate the text into libass-managed memory; the caller retains
     * ownership of the original `text` pointer. Check for allocation
     * failure from strdup and avoid storing a dangling pointer. */
    char *dup_text = strdup(text ? text : "");
    if (!dup_text) {
        LOG(1, "render_ass_add_event: strdup failed for event %d\n", ev);
        track->events[ev].Text = NULL;
    } else {
        track->events[ev].Text = dup_text;
    }

    /* Optional debug trace for developers. */
    if (debug_level > 1) {
        LOG(2,
            "Added event #%d: %lld → %lld ms | text='%s'\n",
            ev,
            (long long)start_ms,
            (long long)end_ms,
            text ? text : "(null)");
    }
}


/*
 * render_ass_frame - render libass images for a timestamp into a Bitmap
 *
 * Render the ASS/SSA `track` at the specified timestamp `now_ms` (in
 * milliseconds) using the provided `renderer`. The function collects the
 * ASS_Image linked list returned by libass, computes a tight union
 * bounding box for all image tiles, allocates a small indexed `Bitmap`
 * covering that box, and rasterizes each tile's coverage mask into the
 * bitmap using a 16-entry palette derived from `palette_mode`.
 *
 * Parameters:
 *  - renderer: initialized ASS_Renderer instance (must be non-NULL)
 *  - track: ASS_Track containing events/styles to render (may be NULL
 *           but will produce an empty Bitmap)
 *  - now_ms: timestamp in milliseconds to render
 *  - palette_mode: textual hint passed to init_palette() to select which
 *                  palette to initialize (may be NULL)
 *
 * Return value:
 *  - Returns a Bitmap structure. On success, `Bitmap.idxbuf` and
 *    `Bitmap.palette` are heap-allocated and the caller is responsible for
 *    freeing them (free(bm.idxbuf); free(bm.palette)). The returned
 *    Bitmap's `w`, `h`, `x`, and `y` fields describe the allocated
 *    rectangle in the video coordinate space. If there is no visual
 *    content for the given timestamp or an error occurs, an empty Bitmap
 *    with all-zero fields is returned (idxbuf and palette will be NULL).
 *
 * Ownership / side-effects:
 *  - The caller owns and must free both bm.idxbuf and bm.palette when
 *    finished.
 *  - This function does not modify the ASS_Track contents; it only reads
 *    from libass-provided ASS_Image structures. libass retains ownership
 *    of the ASS_Image linked list returned by ass_render_frame.
 *
 * Error handling / edge cases:
 *  - If `ass_render_frame` returns NULL, this function returns an
 *    empty Bitmap.
 *  - The implementation performs bounds checks when writing into the
 *    index buffer to handle negative tile offsets or tiles that extend
 *    beyond the computed union box.
 */
Bitmap render_ass_frame(ASS_Renderer *renderer,
                        ASS_Track *track,
                        int64_t now_ms,
                        const char *palette_mode)
{
    Bitmap bm = {0};
    int detect_change = 0;
    /*
     * Render the ASS/SSA track for the requested timestamp (ms). libass
     * returns a linked list of ASS_Image nodes describing glyph/shape
     * bitmaps and their destinations on the target surface.
     * detect_change is set by libass to indicate whether frame contents
     * have changed since the previous render (not used here but available).
     */
    ASS_Image *img = ass_render_frame(renderer, track, (int)now_ms, &detect_change);

    if (!img) {
        /* No image frames -> empty Bitmap */
        return bm;
    }


    /* Get bounding box */
    int minx = INT_MAX, miny = INT_MAX, maxx = INT_MIN, maxy = INT_MIN;
    /* Walk the linked list of images to compute the union bounding box of
     * all drawn fragments. This tightly bounds our output Bitmap to the
     * minimal rectangle containing all non-empty image tiles. */
    /* Per-frame small color->palette-index cache to avoid repeated
     * nearest_palette_index() work when many tiles share colors.
     */
    const int MAX_COLOR_CACHE = 128;
    struct { uint32_t rgba; int palidx; } color_cache[MAX_COLOR_CACHE];
    int color_cache_len = 0;
    int color_cache_evict = 0;

    for (ASS_Image *cur = img; cur; cur = cur->next) {
        if (cur->w <= 0 || cur->h <= 0) continue; /* skip degenerate tiles */
        if (cur->dst_x < minx) minx = cur->dst_x;
        if (cur->dst_y < miny) miny = cur->dst_y;
        if (cur->dst_x+cur->w > maxx) maxx = cur->dst_x+cur->w;
        if (cur->dst_y+cur->h > maxy) maxy = cur->dst_y+cur->h;
    }
    if (minx > maxx || miny > maxy) return bm;

    int w = maxx - minx;
    int h = maxy - miny;

    /* Guard against absurd sizes and integer overflow when computing
     * the allocation size. Use size_t for multiplication and verify the
     * result by reverse division. */
    if (w <= 0 || h <= 0) return bm;
    size_t pixels = (size_t)w * (size_t)h;
    if (pixels / (size_t)w != (size_t)h) {
        /* Overflow detected */
        LOG(0, "render_ass_frame: size overflow w=%d h=%d\n", w, h);
        return bm;
    }

    bm.w = w;
    bm.h = h;
    bm.x = minx;
    bm.y = miny;

    /* Allocate index buffer (one byte per pixel). Caller must free. */
    bm.idxbuf = calloc(pixels, 1);
    if (!bm.idxbuf) {
        LOG(0, "render_ass_frame: calloc failed for %zu pixels\n", pixels);
        return bm;
    }
    /* record buffer sizes for caller validation */
    bm.idxbuf_len = pixels;

    /* Allocate and initialize the palette used to map ARGB colors to
     * palette indices. We optimize by caching small palettes per
     * `palette_mode` to avoid repeated work across frames. The cache is
     * protected by a mutex to be safe for multi-threaded use.
     */
    {
        const int PALETTE_CACHE_SIZE = 4;
        static pthread_mutex_t palette_cache_lock = PTHREAD_MUTEX_INITIALIZER;
        typedef struct { char *mode; uint32_t palette[16]; int used; } pal_entry_t;
        static pal_entry_t palette_cache[4] = {0};

        uint32_t palbuf[16];
        int cached = 0;

        pthread_mutex_lock(&palette_cache_lock);
        for (int ci = 0; ci < PALETTE_CACHE_SIZE; ci++) {
            if (!palette_cache[ci].used) continue;
            if ((palette_mode == NULL && palette_cache[ci].mode == NULL) ||
                (palette_mode && palette_cache[ci].mode && strcmp(palette_mode, palette_cache[ci].mode) == 0)) {
                /* Move-to-front for LRU */
                pal_entry_t hit = palette_cache[ci];
                for (int j = ci; j > 0; j--) palette_cache[j] = palette_cache[j-1];
                palette_cache[0] = hit;
                memcpy(palbuf, palette_cache[0].palette, sizeof(palbuf));
                cached = 1;
                break;
            }
        }
        pthread_mutex_unlock(&palette_cache_lock);

        if (!cached) {
            /* Compute palette into temporary buffer */
            init_palette(palbuf, palette_mode);
            /* Try to insert into cache */
            int insert_cache = 1;
            char *mode_copy = NULL;
            if (palette_mode) {
                mode_copy = strdup(palette_mode);
                if (!mode_copy) {
                    LOG(0, "render_ass_frame: strdup failed for palette mode key '%s'\n",
                        palette_mode);
                    insert_cache = 0;
                }
            }
            pthread_mutex_lock(&palette_cache_lock);
            if (insert_cache) {
                pal_entry_t evicted = palette_cache[PALETTE_CACHE_SIZE-1];
                for (int j = PALETTE_CACHE_SIZE-1; j > 0; j--) palette_cache[j] = palette_cache[j-1];
                palette_cache[0].used = 1;
                palette_cache[0].mode = mode_copy;
                memcpy(palette_cache[0].palette, palbuf, sizeof(palbuf));
                if (evicted.used && evicted.mode) free(evicted.mode);
            }
            pthread_mutex_unlock(&palette_cache_lock);
            if (!insert_cache && mode_copy) free(mode_copy);
        }

        /* Allocate caller-owned palette and copy values */
        bm.palette = malloc(16 * sizeof(uint32_t));
            if (!bm.palette) {
                LOG(0, "render_ass_frame: malloc failed for palette\n");
                free(bm.idxbuf);
                bm.idxbuf = NULL;
                bm.idxbuf_len = 0;
                return bm;
            }
        memcpy(bm.palette, palbuf, 16 * sizeof(uint32_t));
        bm.palette_bytes = 16 * sizeof(uint32_t);
    }

    /* For each ASS_Image tile, compute the palette index corresponding to
     * the tile's color and blend coverage into the index buffer. libass
     * provides a coverage bitmap (grayscale) in cur->bitmap with stride
     * cur->stride. A non-zero coverage indicates the pixel should receive
     * the tile's color index. We do not perform per-pixel alpha blending
     * into the index buffer here; instead we map any covered pixel to the
     * tile's chosen palette index which matches the DVB approach used in
     * the rest of the pipeline.
     */
    for (ASS_Image *cur = img; cur; cur = cur->next) {
        /* Convert libass color representation to ARGB with correct alpha.
         * libass stores color as 0xAARRGGBB but with an inverted alpha
         * channel; invert it back to standard alpha semantics before
         * nearest-palette lookup. */
        uint32_t argb = cur->color;
        uint8_t a = 255 - ((argb >> 24) & 0xFF);
        uint8_t r = (argb >> 16) & 0xFF;
        uint8_t g = (argb >> 8) & 0xFF;
        uint8_t b = (argb) & 0xFF;
        uint32_t rgba = (a << 24) | (r << 16) | (g << 8) | b;
        int palidx = -1;
        for (int ci = 0; ci < color_cache_len; ci++) {
            if (color_cache[ci].rgba == rgba) { palidx = color_cache[ci].palidx; break; }
        }
        if (palidx < 0) {
            palidx = nearest_palette_index(bm.palette, 16, rgba);
            if (color_cache_len < MAX_COLOR_CACHE) {
                color_cache[color_cache_len].rgba = rgba;
                color_cache[color_cache_len].palidx = palidx;
                color_cache_len++;
            } else {
                color_cache[color_cache_evict].rgba = rgba;
                color_cache[color_cache_evict].palidx = palidx;
                color_cache_evict = (color_cache_evict + 1) % MAX_COLOR_CACHE;
            }
        }

        /* Rasterize coverage mask into the index buffer, offset by the
         * computed union bounding box (minx/miny). We bound-check to avoid
         * writing outside the allocated region when images extend beyond
         * the union box due to rounding or negative dst_x/dst_y. */
        /* Validate bitmap/stride before accessing memory */
        if (!render_ass_validate_image_tile(cur->w, cur->h, cur->stride, cur->bitmap)) {
            /* malformed or unexpectedly large tile; skip */
            continue;
        }

        for (int yy = 0; yy < cur->h; yy++) {
            for (int xx = 0; xx < cur->w; xx++) {
                uint8_t cov = cur->bitmap[yy * cur->stride + xx];
                if (cov > 0) {
                    int dx = cur->dst_x + xx - minx;
                    int dy = cur->dst_y + yy - miny;
                    if (dx >= 0 && dx < w && dy >= 0 && dy < h) {
                        size_t idx = (size_t)dy * (size_t)w + (size_t)dx;
                        if (idx < bm.idxbuf_len) bm.idxbuf[idx] = palidx;
                    }
                }
            }
        }
    }

    return bm;
}

/*
 * render_ass_done - free renderer and library resources.
 *
 * Parameters:
 *  - lib: ASS_Library pointer previously returned by render_ass_init().
 *  - renderer: ASS_Renderer pointer previously returned by render_ass_renderer().
 *
 * Behavior:
 *  - If `renderer` is non-NULL, call ass_renderer_done(renderer).
 *  - If `lib` is non-NULL, call ass_library_done(lib).
 *
 * Note: callers should ensure they no longer reference libass objects after
 * calling this helper.
 */
void render_ass_done(ASS_Library *lib, ASS_Renderer *renderer) {
    if (renderer) ass_renderer_done(renderer);
    if (lib) ass_library_done(lib);
}

/*
 * render_ass_free_track - free event text and event array in ASS_Track
 *
 * The libass public API does not provide a dedicated destructor for
 * ASS_Track in some versions. This helper frees the per-event duplicated
 * Text strings and the events array that was allocated by libass or our
 * wrappers. It does not attempt to free the track pointer itself; callers
 * that own the ASS_Track structure must handle that as appropriate.
 */
void render_ass_free_track(ASS_Track *track) {
    if (!track) return;
    /* Free only per-event duplicated Text strings. The events array
     * itself is typically owned by libass; freeing it here can cause
     * undefined behavior. We free Text pointers we own (duplicated via
     * strdup in render_ass_add_event) and set them to NULL. */
    if (track->events) {
        for (int i = 0; i < track->n_events; i++) {
            if (track->events[i].Text) {
                free(track->events[i].Text);
                track->events[i].Text = NULL;
            }
        }
    }
    /* libass does not expose a public free for ASS_Track; assume caller will discard pointer */
}

/*
 * render_ass_free_renderer - convenience wrapper to free an ASS_Renderer
 *
 * Calls ass_renderer_done(renderer) when renderer is non-NULL. This keeps
 * naming consistent with other render_ass_free_* helpers.
 */
void render_ass_free_renderer(ASS_Renderer *renderer) {
    if (renderer) ass_renderer_done(renderer);
}

/*
 * render_ass_free_lib - convenience wrapper to free an ASS_Library
 *
 * Calls ass_library_done(lib) when lib is non-NULL.
 */
void render_ass_free_lib(ASS_Library *lib) {
    if (lib) ass_library_done(lib);
}

/*
* render_ass_set_style - create a minimal ASS header with a Default style
*
* Parameters:
*  - track: ASS_Track to receive the style (must be non-NULL)
*  - font: font family name passed to ASS style
*  - size: font size in ASS units
*  - fg, outline, shadow: color strings in "#RRGGBB" or "#AARRGGBB" form
*
* Behavior:
*  - Converts provided color strings to ASS color format and builds an
*    in-memory ASS header containing a single "Default" style.
*  - The header is fed to libass via ass_process_data which copies the
*    data into libass-managed structures.
*/
void render_ass_set_style(ASS_Track *track,
                          const char *font, int size,
                          const char *fg, const char *outline, const char *shadow)
{
    if (!track) {
        LOG(0, "render_ass_set_style: NULL track provided\n");
        return;
    }

    track->track_type = TRACK_TYPE_ASS;
    /* drop built-in "Default" */
    track->n_styles = 0;

    char fg_ass[32], outline_ass[32], shadow_ass[32];
    hex_to_ass_color(fg, fg_ass, sizeof(fg_ass));
    hex_to_ass_color(outline, outline_ass, sizeof(outline_ass));
    hex_to_ass_color(shadow, shadow_ass, sizeof(shadow_ass));

    /* Build a textual ASS header describing Script Info, Styles and Events.
     * This header is intentionally small and only provides the Default
     * style used by render_ass_add_event. play resolution is currently
     * hard-coded but could be parameterized if necessary. */
    char header[2048];
    int needed = snprintf(header, sizeof(header),
        "[Script Info]\n"
        "ScriptType: v4.00+\n"
        "PlayResX: %d\n"
        "PlayResY: %d\n"
        "\n"
        "[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
        "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
        "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
        "Alignment, MarginL, MarginR, MarginV, Encoding\n"
        "Style: Default,%s,%d,%s,%s,%s,%s,"
        "0,0,0,0,100,100,0,0,1,0,0,2,10,10,10,1\n"
        "\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n",
    720, 576,   /* default, or pass in video_w/h */
        font, size, fg_ass, fg_ass, outline_ass, shadow_ass);

    if (needed < 0) {
        LOG(1, "render_ass_set_style: snprintf error building header\n");
        return;
    }

    if ((size_t)needed >= sizeof(header)) {
        /* Static buffer truncated; allocate dynamically to the required size. */
        size_t bufsz = (size_t)needed + 1;
        char *dyn = malloc(bufsz);
        if (!dyn) {
            LOG(1, "render_ass_set_style: malloc failed for header size %zu\n", bufsz);
            return;
        }
        int r2 = snprintf(dyn, bufsz,
            "[Script Info]\n"
            "ScriptType: v4.00+\n"
            "PlayResX: %d\n"
            "PlayResY: %d\n"
            "\n"
            "[V4+ Styles]\n"
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
            "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
            "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
            "Alignment, MarginL, MarginR, MarginV, Encoding\n"
            "Style: Default,%s,%d,%s,%s,%s,%s,"
            "0,0,0,0,100,100,0,0,1,0,0,2,10,10,10,1\n"
            "\n"
            "[Events]\n"
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n",
        720, 576,
            font, size, fg_ass, fg_ass, outline_ass, shadow_ass);
        if (r2 < 0 || (size_t)r2 >= bufsz) {
            LOG(1, "render_ass_set_style: snprintf into dynamic buffer failed\n");
            free(dyn);
            return;
        }
        ass_process_data(track, dyn, (size_t)r2);
        free(dyn);
    } else {
        ass_process_data(track, header, (size_t)needed);
    }
}

/*
* render_ass_debug_styles - print a human-readable dump of styles in a track
*
* Parameters:
*  - track: ASS_Track pointer to inspect. If NULL the function prints an
*           error message and returns.
*
* Behavior:
*  - Iterates track->styles and prints key style fields useful for
*    debugging rendering and style mismatches.
*/
void render_ass_debug_styles(ASS_Track *track) {

    if (!track) {
        LOG(0, "[render_ass] No track to debug\n");
        return;
    }

    LOG(2, "\n=== [render_ass] Style Debug Dump ===\n");
    LOG(2, "Track has %d styles\n", track->n_styles);

    for (int i = 0; i < track->n_styles; i++) {
        ASS_Style *st = &track->styles[i];
        LOG(2,
            "  [%d] Name='%s' Font='%s' Size=%f Align=%d\n"
            "       Primary=%08X Secondary=%08X Outline=%08X Back=%08X\n",
            i,
            st->Name ? st->Name : "(null)",
            st->FontName ? st->FontName : "(null)",
            st->FontSize,
            st->Alignment,
            st->PrimaryColour,
            st->SecondaryColour,
            st->OutlineColour,
            st->BackColour);
    }

    LOG(2, "======================================\n");
}

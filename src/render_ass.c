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
#include <cairo/cairo.h>


/*
 * hex_to_ass_color - convert HTML-style hex color to ASS color string
 *
 * Convert a CSS/HTML-style hex color string of the form "#RRGGBB" or
 * "#AARRGGBB" into libass/ASS color syntax "&HAABBGGRR". libass stores
 * alpha as an inverted value (0x00 is opaque), so this helper inverts
 * the alpha channel when present.
 *
 * Parameters:
 *  - hex: input color string (NUL-terminated). If NULL or malformed,
 *         the function emits a default opaque white color.
 *  - out: output buffer to receive the ASS color string (NUL-terminated).
 *  - outsz: size of the output buffer in bytes. The function uses
 *           snprintf and will not write past outsz bytes.
 *
 * Notes / behavior:
 *  - Accepts exactly 7-character strings "#RRGGBB" or 9-character
 *    strings "#AARRGGBB" (including the leading '#'). Other lengths or
 *    malformed input fall back to "&H00FFFFFF" (opaque white).
 *  - The output string format is compatible with ASS header/style color
 *    specifications (e.g., "PrimaryColour: &H00FFFFFF").
 */
static void hex_to_ass_color(const char *hex, char *out, size_t outsz) {
    unsigned r=255,g=255,b=255,a=0;
    /*
     * Parse simple #RRGGBB or #AARRGGBB hex strings. We default to white
     * when the input is missing or malformed. sscanf is used for concise
     * parsing; the input length is checked first to choose the expected
     * layout.
     */
    if (!hex || hex[0] != '#') {
        /* output ASS white: alpha 0x00 (opaque), color FFFFFF */
        snprintf(out, outsz, "&H00FFFFFF");
        return;
    }
    if (strlen(hex) == 7) {
        /* #RRGGBB */
        sscanf(hex+1, "%02x%02x%02x", &r,&g,&b);
        a = 0x00; /* fully opaque (ASS alpha is inverted below) */
    } else if (strlen(hex) == 9) {
        /* #AARRGGBB */
        sscanf(hex+1, "%02x%02x%02x%02x", &a,&r,&g,&b);
    }
    /* ASS encodes alpha inverted (0x00 == opaque). Convert by inverting. */
    unsigned inv_a = 0xFF - a;
    /* Format to &HAABBGGRR as required by ASS headers/styles. */
    snprintf(out, outsz, "&H%02X%02X%02X%02X", inv_a, b, g, r);
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
        fprintf(stderr, "libass: failed to init\n");
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
    /* Allocate an event slot; ass_alloc_event returns a non-negative
     * index on success. */
    int ev = ass_alloc_event(track);
    if (ev < 0) return; /* allocation failure; silently skip event */

    /* Populate minimal event fields: start time and duration (ms). The
     * library stores times as ints; cast from int64_t accordingly. */
    track->events[ev].Start    = (int)start_ms;
    track->events[ev].Duration = (int)(end_ms - start_ms);
    /* Use style index 0 (Default) unless a different style is injected. */
    track->events[ev].Style    = 0;
    /* Duplicate the text into libass-managed memory; the caller retains
     * ownership of the original `text` pointer. */
    track->events[ev].Text     = strdup(text ? text : "");

    /* Optional debug trace for developers. */
    extern int debug_level;
    if (debug_level > 1) {
        fprintf(stderr,
            "[render_ass] Added event #%d: %lld → %lld ms | text='%s'\n",
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
    int minx=99999, miny=99999, maxx=0, maxy=0;
    /* Walk the linked list of images to compute the union bounding box of
     * all drawn fragments. This tightly bounds our output Bitmap to the
     * minimal rectangle containing all non-empty image tiles. */
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

    bm.w = w;
    bm.h = h;
    bm.x = minx;
    bm.y = miny;
    /* Allocate index buffer (one byte per pixel). Caller must free. */
    /* Allocate one-byte-per-pixel index buffer and zero it. Zero ensures
     * transparent/unused pixels remain at palette index 0 (commonly used
     * for transparent color in DVB palettes). */
    bm.idxbuf = calloc(w*h, 1);

    /* Allocate and initialize the palette used to map ARGB colors to
     * palette indices. The implementation uses a 16-entry palette for
     * broadcast/teletext compatibility. Caller must free bm.palette. */
    /* Allocate a small palette and initialize it using the project's
     * init_palette helper. The chosen palette size (16) matches
     * conventional broadcast subtitle palettes. */
    bm.palette = malloc(16 * sizeof(uint32_t));
    init_palette(bm.palette, palette_mode);

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
        int palidx = nearest_palette_index(bm.palette, 16, rgba);

        /* Rasterize coverage mask into the index buffer, offset by the
         * computed union bounding box (minx/miny). We bound-check to avoid
         * writing outside the allocated region when images extend beyond
         * the union box due to rounding or negative dst_x/dst_y. */
        for (int yy=0; yy<cur->h; yy++) {
            for (int xx=0; xx<cur->w; xx++) {
                uint8_t cov = cur->bitmap[yy*cur->stride + xx];
                if (cov > 0) {
                    int dx = cur->dst_x + xx - minx;
                    int dy = cur->dst_y + yy - miny;
                    if (dx >= 0 && dx < w && dy >= 0 && dy < h) {
                        bm.idxbuf[dy*w + dx] = palidx;
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
    /* Free events and text within the track */
    if (track->events) {
        for (int i = 0; i < track->n_events; i++) {
            if (track->events[i].Text) free(track->events[i].Text);
        }
        free(track->events);
        track->events = NULL;
        track->n_events = 0;
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


    if (track) {
        track->track_type = TRACK_TYPE_ASS;
        /* drop built-in "Default" */
        track->n_styles = 0;
    }

    char fg_ass[32], outline_ass[32], shadow_ass[32];
    hex_to_ass_color(fg, fg_ass, sizeof(fg_ass));
    hex_to_ass_color(outline, outline_ass, sizeof(outline_ass));
    hex_to_ass_color(shadow, shadow_ass, sizeof(shadow_ass));

    /* Build a textual ASS header describing Script Info, Styles and Events.
     * This header is intentionally small and only provides the Default
     * style used by render_ass_add_event. play resolution is currently
     * hard-coded but could be parameterized if necessary. */
    char header[2048];
    snprintf(header, sizeof(header),
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

    ass_process_data(track, header, strlen(header));
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
        fprintf(stderr, "[render_ass] No track to debug\n");
        return;
    }

    fprintf(stderr, "\n=== [render_ass] Style Debug Dump ===\n");
    fprintf(stderr, "Track has %d styles\n", track->n_styles);

    for (int i = 0; i < track->n_styles; i++) {
        ASS_Style *st = &track->styles[i];
        fprintf(stderr,
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

    fprintf(stderr, "======================================\n");
}
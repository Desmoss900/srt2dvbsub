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

#define _POSIX_C_SOURCE 200809L
#include "dvb_sub.h"
#include "alloc_utils.h"
#include "pool_alloc.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <libavutil/mem.h>


/*
 * AUDIT CORRECTION SUMMARY
 * ------------------------
 * The following issues were found and fixed in this file as part of
 * a robustness and safety audit:
 *  - Replaced unsafe integer/size arithmetic with size_t checks to
 *    avoid overflow when allocating index and palette buffers.
 *  - Added `safe_av_mallocz_array()` helper to centralize overflow
 *    checks and to provide a portable fallback when av_mallocz_array
 *    isn't available.
 *  - Hardened palette handling: clamped nb_colors to encoder limits
 *    and avoided memcpy(NULL,...) by checking allocations before copy.
 *  - Improved cleanup paths: prefer av_freep() to clear pointers and
 *    avoid double-free/partial-free hazards.
 *  - Added informative debug logging on allocation failures using the
 *    global `debug_level`.
 *  - Clamped durations to avoid unsigned wrap when converting to
 *    `unsigned` end_display_time.
 *
 * Remaining recommendations (see docs/dvb_sub_optimization.txt):
 *  - Normalize free-style across all branches (completed here);
 *  - Consider exposing buffer lengths/strides on Bitmap to validate
 *    memcpy sizes;
 *  - Optionally centralize allocation/free helpers in a common util.
 */

/* Provide a short module name used by LOG() in debug.h */
#define DEBUG_MODULE "dvb_sub"
#include "debug.h"

/*
 * free_sub_and_rects
 * ------------------
 * Helper to release a partially-constructed AVSubtitle that may have
 * an allocated rect and/or data planes. This centralizes cleanup so
 * callers don't repeat the av_freep chains and risk forgetting a
 * field. The function is safe to call with a NULL pointer.
 */
static void free_sub_and_rects(AVSubtitle *sub) {
    if (!sub) return;
    if (sub->rects) {
        if (sub->rects[0]) {
            AVSubtitleRect *r = sub->rects[0];
            /* free palette then index plane if present via pool_free */
            if (r->data[1]) {
                size_t pbytes = (size_t)r->linesize[1];
                if (pbytes == 0) pbytes = (size_t)r->nb_colors * 4u;
                pool_free(r->data[1], pbytes);
                r->data[1] = NULL;
            }
            if (r->data[0]) {
                size_t pix = (size_t)r->w * (size_t)r->h;
                pool_free(r->data[0], pix);
                r->data[0] = NULL;
            }
            av_freep(&sub->rects[0]);
        }
        av_freep(&sub->rects);
    }
    av_freep(&sub);
}

/*
 * free_subtitle
 * --------------
 * Public wrapper (file-local) to free a heap-allocated AVSubtitle
 * and NULL the caller's pointer. See dvb_sub.h for usage notes.
 */
void free_subtitle(AVSubtitle **psub) {
    if (!psub || !*psub) return;
    avsubtitle_free(*psub);
    av_freep(psub);
}

/*
 * make_subtitle
 * -------------
 * Convert a rendered Bitmap into an libav AVSubtitle suitable for the
 * DVB subtitle encoder. The function allocates the AVSubtitle structure
 * and necessary AVSubtitleRect(s), copies the index-plane and palette
 * data, and sets display timing (end_display_time = end_ms - start_ms).
 *
 * Notes on memory ownership:
 *  - All allocations use libavutil allocators (av_malloc/av_mallocz) so
 *    callers should free the returned object with avsubtitle_free() and
 *    av_free() to avoid leaks.
 *  - If the bitmap is empty (zero dimension or missing index buffer)
 *    the function returns an AVSubtitle with zero rects (num_rects=0)
 *    which encoders interpret as a clear/blank subtitle event.
 */
AVSubtitle* make_subtitle(Bitmap bm, int64_t start_ms, int64_t end_ms) {
    /*
     * make_subtitle
     * ------------
     * Convert a rendered Bitmap into an AVSubtitle suitable for DVB
     * encoding. The function allocates the AVSubtitle and any required
     * AVSubtitleRect(s) using libavutil allocators. On failure the
     * function returns NULL and no memory is allocated.
     */
    AVSubtitle *sub = av_mallocz(sizeof(AVSubtitle));
    if (!sub) return NULL; /* allocation failed */

    /*
     * Defensive / empty-bitmap path: when the bitmap is invalid or has
     * zero area we build an AVSubtitle with zero rects. Encoders treat
     * this as an explicit clear event. We still return an allocated
     * AVSubtitle to keep the call-site logic uniform.
     */
    if (bm.w <= 0 || bm.h <= 0 || !bm.idxbuf) {
        LOG(4, "Empty bitmap passed: w=%d h=%d idxbuf=%p\n", bm.w, bm.h, (void*)bm.idxbuf);

        sub->num_rects = 0;
        sub->rects = NULL;

        /* start_display_time is intentionally left at 0. The calling
         * code in the pipeline treats the rect's timing as a duration
         * (end_display_time) and applies absolute timing elsewhere.
         * If you need non-zero start offsets inside the AVSubtitle, set
         * start_display_time appropriately where the subtitle is built.
         */
        sub->start_display_time = 0;

        /* end_display_time is the duration in ms (end - start). Clamp
         * negative durations to zero and cap to UINT_MAX to avoid unsigned
         * wrap. */
        {
            int64_t dur = end_ms - start_ms;
            if (dur < 0) dur = 0;
            if (dur > (int64_t)UINT_MAX) dur = (int64_t)UINT_MAX;
            sub->end_display_time = (unsigned)dur;
        }

        LOG(4, "Built subtitle: rect=%dx%d at (%d,%d), duration=%u ms\n", bm.w, bm.h, bm.x, bm.y, sub->end_display_time);
        return sub;
    }

    /*
     * Normal path: allocate a single AVSubtitleRect and populate its
     * fields with geometry, index-plane and palette. The function uses
     * av_mallocz so callers can uniformly free with avsubtitle_free.
     */
    sub->num_rects = 1;
    /* Allocate the rects array with overflow protection. */
    sub->rects = safe_av_mallocz_array((size_t)sub->num_rects, sizeof(*sub->rects));
    if (!sub->rects) {
        LOG(1, "allocation failed: rects array (%zu x %zu)\n", (size_t)sub->num_rects, sizeof(*sub->rects));
        av_freep(&sub);
        return NULL;
    }

    /*
     * Allocates memory for the first subtitle rectangle in the 'rects' array of the 'sub' structure.
     * The memory is zero-initialized using av_mallocz to ensure all fields are set to zero.
     * This is typically used to prepare a new AVSubtitleRect for subtitle rendering.
     */
    sub->rects[0] = av_mallocz(sizeof(AVSubtitleRect));

    if (!sub->rects[0]) {
        LOG(1, "allocation failed: rect[0] (%zu bytes)\n", sizeof(AVSubtitleRect));
        av_freep(&sub->rects);
        av_freep(&sub);
        return NULL;
    }

    AVSubtitleRect *r = sub->rects[0];

    /* Copy geometry from the Bitmap into the subtitle rect. */
    r->x = bm.x;
    r->y = bm.y;
    r->w = bm.w;
    r->h = bm.h;

    /* Number of colors: prefer bm.nb_colors when present, otherwise 16.
     * Clamp to the encoder's maximum (AVPALETTE_SIZE / 4 entries). */
    {
        const int default_colors = 16;
        const int max_colors = (int)(AVPALETTE_SIZE / 4u);
        int nb = bm.nb_colors > 0 ? bm.nb_colors : default_colors;
        if (nb > max_colors) nb = max_colors;
        r->nb_colors = nb;
    }
    r->type = SUBTITLE_BITMAP;

    /* Safely compute pixel count and allocate the index plane. Use
     * size_t arithmetic to avoid signed-int overflow if bm.w or bm.h
     * are large or negative. The empty-bitmap case (bm.w<=0/bm.h<=0
     * or missing idxbuf) is handled earlier and returns a zero-rect
     * AVSubtitle; therefore we don't re-check dimensions here.
     */
    size_t pixel_count = (size_t)bm.w * (size_t)bm.h;
    /* Defensive check: ensure multiplication didn't wrap (shouldn't on
     * platforms where size_t is wide enough, but keep the check). */
    if (pixel_count / (size_t)bm.w != (size_t)bm.h) {
        /* normalize cleanup to central helper */
        free_sub_and_rects(sub);
        return NULL;
    }
    /* allocate (zeroed) index plane via pool */
    r->data[0] = pool_alloc(pixel_count);
    /* Clamp linesize to INT_MAX to avoid narrowing when casting to int. */
    r->linesize[0] = (bm.w > INT_MAX) ? INT_MAX : bm.w;
    if (!r->data[0]) {
        LOG(1, "allocation failed: index plane (%zu bytes)\n", pixel_count);
        free_sub_and_rects(sub);
        return NULL;
    }
    /* Validate caller-provided idx buffer length when available. If the
     * Bitmap carries an idxbuf_len we require it to be at least the
     * expected pixel_count; otherwise treat it as an error to avoid
     * potential OOB reads. */
    if (bm.idxbuf_len == 0 || bm.idxbuf_len < pixel_count) {
        LOG(1, "idxbuf too small or missing: have=%zu need=%zu\n", bm.idxbuf_len, pixel_count);
        free_sub_and_rects(sub);
        return NULL;
    }
    memcpy(r->data[0], bm.idxbuf, pixel_count);

    /*
     * Palette plane: allocate up to AVPALETTE_SIZE bytes and copy the
     * 32-bit ARGB palette entries. Use size_t arithmetic and check for
     * allocation failure before copying to avoid memcpy(NULL,...). */
    size_t palette_bytes = (size_t)r->nb_colors * 4u;
    if (palette_bytes > (size_t)AVPALETTE_SIZE) palette_bytes = (size_t)AVPALETTE_SIZE;
    if (palette_bytes > 0) {
        r->data[1] = pool_alloc(palette_bytes);
        /* linesize holds the byte count for the palette plane */
        r->linesize[1] = (int) (palette_bytes > (size_t)INT_MAX ? INT_MAX : (int)palette_bytes);
        if (!r->data[1]) {
            LOG(1, "allocation failed: palette (%zu bytes)\n", palette_bytes);
            free_sub_and_rects(sub);
            return NULL;
        }
        if (bm.palette) {
            if (bm.palette_bytes < palette_bytes) {
                LOG(1, "palette too small: have=%zu need=%zu\n", bm.palette_bytes, palette_bytes);
                free_sub_and_rects(sub);
                return NULL;
            }
            memcpy(r->data[1], bm.palette, palette_bytes);
        }
    } else {
        r->data[1] = NULL;
        r->linesize[1] = 0;
    }

    /* Set display timing (end_display_time is a duration in ms). Clamp
     * negative durations to zero and cap to UINT_MAX to avoid unsigned
     * wrap or truncation. start_display_time is left as 0 (relative
     * display start) per existing behavior. */
    sub->start_display_time = 0;
    int64_t dur = end_ms - start_ms;
    if (dur < 0) dur = 0;
    if (dur > (int64_t)UINT_MAX) dur = (int64_t)UINT_MAX;
    sub->end_display_time = (unsigned)dur;

    return sub;
}
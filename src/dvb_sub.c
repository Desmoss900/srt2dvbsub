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

#define _POSIX_C_SOURCE 200809L
#include "dvb_sub.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <libavutil/mem.h>

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
        extern int debug_level;
        if (debug_level > 3) {
            fprintf(stderr,
                "[dvb_sub] Empty bitmap passed: w=%d h=%d idxbuf=%p\n",
                bm.w, bm.h, (void*)bm.idxbuf);
        }

        sub->num_rects = 0;
        sub->rects = NULL;
        sub->start_display_time = 0;
        /* end_display_time is the duration in ms (end - start) */
        sub->end_display_time   = (unsigned)(end_ms - start_ms);

        if (debug_level > 3) {
            fprintf(stderr,
                "[dvb_sub] Built subtitle: rect=%dx%d at (%d,%d), duration=%u ms\n",
                bm.w, bm.h, bm.x, bm.y, sub->end_display_time);
        }
        return sub;
    }

    /*
     * Normal path: allocate a single AVSubtitleRect and populate its
     * fields with geometry, index-plane and palette. The function uses
     * av_mallocz so callers can uniformly free with avsubtitle_free.
     */
    sub->num_rects = 1;
    sub->rects = av_mallocz(sizeof(AVSubtitleRect*));
    if (!sub->rects) { av_free(sub); return NULL; }

    /*
     * Allocates memory for the first subtitle rectangle in the 'rects' array of the 'sub' structure.
     * The memory is zero-initialized using av_mallocz to ensure all fields are set to zero.
     * This is typically used to prepare a new AVSubtitleRect for subtitle rendering.
     */
    sub->rects[0] = av_mallocz(sizeof(AVSubtitleRect));

    if (!sub->rects[0]) {
        av_free(sub->rects);
        av_free(sub);
        return NULL;
    }

    AVSubtitleRect *r = sub->rects[0];

    /* Copy geometry from the Bitmap into the subtitle rect. */
    r->x = bm.x;
    r->y = bm.y;
    r->w = bm.w;
    r->h = bm.h;

    /* Number of colors: prefer bm.nb_colors when present, otherwise 16. */
    r->nb_colors = bm.nb_colors > 0 ? bm.nb_colors : 16;
    r->type = SUBTITLE_BITMAP;

    /*
     * Index plane: allocate one byte per pixel and copy the index buffer
     * (palette indices 0..nb_colors-1). The encoder expects a packed
     * index plane where linesize equals width for simple bitmaps.
     */
    r->data[0] = av_mallocz(bm.w * bm.h);
    r->linesize[0] = bm.w;
    if (!r->data[0]) {
        av_free(r);
        av_free(sub->rects);
        av_free(sub);
        return NULL;
    }
    memcpy(r->data[0], bm.idxbuf, bm.w * bm.h);

    /*
     * Palette plane: allocate up to AVPALETTE_SIZE bytes and copy the
     * 32-bit ARGB palette entries. Clamp to the encoder's maximum.
     */
    int palette_bytes = FFMIN(r->nb_colors * 4, AVPALETTE_SIZE);
    r->data[1] = av_mallocz(palette_bytes);
    r->linesize[1] = palette_bytes;
    if (bm.palette) {
        memcpy(r->data[1], bm.palette, palette_bytes);
    }

    /* Set display timing (end_display_time is a duration in ms). */
    sub->start_display_time = 0;
    sub->end_display_time   = (unsigned)(end_ms - start_ms);

    return sub;
}
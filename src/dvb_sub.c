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
*   Email: [license@chili-iptv.info]  *   Website: [www.chili-iptv.info]
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

AVSubtitle* make_subtitle(Bitmap bm, int64_t start_ms, int64_t end_ms) {
    AVSubtitle *sub = av_mallocz(sizeof(AVSubtitle));
    if (!sub) return NULL;

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
    sub->end_display_time   = (unsigned)(end_ms - start_ms);

    extern int debug_level;
    if (debug_level > 3) {
        fprintf(stderr,
            "[dvb_sub] Built subtitle: rect=%dx%d at (%d,%d), duration=%u ms\n",
            bm.w, bm.h, bm.x, bm.y, sub->end_display_time);
    }
        return sub;
    }

    sub->num_rects = 1;
    sub->rects = av_mallocz(sizeof(AVSubtitleRect*));
    if (!sub->rects) { av_free(sub); return NULL; }

    sub->rects[0] = av_mallocz(sizeof(AVSubtitleRect));
    if (!sub->rects[0]) {
        av_free(sub->rects);
        av_free(sub);
        return NULL;
    }

    AVSubtitleRect *r = sub->rects[0];
    r->x = bm.x;
    r->y = bm.y;
    r->w = bm.w;
    r->h = bm.h;
    r->nb_colors = bm.nb_colors > 0 ? bm.nb_colors : 16;
    r->type = SUBTITLE_BITMAP;

    // Allocate bitmap plane
    r->data[0] = av_mallocz(bm.w * bm.h);
    r->linesize[0] = bm.w;
    if (!r->data[0]) {
        av_free(r);
        av_free(sub->rects);
        av_free(sub);
        return NULL;
    }
    memcpy(r->data[0], bm.idxbuf, bm.w * bm.h);

    // Allocate palette plane
    int palette_bytes = FFMIN(r->nb_colors * 4, AVPALETTE_SIZE);
    r->data[1] = av_mallocz(palette_bytes);
    r->linesize[1] = palette_bytes;
    if (bm.palette) {
        memcpy(r->data[1], bm.palette, palette_bytes);
    }

    sub->start_display_time = 0;
    sub->end_display_time   = (unsigned)(end_ms - start_ms);

    return sub;
}
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
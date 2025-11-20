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
*   Email: [license@chili-iptv.de]  
*   Website: [www.chili-iptv.de]
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
#include "render_pango.h"
#include "palette.h"
#include "debug.h"
#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <limits.h>
#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <stdatomic.h>
/* Use a thread-local PangoFontMap to avoid concurrent internal Pango
 * mutations when rendering from multiple worker threads. We create a
 * pthread key with a destructor that unrefs the fontmap on thread exit.
 */
#include <pthread.h>

/*
 * AUDIT SUMMARY (render_pango.c)
 * --------------------------------
 * This file was audited and hardened. The following corrections and
 * defensive changes were applied to improve safety and robustness:
 *
 *  - srt_to_pango_markup(): safer bounded buffer usage, checked snprintf
 *    return values, and robust entity escaping.
 *  - parse_hex_color(): validated sscanf return values and corrected
 *    parsing of #AARRGGBB (alpha-first) formats; robust fallback to
 *    opaque white on malformed input.
 *  - Allocation safety helpers: added mul_size_ok() and safe_malloc_count()
 *    to detect size_t overflow and apply a conservative pixel cap.
 *  - Pixel caps: RENDER_PANGO_SAFE_MAX_PIXELS (total) and
 *    RENDER_PANGO_SAFE_MAX_DIM (per-dimension) to avoid excessive
 *    allocations for pathological sizes.
 *  - Centralized bitmap allocation/cleanup: allocate_bitmap_buffers()
 *    and free_bitmap_buffers() with nb_colors bookkeeping.
 *  - Defensive Cairo/Pango checks: verify surface/context creation and
 *    cairo_image_surface_get_data() before accessing pixel data.
 *  - Thread-local fontmap: created with pthread_once; get_thread_pango_fontmap()
 *    checks and logs failures; render_pango_cleanup() unrefs thread fontmaps.
 *  - Atomic runtime knobs: dbg_ssaa_override and dbg_no_unsharp are atomic_int
 *    to avoid data races during concurrent reads/writes.
 *  - Early short-circuit for absurd display sizes: render_text_pango()
 *    rejects implausible disp_w/disp_h values to avoid creating huge layouts.
 *  - Centralized error cleanup paths: consistent final_cleanup/cleanup calls
 *    to prevent leaks and partial frees on early exits.
 *
 * Tests:
 *  - Added `tool/test_render_pango.c` with cases for markup, long inputs,
 *    color parsing, basic render, and extreme-size guards.
 *  - Added `tool/Makefile` targets: `test` and `asan-test` (Address/Undefined
 *    sanitizer builds). The test harness calls render_pango_cleanup() and
 *    attempts to call FcFini() at runtime to reduce sanitizer-reported leaks.
 *
 * Result: functional unit tests pass and sanitizer/Valgrind runs show no
 * definite leaks (remaining reachable allocations originate in system
 * libraries and are considered benign for this project).
 */

/*
 * Static variables for managing thread-specific Pango fontmap key.
 * - pango_fontmap_key: Key used to store/retrieve thread-local fontmap data.
 * - pango_fontmap_key_once: Ensures the key is initialized only once per process.
 */
static pthread_key_t pango_fontmap_key;
static pthread_once_t pango_fontmap_key_once = PTHREAD_ONCE_INIT;

/*
 * pango_fontmap_destructor
 * -----------------------
 * Called by pthread when a thread with a stored PangoFontMap exits. We
 * simply unref the GObject if present. Keep this minimal and robust as
 * it runs during thread teardown.
 */
static void pango_fontmap_destructor(void *v) {
    if (!v) return;
    g_object_unref((GObject*)v);
}

/* Create the pthread key for storing a PangoFontMap per-thread. This is
 * executed once via pthread_once to avoid races during key creation. */
static void make_pango_fontmap_key(void) {
    int rc = pthread_key_create(&pango_fontmap_key, pango_fontmap_destructor);
    if (rc != 0) {
        /* Key creation failed (unlikely) — record diagnostic. Subsequent
         * calls that rely on the key should handle a NULL/invalid key. */
        fprintf(stderr, "render_pango: pthread_key_create failed: %d\n", rc);
    }
}


/*
 * get_thread_pango_fontmap
 * ------------------------
 * Obtain (or lazily create) a PangoFontMap tied to the calling thread.
 * Using a thread-local fontmap avoids races and unnecessary global
 * mutations inside Pango/fontconfig when rendering concurrently.
 * The returned object is owned by thread-specific storage and should
 * NOT be unreffed by callers.
 */
static PangoFontMap *get_thread_pango_fontmap(void) {
    pthread_once(&pango_fontmap_key_once, make_pango_fontmap_key);
    PangoFontMap *fm = (PangoFontMap *)pthread_getspecific(pango_fontmap_key);
    if (!fm) {
        fm = pango_cairo_font_map_new();
        if (!fm) {
            fprintf(stderr, "render_pango: pango_cairo_font_map_new() returned NULL\n");
            return NULL;
        }
        int rc = pthread_setspecific(pango_fontmap_key, fm);
        if (rc != 0) {
            /* Failed to store fm in thread-specific storage — clean up and
             * return NULL to indicate we couldn't establish a thread fontmap. */
            fprintf(stderr, "render_pango: pthread_setspecific failed: %d\n", rc);
            g_object_unref((GObject*)fm);
            return NULL;
        }
    }
    return fm;
}

/*
 * render_pango_cleanup
 * ---------------------
 * Deterministically free any PangoFontMap stored in this thread's
 * thread-specific storage. Safe to call multiple times; useful to call
 * before FcFini() to control teardown ordering.
 */
void render_pango_cleanup(void) {
    pthread_once(&pango_fontmap_key_once, make_pango_fontmap_key);
    PangoFontMap *fm = (PangoFontMap *)pthread_getspecific(pango_fontmap_key);
    if (fm) {
        pthread_setspecific(pango_fontmap_key, NULL);
        g_object_unref((GObject*)fm);
    }
}

/* Safety helpers for guarded allocations and multiplication checks. */
/* Conservative cap on number of pixels we will attempt to allocate buffers for. */
#ifndef RENDER_PANGO_SAFE_MAX_PIXELS
#define RENDER_PANGO_SAFE_MAX_PIXELS ((size_t)100000000) /* 100M pixels */
#endif

/* Conservative cap on any single bitmap dimension to avoid excessive
 * allocations or integer-promoted multiplication surprises on 32-bit
 * platforms. This protects against pathological layout sizes. */
#ifndef RENDER_PANGO_SAFE_MAX_DIM
#define RENDER_PANGO_SAFE_MAX_DIM ((size_t)20000) /* 20k pixels per side */
#endif

static int mul_size_ok(size_t a, size_t b, size_t *out) {
    if (a == 0 || b == 0) { *out = 0; return 1; }
    if (a > SIZE_MAX / b) return 0;
    *out = a * b;
    return 1;
}

static void *safe_malloc_count(size_t nitems, size_t itemsize) {
    size_t bytes;
    if (itemsize != 0) {
        if (!mul_size_ok(nitems, itemsize, &bytes)) return NULL;
    } else {
        bytes = 0;
    }
    /* If this looks like a pixel buffer, apply a conservative cap. */
    if (nitems > RENDER_PANGO_SAFE_MAX_PIXELS) return NULL;
    return malloc(bytes);
}


/* Runtime knobs: allow tests or CLI to tune rendering behavior. Make these
 * atomic so they can be safely modified/read from multiple threads. */
static atomic_int dbg_ssaa_override = 0; /* >0 forces a specific supersample factor */
static atomic_int dbg_no_unsharp = 0;    /* disable unsharp sharpening when non-zero */
void render_pango_set_ssaa_override(int ssaa) { atomic_store(&dbg_ssaa_override, ssaa); }
void render_pango_set_no_unsharp(int no) { atomic_store(&dbg_no_unsharp, no); }

/* Palette presets */
/*
 * init_palette
 * ------------
 * Initialize a 16-entry ARGB palette according to `mode`. `pal` must
 * point to at least 16 uint32_t entries. If `pal` is NULL the call is a no-op.
 */
void init_palette(uint32_t *pal,const char *mode) {
    if (!pal) return;

    /* Contract: palette index 0 MUST be transparent (alpha == 0). Ensure
     * this at function start so all control paths satisfy the contract. */
    pal[0] = 0x00000000;

    /*
     * Sets up a 16-color palette (`pal`) based on the specified `mode` string.
     *
     * Supported modes:
     * - "greyscale": Generates a smooth greyscale palette from transparent to white.
     * - "broadcast": Uses a broadcast palette with full-bright and half-bright colors,
     *   providing intermediate luminance values for anti-aliased edges without semi-transparency.
     * - "ebu-broadcast": Uses the full EBU 16-color CLUT, including half-bright variants
     *   for improved anti-aliasing without compositing halos.
     * - Default: Uses a simple broadcast palette with standard colors and transparent entries.
     *
     * Each palette entry is a 32-bit ARGB value. The first entry is always transparent.
     * Half-bright variants use full alpha and half the RGB channel values to avoid
     * semi-transparent compositing artifacts.
     *
     * Parameters:
     *   mode - String specifying the palette mode.
     *   pal  - Array of 16 uint32_t values to be filled with palette colors.
     */
    if (mode && strcasecmp(mode,"greyscale")==0) {
        /* Smooth greyscale from transparent to white */
        pal[0] = 0x00000000; /* transparent */
        for (int i=1; i<16; i++) {
            int v = (i-1)*17;
            pal[i] = (0xFF<<24) | (v<<16) | (v<<8) | v;
        }
    } else if (mode && strcasecmp(mode,"broadcast")==0) {
        /* Broadcast palette with additional half-bright entries (helps anti-aliased edges) */
        pal[0]=0x00000000; 
        pal[1]=0xFFFFFFFF; 
        pal[2]=0xFFFFFF00;
        pal[3]=0xFF00FFFF; 
        pal[4]=0xFF00FF00; 
        pal[5]=0xFFFF00FF;
        pal[6]=0xFFFF0000; 
        pal[7]=0xFF0000FF;
        pal[8]=0xFF000000;
        /* half-bright variants (opaque but half RGB) to provide intermediate
        * luminance values for anti-aliased edges without introducing
        * semi-transparent compositing halos. Use full alpha (0xFF) and
        * halve RGB channels. */
        pal[9]  = 0xFF7F7F7F; /* half white */
        pal[10] = 0xFF7F7F00; /* half yellow */
        pal[11] = 0xFF007F7F; /* half cyan */
        pal[12] = 0xFF007F00; /* half green */
        pal[13] = 0xFF7F007F; /* half magenta */
        pal[14] = 0xFF7F0000; /* half red */
        pal[15] = 0xFF00007F; /* half blue */

    } else if (mode && strcasecmp(mode,"ebu-broadcast")==0) {
        /* Full EBU 16-color CLUT (full + half brightness) */
        pal[0]  = 0x00000000; /* transparent */
        pal[1]  = 0xFFFFFFFF; /* white */
        pal[2]  = 0xFFFFFF00; /* yellow */
        pal[3]  = 0xFF00FFFF; /* cyan */
        pal[4]  = 0xFF00FF00; /* green */
        pal[5]  = 0xFFFF00FF; /* magenta */
        pal[6]  = 0xFFFF0000; /* red */
        pal[7]  = 0xFF0000FF; /* blue */
        pal[8]  = 0xFF000000; /* black */
        /* half-bright variants (opaque half-RGB) rather than semi-transparent
        * entries: helps anti-aliased edges map to intermediate visible
        * colors without compositing dark halos. */
        pal[9]  = 0xFF7F7F7F; /* half white */
        pal[10] = 0xFF7F7F00; /* half yellow */
        pal[11] = 0xFF007F7F; /* half cyan */
        pal[12] = 0xFF007F00; /* half green */
        pal[13] = 0xFF7F007F; /* half magenta */
        pal[14] = 0xFF7F0000; /* half red */
        pal[15] = 0xFF00007F; /* half blue */
    } else {
        /* Default: same as simple broadcast */
        pal[0]=0x00000000; pal[1]=0xFFFFFFFF; pal[2]=0xFFFFFF00;
        pal[3]=0xFF00FFFF; pal[4]=0xFF00FF00; pal[5]=0xFFFF00FF;
        pal[6]=0xFFFF0000; pal[7]=0xFF000000;
        for(int i=8;i<16;i++) pal[i]=0x00000000;
    }
}

/*
 * nearest_palette_index
 * ---------------------
 * Fast Euclidean nearest-color search in RGB space. Returns an index
 * within [0, npal). Used as a simple fallback when alpha is not
 * considered.
 */
int nearest_palette_index(uint32_t *palette, int npal, uint32_t argb) {
    int best=1; int bestdiff=INT_MAX;
    int r=(argb>>16)&0xFF, g=(argb>>8)&0xFF, b=argb&0xFF;
    for(int i=0;i<npal;i++){
        uint32_t p=palette[i];
        int pr=(p>>16)&0xFF, pg=(p>>8)&0xFF, pb=p&0xFF;
        int dr=r-pr, dg=g-pg, db=b-pb;
        int diff=dr*dr+dg*dg+db*db;
        if(diff<bestdiff){bestdiff=diff; best=i;}
    }
    return best;
}

/* Centralized helpers for bitmap buffer allocation and cleanup. */
static void free_bitmap_buffers(Bitmap *bm) {
    if (!bm) return;
    if (bm->idxbuf) { free(bm->idxbuf); bm->idxbuf = NULL; }
    if (bm->palette) { free(bm->palette); bm->palette = NULL; }
    bm->nb_colors = 0;
}

static int allocate_bitmap_buffers(Bitmap *bm, size_t w, size_t h, const char *palette_mode) {
    if (!bm) return 0;
    /* Per-dimension guard: reject obviously huge single-dimension values. */
    if (w == 0 || h == 0) return 0;
    if (w > RENDER_PANGO_SAFE_MAX_DIM || h > RENDER_PANGO_SAFE_MAX_DIM) return 0;
    size_t pix_count = 0;
    if (!mul_size_ok(w, h, &pix_count) || pix_count > RENDER_PANGO_SAFE_MAX_PIXELS) return 0;
    bm->idxbuf = (uint8_t*)calloc(pix_count, 1);
    if (!bm->idxbuf) return 0;
    bm->palette = (uint32_t*)safe_malloc_count(16, sizeof(uint32_t));
    if (!bm->palette) { free(bm->idxbuf); bm->idxbuf = NULL; return 0; }
    init_palette(bm->palette, palette_mode);
    /* record how many colors are valid in the palette */
    bm->nb_colors = 16;
    /* record buffer sizes for later validation */
    bm->idxbuf_len = pix_count;
    bm->palette_bytes = 16 * sizeof(uint32_t);
    return 1;
}

/* Choose nearest palette index by comparing display (premultiplied) RGB
 * values. Both source and palette entries are converted to their effective
 * displayed color over black: channel_display = channel * (alpha/255). */
/*
 * nearest_palette_index_display
 * -------------------------------
 * Perceptual nearest-index search that operates on display (premultiplied)
 * RGB components. Alpha mismatch is penalized to avoid picking
 * semi-transparent palette entries for nearly-opaque source pixels which
 * would produce dark halos after compositing.
 */
static int nearest_palette_index_display(uint32_t *palette, int npal, double rd, double gd, double bd, int src_alpha) {
    int best = 1;
    double bestdiff = 1e308;
    /* alpha mismatch penalty weight (squared alpha diff will be multiplied by this) */
    const double alpha_weight = 10.0; /* stronger penalty for alpha mismatch */
    for (int i = 0; i < npal; i++) {
        uint32_t p = palette[i];
        double pa = ((p >> 24) & 0xFF) / 255.0;
        /* If source pixel is nearly opaque, avoid choosing semi-transparent
         * palette entries which lead to composite-darkening halos. Prefer
         * opaque palette entries when possible. */
        if (src_alpha >= 240 && pa < 0.99) continue;
        /* Also avoid fully transparent entries for non-trivial source alpha. */
        if (src_alpha >= 16 && pa < 0.01) continue;
        /* Compute displayed (premultiplied) palette components */
        double pr = ((p >> 16) & 0xFF) * pa;
        double pg = ((p >> 8) & 0xFF) * pa;
        double pb = (p & 0xFF) * pa;
    double dr = rd - pr;
    double dg = gd - pg;
    double db = bd - pb;
    /* perceptual luma-weighted distance (Rec.709-like weights) favors
     * differences the eye notices more and can reduce perceivable blockiness */
    const double wr = 0.2126, wg = 0.7152, wb = 0.0722;
        double color_diff = wr * dr * dr + wg * dg * dg + wb * db * db;
        /* Prefer not to choose much darker palette entries for very bright
         * nearly-opaque source pixels — this helps avoid dark speckles
         * eating into bright glyph areas after quantization. Compute
         * a simple luminance bias that penalizes palette entries with
         * significantly lower luma than the source. */
        double src_luma = 0.2126 * rd + 0.7152 * gd + 0.0722 * bd;
        double pal_luma = 0.2126 * pr + 0.7152 * pg + 0.0722 * pb;
        if (src_alpha >= 200 && src_luma > 200.0 && pal_luma < src_luma - 20.0) {
            /* add a luma-penalty scaled by the luma gap */
            double gap = src_luma - pal_luma;
            color_diff += (gap * gap) * 0.08; /* tuned penalty */
        }
        double pa255 = pa * 255.0;
        double adiff = pa255 - (double)src_alpha;
        double diff = color_diff + alpha_weight * (adiff * adiff);
        if (diff < bestdiff) { bestdiff = diff; best = i; }
    }
    return best;
}

/* nearest_palette_index_display_skip_transparent: like above but skips index 0
 * (which is always fully transparent in DVB subtitles). Used for background color
 * quantization to ensure the background doesn't vanish. */
static int nearest_palette_index_display_skip_transparent(uint32_t *palette, int npal, double rd, double gd, double bd, int src_alpha) {
    int best = 1;
    double bestdiff = 1e308;
    const double alpha_weight = 10.0;
    for (int i = 1; i < npal; i++) {  /* START AT 1 TO SKIP TRANSPARENT INDEX 0 */
        uint32_t p = palette[i];
        double pa = ((p >> 24) & 0xFF) / 255.0;
        if (src_alpha >= 240 && pa < 0.99) continue;
        if (src_alpha >= 16 && pa < 0.01) continue;
        double pr = ((p >> 16) & 0xFF) * pa;
        double pg = ((p >> 8) & 0xFF) * pa;
        double pb = (p & 0xFF) * pa;
        double dr = rd - pr;
        double dg = gd - pg;
        double db = bd - pb;
        const double wr = 0.2126, wg = 0.7152, wb = 0.0722;
        double color_diff = wr * dr * dr + wg * dg * dg + wb * db * db;
        double src_luma = 0.2126 * rd + 0.7152 * gd + 0.0722 * bd;
        double pal_luma = 0.2126 * pr + 0.7152 * pg + 0.0722 * pb;
        if (src_alpha >= 200 && src_luma > 200.0 && pal_luma < src_luma - 20.0) {
            double gap = src_luma - pal_luma;
            color_diff += (gap * gap) * 0.08;
        }
        double pa255 = pa * 255.0;
        double adiff = pa255 - (double)src_alpha;
        double diff = color_diff + alpha_weight * (adiff * adiff);
        if (diff < bestdiff) { bestdiff = diff; best = i; }
    }
    return best;
}


/* Allocate and return an empty, NUL-terminated string. */
static char *alloc_empty_string(void) {
    char *s = malloc(1);
    if (s) s[0] = '\0';
    return s;
}

/*
 * srt_to_pango_markup
 * -------------------
 * Convert a small subset of SRT/HTML inline tags into Pango markup and
 * escape XML special characters. Allocates and returns a NUL-terminated
 * string; the caller must free it.
 */
char* srt_to_pango_markup(const char *srt_text) {
    if (!srt_text) return alloc_empty_string();
    size_t input_len = strlen(srt_text);
    /* conservative expansion factor (entities + markup) */
    size_t maxlen = input_len * 4 + 32;
    if (maxlen < 128) maxlen = 128;
    char *buf = (char*)malloc(maxlen);
    if (!buf) return alloc_empty_string();
    char *out = buf;
    const char *p = srt_text;
    while (*p) {
        size_t used = (size_t)(out - buf);
        if (used + 16 >= maxlen) break; /* keep some slack for worst-case writes */
        size_t remaining = maxlen - used;

        if (strncasecmp(p, "<i>", 3) == 0) {
            const char *tag = "<span style=\"italic\">";
            size_t tlen = strlen(tag);
            if (remaining <= tlen) break;
            memcpy(out, tag, tlen);
            out += tlen; p += 3;
            continue;
        }
        else if (strncasecmp(p, "</i>", 4) == 0) {
            const char *tag = "</span>";
            size_t tlen = strlen(tag);
            if (remaining <= tlen) break;
            memcpy(out, tag, tlen);
            out += tlen; p += 4;
            continue;
        }
        else if (strncasecmp(p, "<b>", 3) == 0) {
            const char *tag = "<span weight=\"bold\">";
            size_t tlen = strlen(tag);
            if (remaining <= tlen) break;
            memcpy(out, tag, tlen);
            out += tlen; p += 3;
            continue;
        }
        else if (strncasecmp(p, "</b>", 4) == 0) {
            const char *tag = "</span>";
            size_t tlen = strlen(tag);
            if (remaining <= tlen) break;
            memcpy(out, tag, tlen);
            out += tlen; p += 4;
            continue;
        }
        else if (strncasecmp(p, "<u>", 3) == 0) {
            const char *tag = "<span underline=\"single\">";
            size_t tlen = strlen(tag);
            if (remaining <= tlen) break;
            memcpy(out, tag, tlen);
            out += tlen; p += 3;
            continue;
        }
        else if (strncasecmp(p, "</u>", 4) == 0) {
            const char *tag = "</span>";
            size_t tlen = strlen(tag);
            if (remaining <= tlen) break;
            memcpy(out, tag, tlen);
            out += tlen; p += 4;
            continue;
        }
        else if (strncasecmp(p, "<font ", 6) == 0) {
            /* Convert <font color="#RRGGBB"> to <span foreground="..."> */
            const char *end = strchr(p, '>');
            if (end) {
                char tmp[256];
                size_t tlen = (size_t)(end - p + 1);
                if (tlen >= sizeof(tmp)) tlen = sizeof(tmp) - 1;
                memcpy(tmp, p, tlen);
                tmp[tlen] = '\0';
                char color[64] = "";
                if (sscanf(tmp, "<font color=\"%63[^\"]", color) == 1) {
                    int n = snprintf(out, remaining, "<span foreground=\"%s\">", color);
                    if (n < 0 || (size_t)n >= remaining) { /* truncated or error */
                        /* stop to avoid overflow */
                        out += remaining - 1;
                        out[0] = '\0';
                        break;
                    }
                    out += n;
                    p = end + 1;
                    continue;
                }
            }
            /* If unsupported, copy a single character to avoid dropping input */
            if (remaining <= 1) break;
            *out++ = *p++;
            continue;
        }
        else if (strncasecmp(p, "</font>", 7) == 0) {
            int n = snprintf(out, remaining, "</span>");
            if (n < 0 || (size_t)n >= remaining) break;
            out += n; p += 7; continue;
        }
        else {
            /* Escape XML entities */
            if (*p == '&') {
                const char *ent = "&amp;";
                size_t elen = 5;
                if (remaining <= elen) break;
                memcpy(out, ent, elen); out += elen; p++; continue;
            } else if (*p == '<') {
                const char *ent = "&lt;"; size_t elen = 4;
                if (remaining <= elen) break;
                memcpy(out, ent, elen); out += elen; p++; continue;
            } else if (*p == '>') {
                const char *ent = "&gt;"; size_t elen = 4;
                if (remaining <= elen) break;
                memcpy(out, ent, elen); out += elen; p++; continue;
            } else {
                if (remaining <= 1) break;
                *out++ = *p++;
                continue;
            }
        }
    }
    *out = '\0';
    return buf;
}

/*
 * render_text_pango
 * -----------------
 * Render the provided Pango markup into an indexed Bitmap suitable for
 * the DVB/AVSubtitle pipeline. High-level steps:
 *  1. Resolve font size (adaptive if fontsize<=0).
 *  2. Create a supersampled Cairo surface and render the Pango layout at
 *     higher resolution to improve edge quality.
 *  3. Optionally blur/sharpen the supersampled surface and downsample.
 *  4. Convert ARGB pixels to indexed palette using error-diffusion
 *     dithering plus cleanup passes to remove speckles and short runs.
 *
 * The returned Bitmap contains allocated `idxbuf` and `palette` which
 * the caller must free when no longer needed.
 */
Bitmap render_text_pango(const char *markup,
                          int disp_w, int disp_h,
                          int fontsize, const char *fontfam,
                          const char *fontstyle,
                          const char *fgcolor,
                          const char *outlinecolor,
                          const char *shadowcolor,
                          const char *bgcolor,
                          int align_code,
                          const char *palette_mode) {
    Bitmap bm={0};
    
    LOG(3, "DEBUG render_text_pango: bgcolor=%s\n", bgcolor ? bgcolor : "(null)");

    /* Local resource pointers: initialize to NULL so cleanup is safe on any
     * early-failure path. */
    PangoFontMap *thread_fm = NULL;
    PangoFontDescription *desc = NULL;
    PangoContext *ctx_dummy = NULL;
    PangoLayout *layout_dummy = NULL;
    cairo_surface_t *dummy = NULL;
    cairo_t *cr_dummy = NULL;
    PangoContext *ctx_real = NULL;
    PangoLayout *layout_real = NULL;
    cairo_surface_t *surface_ss = NULL;
    cairo_t *cr = NULL;
    cairo_surface_t *surface = NULL;
    bool success = false; /* set true only on full successful render */

    /* Ensure we have a single process-wide PangoFontMap to avoid creating
     * multiple internal fontconfig allocations across repeated renders.
     * We create contexts from this map per-render and unref them. */
    /* Ensure a per-thread fontmap exists (created on first use). */
    thread_fm = get_thread_pango_fontmap();
    if (!thread_fm) return bm; /* can't proceed without a fontmap */

    /* Defensive: refuse to attempt rendering for absurd display sizes that
     * are almost certainly a caller error or test of failure paths. This
     * avoids creating huge Cairo surfaces or layouts when disp_w/h are
     * unreasonably large (e.g., user-provided test harness values). */
    if ((size_t)disp_w > RENDER_PANGO_SAFE_MAX_DIM || (size_t)disp_h > RENDER_PANGO_SAFE_MAX_DIM) {
        /* return empty bitmap (idxbuf==NULL) to indicate we couldn't allocate */
        return bm;
    }

    /* --- Font size selection ---
     * If caller passed a positive fontsize (CLI --fontsize), respect it.
     * Otherwise compute a dynamic font size based on display height using
     * targeted ranges for SD, HD and UHD:
     *  SD:  ~18..22
     *  HD:  ~40..48
     *  UHD: ~80..88 */
    
    LOG(2, "render_text_pango: Input fontfam='%s' fontstyle='%s' fontsize=%d disp_h=%d\n",
        fontfam ? fontfam : "(null)", fontstyle ? fontstyle : "(null)", fontsize, disp_h);
    
    if (fontsize > 0) {
        /* respect caller-provided fontsize (no upper clamp) */
    } else {
        int f = 18;
        if (disp_h <= 576) {
            /* SD band: slightly smaller baseline to avoid vertical overflow.
             * Interpolate 19..24 over 0..576 (a small reduction from previous)
             * to make SD subtitles a bit less tall while keeping improved clarity. */
            double t = (double)disp_h / 576.0;
            if (t < 0.0) { t = 0.0; }
            if (t > 1.0) { t = 1.0; }
            double v = 19.0 + t * (24.0 - 19.0);
            f = (int)round(v);
        } else if (disp_h <= 1080) {
            /* HD band: interpolate 36..42 over 577..1080 so 1080 -> ~42 */
            double t = ((double)disp_h - 576.0) / (1080.0 - 576.0);
            if (t < 0.0) { t = 0.0; }
            if (t > 1.0) { t = 1.0; }
            double v = 36.0 + t * (42.0 - 36.0);
            f = (int)round(v);
        } else {
            /* UHD and larger: interpolate 82..88 over 1081..4320 so 2160 -> ~84 */
            double t = ((double)disp_h - 1080.0) / (4320.0 - 1080.0);
            if (t < 0.0) { t = 0.0; }
            if (t > 1.0) { t = 1.0; }
            double v = 82.0 + t * (88.0 - 82.0);
            f = (int)round(v);
        }
        fontsize = f;
        LOG(2, "render_text_pango: Adaptive fontsize calculated: %d\n", fontsize);
    }

    /* Create common font description */
    const char *base_family = (fontfam && *fontfam) ? fontfam : "Open Sans";
    char *desc_string = NULL;
    if (fontstyle && *fontstyle) {
        size_t len = strlen(base_family) + 1 + strlen(fontstyle) + 1;
        desc_string = malloc(len);
        if (desc_string)
            snprintf(desc_string, len, "%s %s", base_family, fontstyle);
    }
    if (desc_string)
        desc = pango_font_description_from_string(desc_string);
    if (!desc)
        desc = pango_font_description_from_string(base_family);
    if (!desc) {
        desc = pango_font_description_new();
        if (!desc) goto final_cleanup;
        pango_font_description_set_family(desc, base_family);
    }
    pango_font_description_set_absolute_size(desc, fontsize * PANGO_SCALE);
    
    LOG(2, "render_text_pango: Resolved font='%s' style='%s' size=%d (base_family resolved to '%s')\n",
        base_family, (fontstyle && *fontstyle) ? fontstyle : "(default)", fontsize, base_family);
    
    free(desc_string);

    /* --- Dummy layout for measurement --- */
    dummy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    if (!dummy || cairo_surface_status(dummy) != CAIRO_STATUS_SUCCESS) goto final_cleanup;
    cr_dummy = cairo_create(dummy);
    if (!cr_dummy || cairo_status(cr_dummy) != CAIRO_STATUS_SUCCESS) goto final_cleanup;
    ctx_dummy = pango_font_map_create_context(thread_fm);
    if (!ctx_dummy) goto final_cleanup;
    
    /* Apply same font options to dummy context for consistent measurement */
    cairo_font_options_t *fopt_dummy = cairo_font_options_create();
    if (disp_h <= 576) {
        cairo_font_options_set_hint_style(fopt_dummy, CAIRO_HINT_STYLE_NONE);
        cairo_font_options_set_hint_metrics(fopt_dummy, CAIRO_HINT_METRICS_OFF);
    } else {
        cairo_font_options_set_hint_style(fopt_dummy, CAIRO_HINT_STYLE_FULL);
        cairo_font_options_set_hint_metrics(fopt_dummy, CAIRO_HINT_METRICS_DEFAULT);
    }
    pango_cairo_context_set_font_options(ctx_dummy, fopt_dummy);
    cairo_set_font_options(cr_dummy, fopt_dummy);
    cairo_font_options_destroy(fopt_dummy);
    
    layout_dummy = pango_layout_new(ctx_dummy);
    if (!layout_dummy) goto final_cleanup;
    pango_layout_set_font_description(layout_dummy, desc);
    pango_layout_set_width(layout_dummy, disp_w * 0.8 * PANGO_SCALE);
    pango_layout_set_wrap(layout_dummy, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_alignment(layout_dummy, PANGO_ALIGN_CENTER);
    pango_layout_set_markup(layout_dummy, markup, -1);

    int lw, lh;

    pango_layout_get_pixel_size(layout_dummy, &lw, &lh);
    
    /* For centered text, create a layout with width matching the actual text width
     * so center alignment works properly without extra padding on the sides */
    int layout_width_for_real = lw;

    /* --- Placement in full frame --- */
    /* Position so bottom of text is 3% from bottom of frame */
    int text_x = (disp_w - lw) / 2;
    int text_y = disp_h - (int)(disp_h * 0.038) - lh;
    if (align_code >= 7) text_y = (int)(disp_h * 0.038);
    else if (align_code >= 4 && align_code <= 6) text_y = (disp_h - lh) / 2;

    /* --- Adaptive supersampled rendering surface ---
     * Choose supersample factor based on display height.
     * SD gets a higher SSAA to avoid blockiness. For HD/UHD we also
     * increase SSAA to keep glyph edges smooth at larger sizes.
     * These choices balance quality vs CPU/memory; users can override
     * with the ssaa_override runtime knob. */
    int ss;
    if (disp_h <= 576) {
    ss = 2; /* SD: increase supersample to further reduce blockiness on low res */
    } else if (disp_h <= 1080) {
    ss = 3; /* HD: bump to 3x to improve edge fidelity compared to previous 2x */
    } else if (disp_h <= 2160) {
    ss = 4; /* 4k/2160p target: use 4x for best quality on UHD */
    } else {
    ss = 4; /* very large displays: cap at 4x */
    }
    if (atomic_load(&dbg_ssaa_override) > 0) ss = atomic_load(&dbg_ssaa_override);
    /* pad to accommodate strokes when upscaled; scale with fontsize so
     * UHD/large text get enough room and strokes don't clip. */
    int pad = 8;
    if (fontsize > 48) pad = (int)ceil(fontsize * 0.25);
    int ss_w = (lw + 2*pad) * ss;
    int ss_h = (lh + 2*pad) * ss;
    surface_ss = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ss_w, ss_h);
    if (!surface_ss || cairo_surface_status(surface_ss) != CAIRO_STATUS_SUCCESS) goto final_cleanup;
    cr = cairo_create(surface_ss);
    if (!cr || cairo_status(cr) != CAIRO_STATUS_SUCCESS) goto final_cleanup;
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    cairo_scale(cr, (double)ss, (double)ss);  /* draw at larger resolution then downscale */

    ctx_real = pango_font_map_create_context(thread_fm);
    if (!ctx_real) goto final_cleanup;
    /* For HD/UHD with stronger supersampling we prefer to disable
     * hinting/metrics so glyph shapes remain smooth and rely on SSAA
     * for crisp edges. Apply cairo font options to both Cairo and Pango
     * contexts when ss is high. */
    cairo_font_options_t *fopt = cairo_font_options_create();
    if (ss >= 3) {
        cairo_font_options_set_hint_style(fopt, CAIRO_HINT_STYLE_NONE);
        cairo_font_options_set_hint_metrics(fopt, CAIRO_HINT_METRICS_OFF);
    } else {
        cairo_font_options_set_hint_style(fopt, CAIRO_HINT_STYLE_FULL);
        cairo_font_options_set_hint_metrics(fopt, CAIRO_HINT_METRICS_DEFAULT);
    }
    pango_cairo_context_set_font_options(ctx_real, fopt);
    cairo_set_font_options(cr, fopt);
    cairo_font_options_destroy(fopt);
    layout_real = pango_layout_new(ctx_real);
    if (!layout_real) goto final_cleanup;
    pango_layout_set_font_description(layout_real, desc);
    pango_layout_set_width(layout_real, layout_width_for_real * PANGO_SCALE);
    pango_layout_set_wrap(layout_real, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_alignment(layout_real, PANGO_ALIGN_CENTER);
    pango_layout_set_markup(layout_real, markup, -1);

    double fr,fg,fb,fa, or_,og,ob,oa, sr,sg,sb,sa;
    parse_hex_color(fgcolor, &fr,&fg,&fb,&fa);
    parse_hex_color(outlinecolor, &or_,&og,&ob,&oa);
    parse_hex_color(shadowcolor, &sr,&sg,&sb,&sa);

    /* Translate to center text within the padded bounding box */
    cairo_translate(cr, (double)pad, (double)pad);

    // Shadow first: make the offset proportional to supersample / fontsize
    if (shadowcolor) {
        /* Compute shadow offset in user-space units; a prior change
         * multiplied by `ss` here which double-applied the earlier
         * cairo_scale() and produced an overly large shadow translate.
         * Keep a small minimum of 1.0 user unit so very small fonts
         * still get a visible shadow. The CTM (scale) already maps
         * this to device pixels. */
        double shadow_off = (fontsize * 0.04);
        if (shadow_off < 1.0) shadow_off = 1.0; /* at least 1 user unit */
        cairo_save(cr);
        cairo_translate(cr, shadow_off, shadow_off);
        cairo_set_source_rgba(cr, sr, sg, sb, sa);
        pango_cairo_show_layout(cr, layout_real);
        cairo_restore(cr);
    }

    // Outline: stroke path. Make stroke width proportional to fontsize so
    // it scales better across SD/HD; shrink slightly for high supersample
    // so outlines don't become visually too thick when downsampled.
    cairo_save(cr);
    /* Make stroke width proportional to fontsize, but scale it down slightly
     * for higher SSAA so strokes don't appear overly thick after downsampling. */
    double stroke_w = 0.9 + (fontsize * 0.045);
    /* For very large displays we thin the stroke a bit when SSAA is high
     * to avoid overly chunky outlines after downsampling. For SD we keep
     * the stroke thicker to prevent small glyph features from being eaten. */
    if (ss >= 4 && disp_h > 576) stroke_w *= 0.70; /* thinner at 4x for HD/UHD */
    cairo_set_line_width(cr, stroke_w);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    pango_cairo_layout_path(cr, layout_real);
    cairo_set_source_rgba(cr, or_, og, ob, oa);
    cairo_stroke(cr);
    cairo_restore(cr);

    // Foreground fill
    cairo_save(cr);
    cairo_set_source_rgba(cr, fr, fg, fb, fa);
    pango_cairo_show_layout(cr, layout_real);
    cairo_restore(cr);

    /* --- Downscale to target surface (1×) ---
     * Optionally apply a mild separable blur on the supersampled surface to
     * reduce remaining high-frequency aliasing before downsampling. For
     * SSAA==3 a compact 3x3 box is used; for larger SSAA we use a 5-tap
     * separable kernel that better approximates a small Gaussian. The
     * amount is intentionally tiny so glyph shapes stay readable but edges
     * become visually smoother after downsample. */
    if (ss >= 3) {
        /* For HD displays (<=1080) we run an additional separable blur in
         * linear light on the supersampled surface to smooth curved glyph
         * edges more naturally. Converting to linear and back here gives
         * perceptually better blurring for luminance and reduces blocky
         * stepping on curves. This is intentionally only enabled for HD
         * to limit CPU cost. */
        if (disp_h <= 1080) {
            unsigned char *ss_data_ls = cairo_image_surface_get_data(surface_ss);
            if (!ss_data_ls) goto final_cleanup;
            int ss_stride_ls = cairo_image_surface_get_stride(surface_ss);
            int sw_ls = ss_w, sh_ls = ss_h;
            /* horizontal -> tmp buffer */
            double *tmp_r = NULL;
            double *tmp_g = NULL;
            double *tmp_b = NULL;
            size_t area_ls = 0;
            if (mul_size_ok((size_t)sw_ls, (size_t)sh_ls, &area_ls) && area_ls <= RENDER_PANGO_SAFE_MAX_PIXELS) {
                tmp_r = (double*)safe_malloc_count(area_ls, sizeof(double));
                tmp_g = (double*)safe_malloc_count(area_ls, sizeof(double));
                tmp_b = (double*)safe_malloc_count(area_ls, sizeof(double));
            }
            if (tmp_r && tmp_g && tmp_b) {
                /* convert to linear (approx sRGB->linear) and horizontal blur (7-tap separable)
                 * Use a wider kernel on HD to smooth curve stepping more aggressively.
                 * Weights chosen approximate a small Gaussian: 1,6,15,20,15,6,1 (sum=64).
                 *
                 * Slightly stronger smoothing: increase wing/near-center weights
                 * to better reduce small curved-step artifacts without blurring
                 * shape excessively. New weights: 1,8,20,24,20,8,1 (sum=82). */
                const int wts[7] = {1,8,20,24,20,8,1};
                const int wsum = 82;
                for (int y = 0; y < sh_ls; y++) {
                    for (int x = 0; x < sw_ls; x++) {
                        // uint32_t px = *(uint32_t*)(ss_data_ls + y*ss_stride_ls + x*4);
                        // double r = ((px >> 16) & 0xFF) / 255.0;
                        // double g = ((px >> 8) & 0xFF) / 255.0;
                        // double b = (px & 0xFF) / 255.0;
                        /* sRGB to linear approx (cheap piecewise inline) */
                        /* per-pixel linear conversion computed for neighbors below;
                         * the immediate lr/lg/lb values are not used here and removed
                         * to avoid -Wunused-variable. */
                        double sumr = 0.0, sumg = 0.0, sumb = 0.0;
                        for (int k = -3; k <= 3; k++) {
                            int xx = x + k;
                            if (xx < 0) xx = 0;
                            if (xx >= sw_ls) xx = sw_ls - 1;
                            uint32_t p2 = *(uint32_t*)(ss_data_ls + y*ss_stride_ls + xx*4);
                            double r2 = ((p2 >> 16) & 0xFF) / 255.0;
                            double g2 = ((p2 >> 8) & 0xFF) / 255.0;
                            double b2 = (p2 & 0xFF) / 255.0;
                            double lr2 = (r2 <= 0.04045) ? (r2 / 12.92) : pow((r2 + 0.055) / 1.055, 2.4);
                            double lg2 = (g2 <= 0.04045) ? (g2 / 12.92) : pow((g2 + 0.055) / 1.055, 2.4);
                            double lb2 = (b2 <= 0.04045) ? (b2 / 12.92) : pow((b2 + 0.055) / 1.055, 2.4);
                            int wi = k + 3;
                            sumr += lr2 * wts[wi];
                            sumg += lg2 * wts[wi];
                            sumb += lb2 * wts[wi];
                        }
                        tmp_r[y*sw_ls + x] = sumr / (double)wsum;
                        tmp_g[y*sw_ls + x] = sumg / (double)wsum;
                        tmp_b[y*sw_ls + x] = sumb / (double)wsum;
                    }
                }
                /* vertical pass: write back converting linear->sRGB approx (7-tap) */
                for (int y = 0; y < sh_ls; y++) {
                    for (int x = 0; x < sw_ls; x++) {
                        double sumr = 0.0, sumg = 0.0, sumb = 0.0;
                        for (int k = -3; k <= 3; k++) {
                            int yy = y + k;
                            if (yy < 0) yy = 0;
                            if (yy >= sh_ls) yy = sh_ls - 1;
                            int wi = k + 3;
                            sumr += tmp_r[yy*sw_ls + x] * wts[wi];
                            sumg += tmp_g[yy*sw_ls + x] * wts[wi];
                            sumb += tmp_b[yy*sw_ls + x] * wts[wi];
                        }
                        double lr = sumr / (double)wsum; double lg = sumg / (double)wsum; double lb = sumb / (double)wsum;
                        /* linear to sRGB approx */
                        double rr = (lr <= 0.0031308) ? lr * 12.92 : 1.055 * pow(lr, 1.0/2.4) - 0.055;
                        double gg = (lg <= 0.0031308) ? lg * 12.92 : 1.055 * pow(lg, 1.0/2.4) - 0.055;
                        double bb = (lb <= 0.0031308) ? lb * 12.92 : 1.055 * pow(lb, 1.0/2.4) - 0.055;
                        uint8_t ir = (uint8_t)fmin(255.0, fmax(0.0, rr * 255.0 + 0.5));
                        uint8_t ig = (uint8_t)fmin(255.0, fmax(0.0, gg * 255.0 + 0.5));
                        uint8_t ib = (uint8_t)fmin(255.0, fmax(0.0, bb * 255.0 + 0.5));
                        unsigned char *dstp = ss_data_ls + y * ss_stride_ls + x * 4;
                        uint32_t old = *(uint32_t*)dstp;
                        uint8_t oa = (old >> 24) & 0xFF;
                        *(uint32_t*)dstp = ((uint32_t)oa<<24) | ((uint32_t)ir<<16) | ((uint32_t)ig<<8) | (uint32_t)ib;
                    }
                }
            }
            free(tmp_r); free(tmp_g); free(tmp_b);
            cairo_surface_mark_dirty(surface_ss);
        }
    unsigned char *ss_data = cairo_image_surface_get_data(surface_ss);
    if (!ss_data) goto final_cleanup;
    int ss_stride = cairo_image_surface_get_stride(surface_ss);
        int sw = ss_w, sh = ss_h;
        uint32_t *tmp = NULL;
        uint32_t *tmp2 = NULL;
        size_t area = 0;
        if (mul_size_ok((size_t)sw, (size_t)sh, &area) && area <= RENDER_PANGO_SAFE_MAX_PIXELS) {
            tmp = (uint32_t*)safe_malloc_count(area, sizeof(uint32_t));
        }
        if (tmp) {
            /* Horizontal pass -> tmp */
            if (ss == 3) {
                for (int y = 0; y < sh; y++) {
                    for (int x = 0; x < sw; x++) {
                        uint64_t sa=0,sr=0,sg=0,sb=0; int cnt=0;
                        for (int dx=-1; dx<=1; dx++) {
                            int xx = x + dx;
                            if (xx < 0 || xx >= sw) continue;
                            uint32_t px = *(uint32_t*)(ss_data + y*ss_stride + xx*4);
                            sa += (px >> 24) & 0xFF;
                            sr += (px >> 16) & 0xFF;
                            sg += (px >> 8) & 0xFF;
                            sb += px & 0xFF;
                            cnt++;
                        }
                        uint8_t na = sa / cnt;
                        uint8_t nr = sr / cnt;
                        uint8_t ng = sg / cnt;
                        uint8_t nb = sb / cnt;
                        tmp[y*sw + x] = ((uint32_t)na<<24) | ((uint32_t)nr<<16) | ((uint32_t)ng<<8) | (uint32_t)nb;
                    }
                }
                /* vertical pass writing back into surface data */
                for (int y = 0; y < sh; y++) {
                    for (int x = 0; x < sw; x++) {
                        uint64_t sa=0,sr=0,sg=0,sb=0; int cnt=0;
                        for (int dy=-1; dy<=1; dy++) {
                            int yy = y + dy;
                            if (yy < 0 || yy >= sh) continue;
                            uint32_t px = tmp[yy*sw + x];
                            sa += (px >> 24) & 0xFF;
                            sr += (px >> 16) & 0xFF;
                            sg += (px >> 8) & 0xFF;
                            sb += px & 0xFF;
                            cnt++;
                        }
                        uint8_t na = sa / cnt;
                        uint8_t nr = sr / cnt;
                        uint8_t ng = sg / cnt;
                        uint8_t nb = sb / cnt;
                        unsigned char *dstp = ss_data + y * ss_stride + x * 4;
                        *(uint32_t*)dstp = ((uint32_t)na<<24) | ((uint32_t)nr<<16) | ((uint32_t)ng<<8) | (uint32_t)nb;
                    }
                }
            } else {
                /* 5-tap separable kernel (1,4,6,4,1)/16 approximating a small Gaussian */
                size_t area_tmp2 = 0;
                if (mul_size_ok((size_t)sw, (size_t)sh, &area_tmp2) && area_tmp2 <= RENDER_PANGO_SAFE_MAX_PIXELS) {
                    tmp2 = (uint32_t*)safe_malloc_count(area_tmp2, sizeof(uint32_t));
                }
                if (tmp2) {
                    for (int y = 0; y < sh; y++) {
                        for (int x = 0; x < sw; x++) {
                            uint64_t sa=0,sr=0,sg=0,sb=0; int wsum=0;
                            for (int k=-2;k<=2;k++) {
                                int xx = x + k;
                                if (xx < 0 || xx >= sw) continue;
                                int weight = (k==0)?6:((abs(k)==1)?4:1);
                                uint32_t px = *(uint32_t*)(ss_data + y*ss_stride + xx*4);
                                sa += ((px >> 24) & 0xFF) * weight;
                                sr += ((px >> 16) & 0xFF) * weight;
                                sg += ((px >> 8) & 0xFF) * weight;
                                sb += (px & 0xFF) * weight;
                                wsum += weight;
                            }
                            uint8_t na = sa / wsum;
                            uint8_t nr = sr / wsum;
                            uint8_t ng = sg / wsum;
                            uint8_t nb = sb / wsum;
                            tmp[y*sw + x] = ((uint32_t)na<<24) | ((uint32_t)nr<<16) | ((uint32_t)ng<<8) | (uint32_t)nb;
                        }
                    }
                    /* vertical pass from tmp -> tmp2 */
                    for (int y = 0; y < sh; y++) {
                        for (int x = 0; x < sw; x++) {
                            uint64_t sa=0,sr=0,sg=0,sb=0; int wsum=0;
                            for (int k=-2;k<=2;k++) {
                                int yy = y + k;
                                if (yy < 0 || yy >= sh) continue;
                                int weight = (k==0)?6:((abs(k)==1)?4:1);
                                uint32_t px = tmp[yy*sw + x];
                                sa += ((px >> 24) & 0xFF) * weight;
                                sr += ((px >> 16) & 0xFF) * weight;
                                sg += ((px >> 8) & 0xFF) * weight;
                                sb += (px & 0xFF) * weight;
                                wsum += weight;
                            }
                            uint8_t na = sa / wsum;
                            uint8_t nr = sr / wsum;
                            uint8_t ng = sg / wsum;
                            uint8_t nb = sb / wsum;
                            unsigned char *dstp = ss_data + y * ss_stride + x * 4;
                            *(uint32_t*)dstp = ((uint32_t)na<<24) | ((uint32_t)nr<<16) | ((uint32_t)ng<<8) | (uint32_t)nb;
                        }
                    }
                }
                if (tmp2) free(tmp2);
            }
            free(tmp);
            cairo_surface_mark_dirty(surface_ss);
        }
    }
    int w = lw+2*pad;
    int h = lh+2*pad;
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) goto final_cleanup;
    
    /* Composite the downsampled text without background fill */
    {
        cairo_t *cr_down = cairo_create(surface);
        if (cr_down && cairo_status(cr_down) == CAIRO_STATUS_SUCCESS) {
            /* shrink from ss× to 1× using the adaptive supersample factor */
            cairo_scale(cr_down, 1.0 / (double)ss, 1.0 / (double)ss);
            cairo_set_source_surface(cr_down, surface_ss, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr_down), CAIRO_FILTER_BEST);
            cairo_paint(cr_down);
        }
        if (cr_down) cairo_destroy(cr_down);
    }

    unsigned char *data = cairo_image_surface_get_data(surface);
    if (!data) goto final_cleanup;

    /* Tiny unsharp mask to restore edge crispness after downsampling.
     * We use a small 3x3 box blur to get a blurred version, then add
     * amount*(orig - blur) back to the original. Operates on premultiplied
     * ARGB channels (This is acceptable for a small amount). */
    if (!atomic_load(&dbg_no_unsharp))
    {
        /* Reduce unsharp amount for higher SSAA: when supersampling is strong
         * we need much less sharpening (or almost none) to avoid reintroducing
         * blockiness.
         * Disable unsharp at very high SSAA to avoid reintroducing
         * aliasing-like artifacts; reduce amount progressively as ss grows. */
        double amount = 0.6; /* default strength */
        if (ss >= 6)
            amount = 0.0; /* very high SSAA: no sharpening */
        else if (ss >= 4)
            amount = 0.30;
        else if (ss == 3)
            amount = 0.5;
        int sw = w, sh = h;
        int stride = cairo_image_surface_get_stride(surface);
        uint32_t *orig = NULL;
        uint32_t *blur = NULL;
        size_t area3 = 0;
        if (mul_size_ok((size_t)sw, (size_t)sh, &area3) && area3 <= RENDER_PANGO_SAFE_MAX_PIXELS)
        {
            orig = (uint32_t *)safe_malloc_count(area3, sizeof(uint32_t));
            blur = (uint32_t *)safe_malloc_count(area3, sizeof(uint32_t));
        }
        if (orig && blur)
        {
            /* copy pixels */
            for (int y = 0; y < sh; y++)
            {
                for (int x = 0; x < sw; x++)
                {
                    orig[y * sw + x] = *(uint32_t *)(data + y * stride + x * 4);
                }
            }
            /* compute 3x3 box blur into blur[] */
            for (int y = 0; y < sh; y++)
            {
                for (int x = 0; x < sw; x++)
                {
                    uint64_t sa = 0, sr = 0, sg = 0, sb = 0;
                    int cnt = 0;
                    for (int dy = -1; dy <= 1; dy++)
                    {
                        int yy = y + dy;
                        if (yy < 0 || yy >= sh)
                            continue;
                        for (int dx = -1; dx <= 1; dx++)
                        {
                            int xx = x + dx;
                            if (xx < 0 || xx >= sw)
                                continue;
                            uint32_t px = orig[yy * sw + xx];
                            sa += (px >> 24) & 0xFF;
                            sr += (px >> 16) & 0xFF;
                            sg += (px >> 8) & 0xFF;
                            sb += px & 0xFF;
                            cnt++;
                        }
                    }
                    uint8_t na = sa / cnt;
                    uint8_t nr = sr / cnt;
                    uint8_t ng = sg / cnt;
                    uint8_t nb = sb / cnt;
                    blur[y * sw + x] = ((uint32_t)na << 24) | ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
                }
            }
            /* apply unsharp: dest = clamp(orig + amount*(orig - blur)) */
            for (int y = 0; y < sh; y++)
            {
                for (int x = 0; x < sw; x++)
                {
                    uint32_t o = orig[y * sw + x];
                    uint32_t b = blur[y * sw + x];
                    int oa = (o >> 24) & 0xFF;
                    int orr = (o >> 16) & 0xFF;
                    int ogr = (o >> 8) & 0xFF;
                    int ob = o & 0xFF;
                    /* 'ba' (blur alpha) was extracted previously but not used; omit it to avoid -Wunused-variable. */
                    int brr = (b >> 16) & 0xFF;
                    int bgr = (b >> 8) & 0xFF;
                    int bb = b & 0xFF;
                    int na = oa; /* keep alpha */
                    int nr = (int)round(orr + amount * (orr - brr));
                    int ng = (int)round(ogr + amount * (ogr - bgr));
                    int nbv = (int)round(ob + amount * (ob - bb));
                    if (nr < 0)
                        nr = 0;
                    if (nr > 255)
                        nr = 255;
                    if (ng < 0)
                        ng = 0;
                    if (ng > 255)
                        ng = 255;
                    if (nbv < 0)
                        nbv = 0;
                    if (nbv > 255)
                        nbv = 255;
                    *(uint32_t *)(data + y * stride + x * 4) = ((uint32_t)na << 24) | ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nbv;
                }
            }
        }
        free(orig);
        free(blur);
    }

    /* HD edge-aware smoothing: for HD (disp_h <= 1080) and high SSAA,
     * apply a small 3x3 smoothing on semi-transparent edge pixels that
     * weights neighbors by alpha and color similarity in premultiplied
     * space. This reduces blocky, high-contrast quantization edges while
     * preserving solid strokes. */
    if (disp_h <= 1080 && ss >= 3) {
        int sw = w, sh = h;
        int stride = cairo_image_surface_get_stride(surface);
        uint32_t *tmp_edge = NULL;
        size_t area4 = 0;
        if (mul_size_ok((size_t)sw, (size_t)sh, &area4) && area4 <= RENDER_PANGO_SAFE_MAX_PIXELS) {
            tmp_edge = (uint32_t*)safe_malloc_count(area4, sizeof(uint32_t));
        }
        if (tmp_edge) {
            /* copy current premultiplied pixels into tmp_edge grid */
            for (int y = 0; y < sh; y++) {
                for (int x = 0; x < sw; x++) {
                    tmp_edge[y*sw + x] = *(uint32_t*)(data + y*stride + x*4);
                }
            }
            const double thr = 60.0; /* color distance threshold */
            const double thr2 = thr*thr;
            for (int y = 0; y < sh; y++) {
                for (int x = 0; x < sw; x++) {
                    uint32_t c = tmp_edge[y*sw + x];
                    int ca = (c >> 24) & 0xFF;
                    if (ca <= 20 || ca >= 250) continue; /* skip almost-transparent or fully opaque */
                    double sum_a=0.0, sum_r=0.0, sum_g=0.0, sum_b=0.0, wsum=0.0;
                    int cx = (c >> 16) & 0xFF;
                    int cg = (c >> 8) & 0xFF;
                    int cb = c & 0xFF;
                    for (int dy=-1; dy<=1; dy++) {
                        int yy = y + dy; if (yy < 0 || yy >= sh) continue;
                        for (int dx=-1; dx<=1; dx++) {
                            int xx = x + dx; if (xx < 0 || xx >= sw) continue;
                            uint32_t n = tmp_edge[yy*sw + xx];
                            int na = (n >> 24) & 0xFF;
                            int nr = (n >> 16) & 0xFF;
                            int ng = (n >> 8) & 0xFF;
                            int nb = n & 0xFF;
                            double dr = (double)nr - (double)cx;
                            double dg = (double)ng - (double)cg;
                            double db = (double)nb - (double)cb;
                            double dist2 = dr*dr + dg*dg + db*db;
                            double sim = 0.0;
                            if (dist2 < thr2) sim = (thr2 - dist2) / thr2; /* 0..1 */
                            double alpha_w = (double)na / 255.0;
                            double w = alpha_w * sim;
                            if (w > 0.0) {
                                sum_a += na * w;
                                sum_r += nr * w;
                                sum_g += ng * w;
                                sum_b += nb * w;
                                wsum += w;
                            }
                        }
                    }
                    if (wsum > 0.0) {
                        uint8_t out_a = (uint8_t)fmin(255.0, sum_a / wsum + 0.5);
                        uint8_t out_r = (uint8_t)fmin(255.0, sum_r / wsum + 0.5);
                        uint8_t out_g = (uint8_t)fmin(255.0, sum_g / wsum + 0.5);
                        uint8_t out_b = (uint8_t)fmin(255.0, sum_b / wsum + 0.5);
                        *(uint32_t*)(data + y*stride + x*4) = ((uint32_t)out_a<<24) | ((uint32_t)out_r<<16) | ((uint32_t)out_g<<8) | (uint32_t)out_b;
                    }
                }
            }
            free(tmp_edge);
            cairo_surface_mark_dirty(surface);
        }
    }

    /* HD tangent blur: detect strong horizontal or vertical edge runs and
     * apply a short 1D blur along the dominant orientation to smooth
     * straight edges while preserving curves. Operates on premultiplied
     * ARGB in the 1x surface. */
    if (disp_h <= 1080 && ss >= 3) {
        int sw = w, sh = h;
        int stride = cairo_image_surface_get_stride(surface);
        uint32_t *tmp = NULL;
        size_t area5 = 0;
        if (mul_size_ok((size_t)sw, (size_t)sh, &area5) && area5 <= RENDER_PANGO_SAFE_MAX_PIXELS) {
            tmp = (uint32_t*)safe_malloc_count(area5, sizeof(uint32_t));
        }
        if (tmp) {
            /* copy current pixels */
            for (int y = 0; y < sh; y++) for (int x = 0; x < sw; x++) tmp[y*sw + x] = *(uint32_t*)(data + y*stride + x*4);
            /* detect and blur short 1D runs */
            for (int y = 1; y < sh-1; y++) {
                for (int x = 1; x < sw-1; x++) {
                    uint32_t c = tmp[y*sw + x];
                    int ca = (c >> 24) & 0xFF;
                    if (ca < 24 || ca > 250) continue;
                    /* compute local gradient magnitude using simple sobel-ish */
                    int cx = (c >> 16) & 0xFF; int cg = (c >> 8) & 0xFF; int cb = c & 0xFF;
                    int gx = 0, gy = 0;
                    for (int dx=-1; dx<=1; dx++) {
                        for (int dy=-1; dy<=1; dy++) {
                            if (dx==0 && dy==0) continue;
                            uint32_t n = tmp[(y+dy)*sw + (x+dx)];
                            int nr = (n >> 16) & 0xFF; int ng = (n >> 8) & 0xFF; int nb = n & 0xFF;
                            int nl = (int)(0.2126*nr + 0.7152*ng + 0.0722*nb);
                            int cl = (int)(0.2126*cx + 0.7152*cg + 0.0722*cb);
                            int diff = nl - cl;
                            gx += diff * dx;
                            gy += diff * dy;
                        }
                    }
                    int ag = abs(gx) + abs(gy);
                    /* Lower the threshold further to catch subtler curvature
                     * and smooth gentle curves. Keep high enough to avoid
                     * blurring flat areas. */
                    if (ag < 22) continue; /* weak edge: skip */
                    /* orientation: prefer horizontal if |gx| > |gy| */
                    if (abs(gx) > abs(gy)) {
                        /* horizontal tangent blur: average across x neighbors */
                        int sumr=0,sumg=0,sumb=0,sumw=0;
                        /* stronger central weight and slightly wider support to smooth curves */
                        for (int k=-3;k<=3;k++) {
                            int xx = x + k; if (xx < 0 || xx >= sw) continue;
                            uint32_t p = tmp[y*sw + xx];
                            int pr = (p >> 16) & 0xFF; int pg = (p >> 8) & 0xFF; int pb = p & 0xFF;
                            int wgt = (k==0)?10:((abs(k)==1)?8:((abs(k)==2)?4:1));
                            sumr += pr * wgt; sumg += pg * wgt; sumb += pb * wgt; sumw += wgt;
                        }
                        uint8_t nr = sumr / sumw; uint8_t ng = sumg / sumw; uint8_t nb = sumb / sumw;
                        uint8_t na = (c >> 24) & 0xFF;
                        *(uint32_t*)(data + y*stride + x*4) = ((uint32_t)na<<24) | ((uint32_t)nr<<16) | ((uint32_t)ng<<8) | (uint32_t)nb;
                    } else {
                        /* vertical tangent blur */
                        int sumr=0,sumg=0,sumb=0,sumw=0;
                        for (int k=-3;k<=3;k++) {
                            int yy = y + k; if (yy < 0 || yy >= sh) continue;
                            uint32_t p = tmp[yy*sw + x];
                            int pr = (p >> 16) & 0xFF; 
                            int pg = (p >> 8) & 0xFF; 
                            int pb = p & 0xFF; 
                            int wgt = (k==0)?10:((abs(k)==1)?8:((abs(k)==2)?4:1));
                            sumr += pr * wgt; sumg += pg * wgt; sumb += pb * wgt; sumw += wgt;
                        }
                        uint8_t nr = sumr / sumw; uint8_t ng = sumg / sumw; uint8_t nb = sumb / sumw;
                        uint8_t na = (c >> 24) & 0xFF;
                        *(uint32_t*)(data + y*stride + x*4) = ((uint32_t)na<<24) | ((uint32_t)nr<<16) | ((uint32_t)ng<<8) | (uint32_t)nb;
                    }
                }
            }
            free(tmp);
            cairo_surface_mark_dirty(surface);
        }
    }

    /* Guard final bitmap allocations (idxbuf + palette). Centralize the
     * allocation and pixel-limit logic via allocate_bitmap_buffers(). */
    if (!allocate_bitmap_buffers(&bm, (size_t)w, (size_t)h, palette_mode)) {
        /* skip heavy allocation and return an empty bitmap */
        goto final_cleanup;
    }

    int stride = cairo_image_surface_get_stride(surface);

    /* precompute foreground premultiplied display components (0..255) */
    double fg_disp_r = fr * fa * 255.0;
    double fg_disp_g = fg * fa * 255.0;
    double fg_disp_b = fb * fa * 255.0;
    int fg_palette_idx = nearest_palette_index_display(bm.palette, 16,
                                                       fg_disp_r, fg_disp_g, fg_disp_b, 255);

    /* Precompute background palette index if bgcolor is specified */
    int bg_palette_idx = 1;  /* default to white */
    double bg_disp_r = 0, bg_disp_g = 0, bg_disp_b = 0;
    if (bgcolor) {
        double bgr=0, bgg=0, bgb=0, bga=0;
        parse_bgcolor(bgcolor, &bgr, &bgg, &bgb, &bga);
        bg_disp_r = bgr * 255.0;
        bg_disp_g = bgg * 255.0;
        bg_disp_b = bgb * 255.0;
        /* Use skip-transparent version to ensure background never maps to index 0 */
        bg_palette_idx = nearest_palette_index_display_skip_transparent(bm.palette, 16,
                                                                        bg_disp_r, bg_disp_g, bg_disp_b, 255);
        LOG(3, "DEBUG: bgcolor=%s parsed to RGB(%f,%f,%f), quantized to palette index %d\n",
            bgcolor, bgr, bgg, bgb, bg_palette_idx);
        uint32_t bgp = bm.palette[bg_palette_idx];
        LOG(3, "DEBUG: palette[%d] = 0x%08x\n", bg_palette_idx, bgp);
    }

    /* Apply Floyd–Steinberg error-diffusion dithering to reduce banding and
     * blockiness when mapping down to the limited 16-color palette. We keep
     * per-channel error buffers for the current and next row. */
    size_t errlen = 0;
    if (!mul_size_ok((size_t)w + 2, 1, &errlen)) errlen = 0;
    double *err_r_cur = errlen ? (double*)calloc((size_t)w+2, sizeof(double)) : NULL;
    double *err_g_cur = errlen ? (double*)calloc((size_t)w+2, sizeof(double)) : NULL;
    double *err_b_cur = errlen ? (double*)calloc((size_t)w+2, sizeof(double)) : NULL;
    double *err_r_next = errlen ? (double*)calloc((size_t)w+2, sizeof(double)) : NULL;
    double *err_g_next = errlen ? (double*)calloc((size_t)w+2, sizeof(double)) : NULL;
    double *err_b_next = errlen ? (double*)calloc((size_t)w+2, sizeof(double)) : NULL;
    if (!err_r_cur || !err_g_cur || !err_b_cur || !err_r_next || !err_g_next || !err_b_next) {
    free(err_r_cur); free(err_g_cur); free(err_b_cur);
    free(err_r_next); free(err_g_next); free(err_b_next);
    /* deallocate bitmap allocations made earlier */
    free_bitmap_buffers(&bm);
    goto final_cleanup;
    }

    for (int yy = 0; yy < h; yy++) {
        /* zero the next-row error buffers */
        for (int i = 0; i < w+2; i++) { err_r_next[i] = err_g_next[i] = err_b_next[i] = 0.0; }

        int bg_pixel_count = 0;
        for (int xx = 0; xx < w; xx++) {
            uint32_t argb = *(uint32_t*)(data + yy*stride + xx*4);
            uint8_t a = (argb >> 24) & 0xFF;
                /* lower the threshold so very soft antialiased edges still
                 * pick nearby colors instead of being forced transparent. */
                if (a < 16) {
                    bg_pixel_count++;
                    /* Fully transparent pixels: if they contain a background color
                     * (from the cairo_fill), use the precomputed background palette index.
                     * Otherwise map to transparent. */
                    if (bgcolor) {
                        if (bg_pixel_count <= 5) {  /* Log only first 5 per row */
                            LOG(3, "DEBUG: Row %d: Using precomputed bg_palette_idx=%d for transparent pixel at x=%d\n", yy, bg_palette_idx, xx);
                        }
                        bm.idxbuf[yy*w+xx] = bg_palette_idx;
                    } else {
                        /* No background: map to transparent */
                        bm.idxbuf[yy*w+xx] = 0;
                    }
                    continue;
                }

            /* Use premultiplied/display components directly (Cairo stores
             * premultiplied ARGB). Operating in display space avoids halos
             * when compositing semi-transparent pixels into the 16-color
             * palette. Errors are propagated in display units (0..255). */
            if (a >= 220) {
                /* For opaque pixels, always use foreground palette index.
                 * Background pixels (if any) should be transparent or rendered
                 * separately, so all rendered pixels are foreground text. */
                bm.idxbuf[yy*w+xx] = fg_palette_idx;
                err_r_cur[xx+1] = err_g_cur[xx+1] = err_b_cur[xx+1] = 0.0;
                continue;
            }
            bool skip_diffuse = (a >= 210);
            double rd = (double)((argb >> 16) & 0xFF) + (skip_diffuse ? 0.0 : err_r_cur[xx+1]);
            double gd = (double)((argb >> 8) & 0xFF) + (skip_diffuse ? 0.0 : err_g_cur[xx+1]);
            double bd = (double)(argb & 0xFF) + (skip_diffuse ? 0.0 : err_b_cur[xx+1]);
            if (rd < 0) rd = 0; 
            if (rd > 255) rd = 255;
            if (gd < 0) gd = 0; 
            if (gd > 255) gd = 255;
            if (bd < 0) bd = 0; 
            if (bd > 255) bd = 255;
            /* Bias semi-transparent glyph edge pixels toward foreground
             * display color to reduce dark halos when quantizing to 16 colors. */
            if (!skip_diffuse && a > 24 && a < 255) {
                double rd_orig = rd;
                double gd_orig = gd;
                double bd_orig = bd;
                double diff_fg = fabs(rd_orig - fg_disp_r) +
                                 fabs(gd_orig - fg_disp_g) +
                                 fabs(bd_orig - fg_disp_b);
                if (diff_fg < 96.0) {
                    double an = (double)a / 255.0;
                    double bias = pow(an, 1.05) * 0.6; /* tuned base */
                    /* Reduce bias at higher SSAA for HD/UHD: strong SSAA needs less artificial bias.
                     * For SD (disp_h <= 576) keep bias stronger to avoid eaten-out strokes. */
                    if (disp_h > 576) bias *= (3.0 / (double)ss);
                    if (bias > 0.92) bias = 0.92;
                    if (disp_h <= 576) {
                        if (bias < 0.12) bias = 0.12; /* stronger minimum bias on SD */
                    } else {
                        if (bias < 0.05) bias = 0.05; /* keep a tiny bias for very soft edges on HD/UHD */
                    }
                    /* Heavy dithering on nearly-opaque pixels can still leave
                     * noticeable speckling when there is essentially no color
                     * variation. If the display-space distance from the
                     * foreground color is tiny, snap directly to the target
                     * foreground to remove residual dark specks inside glyphs. */
                    double diff_fg2 = fabs(rd_orig - fg_disp_r) +
                                      fabs(gd_orig - fg_disp_g) +
                                      fabs(bd_orig - fg_disp_b);
                    if (diff_fg2 < 24.0) {
                        rd = fg_disp_r;
                        gd = fg_disp_g;
                        bd = fg_disp_b;
                    } else {
                        rd = rd * (1.0 - bias) + fg_disp_r * bias;
                        gd = gd * (1.0 - bias) + fg_disp_g * bias;
                        bd = bd * (1.0 - bias) + fg_disp_b * bias;
                    }
                }
            }
            /* Find nearest palette entry in display space (accounts for
             * palette entry alpha when computing visual color). */
            int idx = nearest_palette_index_display(bm.palette, 16, rd, gd, bd, a);
            bm.idxbuf[yy*w+xx] = idx;

            uint32_t p = bm.palette[idx];
            double pa = ((p >> 24) & 0xFF) / 255.0;
            /* palette stored as straight RGB + alpha; compute its display
             * premultiplied components for error computation */
            double pr = ((p >> 16) & 0xFF) * pa;
            double pg = ((p >> 8) & 0xFF) * pa;
            double pb = (p & 0xFF) * pa;

            double err_r = rd - pr;
            double err_g = gd - pg;
            double err_b = bd - pb;

            if (skip_diffuse) {
                err_r = err_g = err_b = 0.0;
                err_r_cur[xx+1] = err_g_cur[xx+1] = err_b_cur[xx+1] = 0.0;
            }

            /* Distribute errors: FS weights: right 7/16, down-left 3/16, down 5/16, down-right 1/16 */
            if (!skip_diffuse) {
                if (xx + 1 < w) {
                    err_r_cur[xx+2] += err_r * (7.0/16.0);
                    err_g_cur[xx+2] += err_g * (7.0/16.0);
                    err_b_cur[xx+2] += err_b * (7.0/16.0);
                }
                if (xx - 1 >= 0) {
                    err_r_next[xx] += err_r * (3.0/16.0);
                    err_g_next[xx] += err_g * (3.0/16.0);
                    err_b_next[xx] += err_b * (3.0/16.0);
                }
                err_r_next[xx+1] += err_r * (5.0/16.0);
                err_g_next[xx+1] += err_g * (5.0/16.0);
                err_b_next[xx+1] += err_b * (5.0/16.0);
                if (xx + 1 < w) {
                    err_r_next[xx+2] += err_r * (1.0/16.0);
                    err_g_next[xx+2] += err_g * (1.0/16.0);
                    err_b_next[xx+2] += err_b * (1.0/16.0);
                }
            }
        }

        /* swap current and next error buffers */
        double *tmp;
        tmp = err_r_cur; err_r_cur = err_r_next; err_r_next = tmp;
        tmp = err_g_cur; err_g_cur = err_g_next; err_g_next = tmp;
        tmp = err_b_cur; err_b_cur = err_b_next; err_b_next = tmp;
        if (bg_pixel_count > 0) {
            LOG(3, "DEBUG: Row %d had %d background pixels\n", yy, bg_pixel_count);
        }
    }

    free(err_r_cur); free(err_g_cur); free(err_b_cur);
    free(err_r_next); free(err_g_next); free(err_b_next);

    /* Fill background: After quantization, any pixels still at index 0 (transparent)
     * should show the background color if bgcolor was specified. */
    if (bgcolor && bm.idxbuf) {
        LOG(3, "DEBUG: Filling background color (index %d) for transparent pixels\n", bg_palette_idx);
        int bg_filled = 0;
        for (int i = 0; i < w * h; i++) {
            if (bm.idxbuf[i] == 0) {
                bm.idxbuf[i] = bg_palette_idx;
                bg_filled++;
            }
        }
        LOG(3, "DEBUG: Filled %d pixels with background index %d\n", bg_filled, bg_palette_idx);
    }

    /* Post-dither neighbor-majority cleanup (two-pass):
     * 1) Conservative 4-neighbor pass: remove isolated dark pixels surrounded
     *    by bright neighbors (handles single-pixel speckles).
     * 2) Wider 8-neighbor pass: remove small linear runs/blocks (2-3 px)
     *    by replacing dark pixels that are in a bright neighborhood.
     * Both passes pick the brightest neighbor as replacement to avoid
     * biasing toward mid-brightness colors.
     */
    if (w > 4 && h > 4 && bm.idxbuf && bm.palette) {
        size_t wh = 0;
        if (!mul_size_ok((size_t)w, (size_t)h, &wh)) wh = SIZE_MAX;
        uint8_t *clean_idx = NULL;
        size_t area = 0;
        if (mul_size_ok((size_t)w, (size_t)h, &area) && area <= RENDER_PANGO_SAFE_MAX_PIXELS) {
            clean_idx = (uint8_t*)safe_malloc_count(area, 1);
        }
        if (clean_idx) {
            /* Pass 1: 4-neighbor conservative cleanup */
            memcpy(clean_idx, bm.idxbuf, (size_t)w * (size_t)h);
            for (int y = 1; y < h - 1; y++) {
                for (int x = 1; x < w - 1; x++) {
                    int i = y * w + x;
                    int idx = bm.idxbuf[i];
                    uint32_t palc = bm.palette[idx];
                    uint8_t pr = (palc >> 16) & 0xff;
                    uint8_t pg = (palc >> 8) & 0xff;
                    uint8_t pb = palc & 0xff;
                    double luma = 0.2126 * pr + 0.7152 * pg + 0.0722 * pb;
                    if (luma < 140.0) {
                        int bright_count = 0;
                        int neighbor_idx4[4] = { i - w, i + w, i - 1, i + 1 };
                        for (int n = 0; n < 4; n++) {
                            int nidx = neighbor_idx4[n];
                            if (nidx < 0 || (size_t)nidx >= wh) continue;
                            uint32_t npal = bm.palette[bm.idxbuf[nidx]];
                            uint8_t nr = (npal >> 16) & 0xff;
                            uint8_t ng = (npal >> 8) & 0xff;
                            uint8_t nb = npal & 0xff;
                            double nl = 0.2126 * nr + 0.7152 * ng + 0.0722 * nb;
                            if (nl >= 180.0) bright_count++;
                        }
                        if (bright_count >= 3) {
                            double best_l = -1.0;
                            int best_idx = idx;
                            for (int n = 0; n < 4; n++) {
                                int nidx = neighbor_idx4[n];
                                if (nidx < 0 || (size_t)nidx >= wh) continue;
                                int ni = bm.idxbuf[nidx];
                                uint32_t npal = bm.palette[ni];
                                uint8_t nr = (npal >> 16) & 0xff;
                                uint8_t ng = (npal >> 8) & 0xff;
                                uint8_t nb = npal & 0xff;
                                double nl = 0.2126 * nr + 0.7152 * ng + 0.0722 * nb;
                                if (nl > best_l) { best_l = nl; best_idx = ni; }
                            }
                            clean_idx[i] = best_idx;
                        }
                    }
                }
            }
            /* Commit first-pass results */
            memcpy(bm.idxbuf, clean_idx, (size_t)w * (size_t)h);

            /* Pass 2: 8-neighbor wider cleanup to catch small linear runs/blocks */
            for (int y = 1; y < h - 1; y++) {
                for (int x = 1; x < w - 1; x++) {
                    int i = y * w + x;
                    int idx = bm.idxbuf[i];
                    uint32_t palc = bm.palette[idx];
                    uint8_t pr = (palc >> 16) & 0xff;
                    uint8_t pg = (palc >> 8) & 0xff;
                    uint8_t pb = palc & 0xff;
                    double luma = 0.2126 * pr + 0.7152 * pg + 0.0722 * pb;
                    if (luma < 150.0) {
                        int bright_count = 0;
                        int neighbor_idx8[8] = {
                            i - w - 1, i - w, i - w + 1,
                            i - 1,           i + 1,
                            i + w - 1, i + w, i + w + 1
                        };
                        for (int n = 0; n < 8; n++) {
                            int nidx = neighbor_idx8[n];
                            if (nidx < 0 || (size_t)nidx >= wh) continue;
                            uint32_t npal = bm.palette[bm.idxbuf[nidx]];
                            uint8_t nr = (npal >> 16) & 0xff;
                            uint8_t ng = (npal >> 8) & 0xff;
                            uint8_t nb = npal & 0xff;
                            double nl = 0.2126 * nr + 0.7152 * ng + 0.0722 * nb;
                            if (nl >= 185.0) bright_count++;
                        }
                        /* require a strong bright majority to avoid eroding strokes */
                        if (bright_count >= 5) {
                            double best_l = -1.0;
                            int best_idx = idx;
                            for (int n = 0; n < 8; n++) {
                                int nidx = neighbor_idx8[n];
                                if (nidx < 0 || (size_t)nidx >= wh) continue;
                                int ni = bm.idxbuf[nidx];
                                uint32_t npal = bm.palette[ni];
                                uint8_t nr = (npal >> 16) & 0xff;
                                uint8_t ng = (npal >> 8) & 0xff;
                                uint8_t nb = npal & 0xff;
                                double nl = 0.2126 * nr + 0.7152 * ng + 0.0722 * nb;
                                if (nl > best_l) { best_l = nl; best_idx = ni; }
                            }
                            clean_idx[i] = best_idx;
                        }
                    }
                }
            }

            /* Pass 3: directional-run cleanup to remove short dark linear runs
             * on horizontal/vertical straight edges. This searches for dark
             * runs of length 2..4 where both ends have bright pixels and
             * replaces the run with the brightest surrounding neighbor. This
             * helps remove blocky runs along straight strokes while being
             * conservative to preserve genuine small features. */
            for (int y = 1; y < h - 1; y++) {
                for (int x = 1; x < w - 1; x++) {
                    int i = y * w + x;
                    int idx = bm.idxbuf[i];
                    uint32_t palc = bm.palette[idx];
                    uint8_t pr = (palc >> 16) & 0xff;
                    uint8_t pg = (palc >> 8) & 0xff;
                    uint8_t pb = palc & 0xff;
                    double luma = 0.2126 * pr + 0.7152 * pg + 0.0722 * pb;
                    if (luma < 160.0) {
                        /* check horizontal runs */
                        for (int run = 2; run <= 6; run++) {
                            bool is_run = true;
                            if (x + run - 1 >= w - 1) { is_run = false; break; }
                            for (int k = 0; k < run; k++) {
                                int ii = y * w + (x + k);
                                uint32_t pc = bm.palette[bm.idxbuf[ii]];
                                uint8_t rr = (pc >> 16) & 0xff;
                                uint8_t gg = (pc >> 8) & 0xff;
                                uint8_t bb = pc & 0xff;
                                double ll = 0.2126 * rr + 0.7152 * gg + 0.0722 * bb;
                                if (ll >= 175.0) { is_run = false; break; }
                            }
                            if (!is_run) continue;
                            /* require bright pixels on both ends */
                            uint32_t leftp = bm.palette[bm.idxbuf[y*w + (x - 1)]];
                            uint32_t rightp = bm.palette[bm.idxbuf[y*w + (x + run)]];
                            double ll = 0.2126 * ((leftp >> 16) & 0xff) + 0.7152 * ((leftp >> 8) & 0xff) + 0.0722 * (leftp & 0xff);
                            double lr = 0.2126 * ((rightp >> 16) & 0xff) + 0.7152 * ((rightp >> 8) & 0xff) + 0.0722 * (rightp & 0xff);
                            if (ll >= 180.0 && lr >= 180.0) {
                                /* blend surrounding neighbors to create a smooth fill color */
                                int sum_r = 0, sum_g = 0, sum_b = 0, sum_w = 0;
                                int candidates[8] = { y*w + (x - 1), y*w + (x + run), (y-1)*w + x, (y+1)*w + x, (y-1)*w + (x + run - 1), (y+1)*w + (x + run - 1), y*w + (x - 2 >= 0 ? x - 2 : x - 1), y*w + (x + run + 1 < w ? x + run + 1 : x + run) };
                                for (int c = 0; c < 8; c++) {
                                    int ci = candidates[c];
                                    if (ci < 0 || ci >= w*h) continue;
                                    uint32_t cp = bm.palette[bm.idxbuf[ci]];
                                    int cr = (cp >> 16) & 0xff; int cg = (cp >> 8) & 0xff; int cb = cp & 0xff;
                                    int wgt = (c == 0 || c == 1) ? 4 : 1;
                                    sum_r += cr * wgt; sum_g += cg * wgt; sum_b += cb * wgt; sum_w += wgt;
                                }
                                if (sum_w > 0) {
                                    uint8_t nr = sum_r / sum_w; uint8_t ng = sum_g / sum_w; uint8_t nb = sum_b / sum_w;
                                    uint8_t na = (bm.palette[bm.idxbuf[y*w + (x - 1)]] >> 24) & 0xff;
                                    int new_idx = nearest_palette_index_display(bm.palette, 16, nr, ng, nb, na);
                                    for (int k = 0; k < run; k++) bm.idxbuf[y*w + (x + k)] = new_idx;
                                }
                            }
                        }
                        /* check vertical runs */
                        for (int run = 2; run <= 6; run++) {
                            bool is_run = true;
                            if (y + run - 1 >= h - 1) { is_run = false; break; }
                            for (int k = 0; k < run; k++) {
                                int ii = (y + k) * w + x;
                                uint32_t pc = bm.palette[bm.idxbuf[ii]];
                                uint8_t rr = (pc >> 16) & 0xff;
                                uint8_t gg = (pc >> 8) & 0xff;
                                uint8_t bb = pc & 0xff;
                                double ll = 0.2126 * rr + 0.7152 * gg + 0.0722 * bb;
                                if (ll >= 175.0) { is_run = false; break; }
                            }
                            if (!is_run) continue;
                            uint32_t top = bm.palette[bm.idxbuf[(y - 1)*w + x]];
                            uint32_t bot = bm.palette[bm.idxbuf[(y + run)*w + x]];
                            double lt = 0.2126 * ((top >> 16) & 0xff) + 0.7152 * ((top >> 8) & 0xff) + 0.0722 * (top & 0xff);
                            double lb2 = 0.2126 * ((bot >> 16) & 0xff) + 0.7152 * ((bot >> 8) & 0xff) + 0.0722 * (bot & 0xff);
                            if (lt >= 180.0 && lb2 >= 180.0) {
                                int sum_r = 0, sum_g = 0, sum_b = 0, sum_w = 0;
                                int candidates[8] = { (y - 1)*w + x, (y + run)*w + x, y*w + (x-1), y*w + (x+1), (y + run - 1)*w + (x-1), (y + run - 1)*w + (x+1), (y - 2 >=0 ? (y-2)*w + x : (y-1)*w + x), (y + run + 1 < h ? (y + run + 1)*w + x : (y + run)*w + x) };
                                for (int c = 0; c < 8; c++) {
                                    int ci = candidates[c];
                                    if (ci < 0 || ci >= w*h) continue;
                                    uint32_t cp = bm.palette[bm.idxbuf[ci]];
                                    int cr = (cp >> 16) & 0xff; int cg = (cp >> 8) & 0xff; int cb = cp & 0xff;
                                    int wgt = (c == 0 || c == 1) ? 4 : 1;
                                    sum_r += cr * wgt; sum_g += cg * wgt; sum_b += cb * wgt; sum_w += wgt;
                                }
                                if (sum_w > 0) {
                                    uint8_t nr = sum_r / sum_w; uint8_t ng = sum_g / sum_w; uint8_t nb = sum_b / sum_w;
                                    uint8_t na = (bm.palette[bm.idxbuf[(y - 1)*w + x]] >> 24) & 0xff;
                                    int new_idx = nearest_palette_index_display(bm.palette, 16, nr, ng, nb, na);
                                    for (int k = 0; k < run; k++) bm.idxbuf[(y + k)*w + x] = new_idx;
                                }
                            }
                        }
                    }
                }
            }

            /* Write back final cleaned indices */
            memcpy(bm.idxbuf, clean_idx, (size_t)w * (size_t)h);
            free(clean_idx);
        }
    }

    /* Mark successful completion only if we reach here by falling through
     * (goto-based early exits jump directly to the label below and will not
     * execute this assignment). */
    success = true;
final_cleanup:
    if (success) {
        bm.x = text_x - pad;
        bm.y = text_y - pad;
        bm.w = w;
        bm.h = h;
    } else {
        /* Free any partially-allocated bitmap buffers to avoid leaking on failure */
        free_bitmap_buffers(&bm);
    }

    /* Cleanup Pango/Cairo resources (guarded) */
    if (desc) pango_font_description_free(desc);
    if (layout_dummy) g_object_unref(layout_dummy);
    if (ctx_dummy) g_object_unref(ctx_dummy);
    if (cr_dummy) cairo_destroy(cr_dummy);
    if (dummy) cairo_surface_destroy(dummy);
    if (layout_real) g_object_unref(layout_real);
    if (ctx_real) g_object_unref(ctx_real);
    if (cr) cairo_destroy(cr);
    if (surface_ss) cairo_surface_destroy(surface_ss);
    if (surface) cairo_surface_destroy(surface);

    return bm;
}

void parse_hex_color(const char *hex, double *r, double *g, double *b, double *a) {
    /*
     * Parses hex color strings in two formats:
     *   #RRGGBB     - 6-char format (RGB with opaque alpha=1.0)
     *   #AARRGGBB   - 8-char format (Alpha + RGB, where AA is alpha from 00-FF)
     * 
     * Examples:
     *   #FFFFFF     = opaque white
     *   #000000     = opaque black
     *   #FF000000   = opaque black (AA=FF, RGB=000000)
     *   #00000000   = fully transparent black (AA=00, RGB=000000)
     *   #80FF0000   = semi-transparent red (AA=80, RGB=FF0000)
     */
    if (!hex || hex[0] != '#') { *r=*g=*b=1.0; *a=1.0; return; }
    unsigned int rr=255, gg=255, bb=255, aa=255;
    size_t hlen = strlen(hex);
    if (hlen==7) {
        /* #RRGGBB - 6 chars + # = 7 total */
        if (sscanf(hex+1, "%02x%02x%02x", &rr, &gg, &bb) != 3) {
            /* malformed; fall back to white */
            rr = gg = bb = 255;
        }
    } else if (hlen==9) {
        /* #AARRGGBB - 8 chars + # = 9 total */
        unsigned int aa_t=255, rr_t=255, gg_t=255, bb_t=255;
        if (sscanf(hex+1, "%02x%02x%02x%02x", &aa_t, &rr_t, &gg_t, &bb_t) != 4) {
            aa_t = rr_t = gg_t = bb_t = 255;
        }
        aa = aa_t; rr = rr_t; gg = gg_t; bb = bb_t;
    }
    *r=rr/255.0; *g=gg/255.0; *b=bb/255.0; *a=aa/255.0;
}

void parse_bgcolor(const char *hex, double *r, double *g, double *b, double *a) {
    /*
     * Parses background color hex strings in #RRGGBB format only (6 hex digits).
     * Background color is always opaque (alpha=1.0).
     * 
     * Examples:
     *   #FFFFFF     = opaque white
     *   #000000     = opaque black
     *   #FF0000     = opaque red
     */
    if (!hex || hex[0] != '#') { *r=*g=*b=1.0; *a=1.0; return; }
    unsigned int rr=255, gg=255, bb=255;
    size_t hlen = strlen(hex);
    if (hlen==7) {
        /* #RRGGBB - 6 chars + # = 7 total */
        if (sscanf(hex+1, "%02x%02x%02x", &rr, &gg, &bb) != 3) {
            /* malformed; fall back to white */
            rr = gg = bb = 255;
        } else {
            LOG(3, "DEBUG parse_bgcolor: sscanf returned rr=%u gg=%u bb=%u from string '%s'\n", rr, gg, bb, hex);
        }
    } else {
        /* Invalid length; fall back to white and log warning if debugging */
        LOG(3, "DEBUG parse_bgcolor: invalid length %zu, expected 7 for hex='%s'\n", hlen, hex);
        rr = gg = bb = 255;
    }
    *r=rr/255.0; *g=gg/255.0; *b=bb/255.0; *a=1.0;  /* alpha always 1.0 (opaque) */
    LOG(3, "DEBUG parse_bgcolor: returning r=%f g=%f b=%f a=%f\n", *r, *g, *b, *a);
}


/*
 * Check if a font family exists on the system using Fontconfig.
 */
int font_exists(const char *font_name) {
    if (!font_name) return 0;
    
    PangoFontMap *fontmap = pango_cairo_font_map_get_default();
    if (!fontmap) return 0;
    
    PangoContext *ctx = pango_font_map_create_context(fontmap);
    if (!ctx) return 0;
    
    PangoFontDescription *desc = pango_font_description_from_string(font_name);
    if (!desc) {
        g_object_unref(ctx);
        return 0;
    }
    
    PangoFont *font = pango_context_load_font(ctx, desc);
    int exists = (font != NULL) ? 1 : 0;
    
    if (font) g_object_unref(font);
    pango_font_description_free(desc);
    g_object_unref(ctx);
    
    return exists;
}

/*
 * Check if a specific font style exists for a font family.
 */
int font_style_exists(const char *font_name, const char *style_name) {
    if (!font_name || !style_name) return 0;
    
    /* Create full font description with style */
    char full_font[512];
    snprintf(full_font, sizeof(full_font), "%s %s", font_name, style_name);
    
    return font_exists(full_font);
}

/*
 * Validate and resolve font family and style.
 *
 * Priority for font selection:
 * 1. If user specified --font and it exists, use it
 * 2. If user specified --font but it doesn't exist, try preferred defaults
 * 3. If no user font, try preferred defaults
 * 4. If all fail, error out
 *
 * For style:
 * 1. If user specified --font-style and it exists for the font, use it
 * 2. Try fallbacks: "Light", "Thin", "Medium", "Regula"Medium"r"
 * 3. Default to NULL (use Pango default)
 */
int validate_and_resolve_font(const char *user_font, const char *user_style,
                               char **out_font, char **out_style) {
    extern int debug_level;
    
    const char *preferred_fonts[] = {"Open Sans", "Roboto", "DejaVu Sans", NULL};
    const char *fallback_styles[] = {"Light", "Thin", "Medium", "Regular", NULL};
    
    const char *resolved_font = NULL;
    const char *resolved_style = NULL;
    
    /* Try to resolve font */
    if (user_font) {
        if (font_exists(user_font)) {
            resolved_font = user_font;
        } else {
            if (debug_level > 0) {
                LOG(1, "Font '%s' not found on system. Trying preferred fonts...\n", user_font);
            }
            /* Fall back to preferred fonts */
            for (int i = 0; preferred_fonts[i]; i++) {
                if (font_exists(preferred_fonts[i])) {
                    resolved_font = preferred_fonts[i];
                    LOG(1, "Using fallback font: %s\n", resolved_font);
                    break;
                }
            }
        }
    } else {
        /* No user font specified, try preferred fonts */
        for (int i = 0; preferred_fonts[i]; i++) {
            if (font_exists(preferred_fonts[i])) {
                resolved_font = preferred_fonts[i];
                break;
            }
        }
    }
    
    /* If no font found, error out */
    if (!resolved_font) {
        LOG(0, "ERROR: No suitable font found on system.\n");
        LOG(0, "Please install one of the following fonts:\n");
        for (int i = 0; preferred_fonts[i]; i++) {
            LOG(0, "  - %s\n", preferred_fonts[i]);
        }
        return -1;
    }
    
    /* Try to resolve style */
    if (user_style) {
        if (font_style_exists(resolved_font, user_style)) {
            resolved_style = user_style;
        } else {
            if (debug_level > 0) {
                LOG(1, "Font style '%s' not found for font '%s'. Trying fallbacks...\n", 
                    user_style, resolved_font);
            }
            /* Try fallback styles */
            for (int i = 0; fallback_styles[i]; i++) {
                if (font_style_exists(resolved_font, fallback_styles[i])) {
                    resolved_style = fallback_styles[i];
                    if (debug_level > 0) {
                        LOG(1, "Using fallback style: %s\n", resolved_style);
                    }
                    break;
                }
            }
        }
    } else {
        /* No user style specified, try preferred defaults */
        for (int i = 0; fallback_styles[i]; i++) {
            if (font_style_exists(resolved_font, fallback_styles[i])) {
                resolved_style = fallback_styles[i];
                break;
            }
        }
    }
    
    /* Allocate output strings */
    if (resolved_font) {
        *out_font = strdup(resolved_font);
        if (!*out_font) {
            LOG(0, "Out of memory allocating font name\n");
            return -1;
        }
    } else {
        *out_font = NULL;
    }
    
    if (resolved_style) {
        *out_style = strdup(resolved_style);
        if (!*out_style) {
            LOG(0, "Out of memory allocating font style\n");
            free(*out_font);
            *out_font = NULL;
            return -1;
        }
    } else {
        *out_style = NULL;
    }
    
    if (debug_level > 0) {
        LOG(1, "Resolved font: %s %s\n", *out_font, *out_style ? *out_style : "(default style)");
    }
    
    return 0;
}

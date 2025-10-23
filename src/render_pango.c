#define _POSIX_C_SOURCE 200809L
#include "render_pango.h"
#include "palette.h"
#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <limits.h>
#include <stdbool.h>
#include <math.h>

/* Use a thread-local PangoFontMap to avoid concurrent internal Pango
 * mutations when rendering from multiple worker threads. We create a
 * pthread key with a destructor that unrefs the fontmap on thread exit.
 */
#include <pthread.h>

static pthread_key_t pango_fontmap_key;
static pthread_once_t pango_fontmap_key_once = PTHREAD_ONCE_INIT;

static void pango_fontmap_destructor(void *v) {
    if (!v) return;
    g_object_unref((GObject*)v);
}

static void make_pango_fontmap_key(void) {
    pthread_key_create(&pango_fontmap_key, pango_fontmap_destructor);
}

/* Return a PangoFontMap for the calling thread, creating one if needed.
 * The map is stored in thread-specific storage and will be cleaned up
 * automatically when the thread exits. */
static PangoFontMap *get_thread_pango_fontmap(void) {
    pthread_once(&pango_fontmap_key_once, make_pango_fontmap_key);
    PangoFontMap *fm = (PangoFontMap *)pthread_getspecific(pango_fontmap_key);
    if (!fm) {
        fm = pango_cairo_font_map_new();
        pthread_setspecific(pango_fontmap_key, fm);
    }
    return fm;
}

/* Public cleanup callable by main to deterministically release this
 * thread's Pango/fontmap resources before calling FcFini() (fontconfig
 * finalizer). This will only affect the current thread (usually main).
 */
void render_pango_cleanup(void) {
    pthread_once(&pango_fontmap_key_once, make_pango_fontmap_key);
    PangoFontMap *fm = (PangoFontMap *)pthread_getspecific(pango_fontmap_key);
    if (fm) {
        pthread_setspecific(pango_fontmap_key, NULL);
        g_object_unref((GObject*)fm);
    }
}

/* runtime knobs */
static int g_ssaa_override = 0;
static int g_no_unsharp = 0;
void render_pango_set_ssaa_override(int ssaa) { g_ssaa_override = ssaa; }
void render_pango_set_no_unsharp(int no) { g_no_unsharp = no; }

// Palette presets
void init_palette(uint32_t *pal,const char *mode) {
    if (!pal) return;

    if (mode && strcasecmp(mode,"greyscale")==0) {
        // Smooth greyscale from transparent to white
        pal[0] = 0x00000000; // transparent
        for (int i=1; i<16; i++) {
            int v = (i-1)*17;
            pal[i] = (0xFF<<24) | (v<<16) | (v<<8) | v;
        }

    } else if (mode && strcasecmp(mode,"broadcast")==0) {
        // Broadcast palette with additional half-bright entries (helps anti-aliased edges)
        pal[0]=0x00000000; pal[1]=0xFFFFFFFF; pal[2]=0xFFFFFF00;
        pal[3]=0xFF00FFFF; pal[4]=0xFF00FF00; pal[5]=0xFFFF00FF;
        pal[6]=0xFFFF0000; pal[7]=0xFF0000FF; pal[8]=0xFF000000;
        /* half-bright variants (opaque but half RGB) to provide intermediate
        * luminance values for anti-aliased edges without introducing
        * semi-transparent compositing halos. Use full alpha (0xFF) and
        * halve RGB channels. */
        pal[9]  = 0xFF7F7F7F; // half white
        pal[10] = 0xFF7F7F00; // half yellow
        pal[11] = 0xFF007F7F; // half cyan
        pal[12] = 0xFF007F00; // half green
        pal[13] = 0xFF7F007F; // half magenta
        pal[14] = 0xFF7F0000; // half red
        pal[15] = 0xFF00007F; // half blue

    } else if (mode && strcasecmp(mode,"ebu-broadcast")==0) {
        // Full EBU 16-color CLUT (full + half brightness)
        pal[0]  = 0x00000000; // transparent
        pal[1]  = 0xFFFFFFFF; // white
        pal[2]  = 0xFFFFFF00; // yellow
        pal[3]  = 0xFF00FFFF; // cyan
        pal[4]  = 0xFF00FF00; // green
        pal[5]  = 0xFFFF00FF; // magenta
        pal[6]  = 0xFFFF0000; // red
        pal[7]  = 0xFF0000FF; // blue
        pal[8]  = 0xFF000000; // black
        /* half-bright variants (opaque half-RGB) rather than semi-transparent
        * entries: helps anti-aliased edges map to intermediate visible
        * colors without compositing dark halos. */
        pal[9]  = 0xFF7F7F7F; // half white
        pal[10] = 0xFF7F7F00; // half yellow
        pal[11] = 0xFF007F7F; // half cyan
        pal[12] = 0xFF007F00; // half green
        pal[13] = 0xFF7F007F; // half magenta
        pal[14] = 0xFF7F0000; // half red
        pal[15] = 0xFF00007F; // half blue

    } else {
        // Default: same as simple broadcast
        pal[0]=0x00000000; pal[1]=0xFFFFFFFF; pal[2]=0xFFFFFF00;
        pal[3]=0xFF00FFFF; pal[4]=0xFF00FF00; pal[5]=0xFFFF00FF;
        pal[6]=0xFFFF0000; pal[7]=0xFF000000;
        for(int i=8;i<16;i++) pal[i]=0x00000000;
    }
}

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

/* Choose nearest palette index by comparing display (premultiplied) RGB
 * values. Both source and palette entries are converted to their effective
 * displayed color over black: channel_display = channel * (alpha/255). */
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

char* srt_to_pango_markup(const char *srt_text) {
    if (!srt_text) return strdup("");
    size_t maxlen = strlen(srt_text)*4+1;
    char *buf=(char*)malloc(maxlen); if(!buf) return strdup("");
    char *out=buf; const char *p=srt_text;
    while(*p && (size_t)(out-buf) < maxlen-10){
        if (strncmp(p,"<i>",3)==0 || strncmp(p,"</i>",4)==0 ||
            strncmp(p,"<b>",3)==0 || strncmp(p,"</b>",4)==0 ||
            strncmp(p,"<u>",3)==0 || strncmp(p,"</u>",4)==0) {
            int len=(*p=='<'&&*(p+1)=='/')?4:3;
            strncpy(out,p,len); out+=len; p+=len;
        }
       else if (strncasecmp(p,"<font ",6)==0) {
            const char *end = strchr(p, '>');
            if (end) {
                char tmp[256] = {0};
                strncpy(tmp, p, end - p + 1);
                char color[64] = "";
                if (sscanf(tmp, "<font color=\"%63[^\"]", color) == 1) {
                    out += sprintf(out, "<span foreground=\"%s\">", color);
                    p = end + 1;
                    continue;
                }
            }
            // If we didn’t match, just skip escaping
            *out++ = *p++;
        }
        else if (strncasecmp(p,"</font>",7)==0) {
            strcpy(out, "</span>");
            out += 7;
            p += 7;
        }
        else if (strncasecmp(p,"</font>",7)==0) {
            strcpy(out,"</span>"); out+=7; p+=7;
        }
        else {
            if(*p=='&'){ strcpy(out,"&amp;"); out+=5; }
            else if(*p=='<'){ strcpy(out,"&lt;"); out+=4; }
            else if(*p=='>'){ strcpy(out,"&gt;"); out+=4; }
            else { *out++=*p; }
            p++;
        }
    }
    *out='\0'; return buf;
}

Bitmap render_text_pango(const char *markup,
                          int disp_w, int disp_h,
                          int fontsize, const char *fontfam,
                          const char *fgcolor,
                          const char *outlinecolor,
                          const char *shadowcolor,
                          int align_code,
                          const char *palette_mode) {
    Bitmap bm={0};

    /* Ensure we have a single process-wide PangoFontMap to avoid creating
     * multiple internal fontconfig allocations across repeated renders.
     * We create contexts from this map per-render and unref them. */
    /* Ensure a per-thread fontmap exists (created on first use). */
    PangoFontMap *thread_fm = get_thread_pango_fontmap();

    // --- Font size selection ---
    // If caller passed a positive fontsize (CLI --fontsize), respect it.
    // Otherwise compute a dynamic font size based on display height using
    // targeted ranges for SD, HD and UHD:
    //  SD:  ~18..22
    //  HD:  ~40..48
    //  UHD: ~80..88
    bool add_bg = false;
    if (fontsize > 0) {
        /* respect caller-provided fontsize (no upper clamp) */
    } else {
        int f = 18;
        if (disp_h <= 576) {
            /* SD band: interpolate 18..22 over 0..576 */
            double t = (double)disp_h / 576.0;
            if (t < 0.0) t = 0.0; if (t > 1.0) t = 1.0;
            double v = 18.0 + t * (22.0 - 18.0);
            f = (int)round(v);
        } else if (disp_h <= 1080) {
            /* HD band: interpolate 36..42 over 577..1080 so 1080 -> ~42 */
            double t = ((double)disp_h - 576.0) / (1080.0 - 576.0);
            if (t < 0.0) t = 0.0; if (t > 1.0) t = 1.0;
            double v = 36.0 + t * (42.0 - 36.0);
            f = (int)round(v);
        } else {
            /* UHD and larger: interpolate 82..88 over 1081..4320 so 2160 -> ~84 */
            double t = ((double)disp_h - 1080.0) / (4320.0 - 1080.0);
            if (t < 0.0) t = 0.0; if (t > 1.0) t = 1.0;
            double v = 82.0 + t * (88.0 - 82.0);
            f = (int)round(v);
        }
        fontsize = f;
    }

    // Create common font description
    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, fontfam ? fontfam : "Lato");
    //pango_font_description_set_weight(desc, PANGO_WEIGHT_HEAVY);
    pango_font_description_set_absolute_size(desc, fontsize * PANGO_SCALE);

    // --- Dummy layout for measurement ---
    cairo_surface_t *dummy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr_dummy = cairo_create(dummy);
    PangoContext *ctx_dummy = pango_font_map_create_context(thread_fm);
    PangoLayout *layout_dummy = pango_layout_new(ctx_dummy);
    pango_layout_set_font_description(layout_dummy, desc);
    pango_layout_set_width(layout_dummy, disp_w * 0.8 * PANGO_SCALE);
    pango_layout_set_wrap(layout_dummy, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_markup(layout_dummy, markup, -1);

    int lw, lh;
    pango_layout_get_pixel_size(layout_dummy, &lw, &lh);

    // Debug resolved font
   

    // --- Placement in full frame ---
    int margin_y = disp_h * 0.038;
    int text_x = (disp_w - lw) / 2;
    int text_y = disp_h - margin_y - lh;
    if (align_code >= 7) text_y = margin_y;
    else if (align_code >= 4 && align_code <= 6) text_y = (disp_h - lh) / 2;

    // --- Adaptive supersampled rendering surface ---
    // Choose supersample factor based on display height.
    // SD gets a higher SSAA to avoid blockiness. For HD/UHD we also
    // increase SSAA to keep glyph edges smooth at larger sizes.
    // These choices balance quality vs CPU/memory; users can override
    // with the ssaa_override runtime knob.
    int ss;
    if (disp_h <= 576) {
        ss = 3; // SD: strong supersample to avoid crystalized edges
    } else if (disp_h <= 1080) {
        ss = 3; // HD: bump to 3x to improve edge fidelity compared to previous 2x
    } else if (disp_h <= 2160) {
        ss = 4; // 4k/2160p target: use 4x for best quality on UHD
    } else {
        ss = 4; // very large displays: cap at 4x
    }
    if (g_ssaa_override > 0) ss = g_ssaa_override;
    /* pad to accommodate strokes when upscaled; scale with fontsize so
     * UHD/large text get enough room and strokes don't clip. */
    int pad = 8;
    if (fontsize > 48) pad = (int)ceil(fontsize * 0.25);
    int ss_w = (lw + 2*pad) * ss;
    int ss_h = (lh + 2*pad) * ss;
    cairo_surface_t *surface_ss = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ss_w, ss_h);
    cairo_t *cr = cairo_create(surface_ss);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    cairo_scale(cr, (double)ss, (double)ss);  // draw at larger resolution then downscale

    PangoContext *ctx_real = pango_font_map_create_context(thread_fm);
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
    PangoLayout *layout_real = pango_layout_new(ctx_real);
    pango_layout_set_font_description(layout_real, desc);
    pango_layout_set_width(layout_real, disp_w * 0.8 * PANGO_SCALE);
    pango_layout_set_wrap(layout_real, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_markup(layout_real, markup, -1);

    // Background
    if (add_bg) {
        cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
        cairo_rectangle(cr, 0, 0, lw+2*pad, lh+2*pad);
        cairo_fill(cr);
    }

    double fr,fg,fb,fa, or_,og,ob,oa, sr,sg,sb,sa;
    parse_hex_color(fgcolor, &fr,&fg,&fb,&fa);
    parse_hex_color(outlinecolor, &or_,&og,&ob,&oa);
    parse_hex_color(shadowcolor, &sr,&sg,&sb,&sa);

    // Shadow first: make the offset proportional to supersample / fontsize
    if (shadowcolor) {
        double shadow_off = (double)ss * (fontsize * 0.04);
        if (shadow_off < ss) shadow_off = ss; // at least ss pixels
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
    if (ss >= 4) stroke_w *= 0.70; /* thinner at 4x to compensate */
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

    // --- Downscale to target surface (1×) ---
    /* Optionally apply a mild separable blur on the supersampled surface to
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
            int ss_stride_ls = cairo_image_surface_get_stride(surface_ss);
            int sw_ls = ss_w, sh_ls = ss_h;
            /* horizontal -> tmp buffer */
            double *tmp_r = malloc((size_t)sw_ls * (size_t)sh_ls * sizeof(double));
            double *tmp_g = malloc((size_t)sw_ls * (size_t)sh_ls * sizeof(double));
            double *tmp_b = malloc((size_t)sw_ls * (size_t)sh_ls * sizeof(double));
            if (tmp_r && tmp_g && tmp_b) {
                /* convert to linear (approx sRGB->linear) and horizontal blur (7-tap separable)
                 * Use a wider kernel on HD to smooth curve stepping more aggressively.
                 * Weights chosen approximate a small Gaussian: 1,6,15,20,15,6,1 (sum=64).
                 */
                /* Slightly stronger smoothing: increase wing/near-center weights
                 * to better reduce small curved-step artifacts without blurring
                 * shape excessively. New weights: 1,8,20,24,20,8,1 (sum=82). */
                const int wts[7] = {1,8,20,24,20,8,1};
                const int wsum = 82;
                for (int y = 0; y < sh_ls; y++) {
                    for (int x = 0; x < sw_ls; x++) {
                        uint32_t px = *(uint32_t*)(ss_data_ls + y*ss_stride_ls + x*4);
                        double r = ((px >> 16) & 0xFF) / 255.0;
                        double g = ((px >> 8) & 0xFF) / 255.0;
                        double b = (px & 0xFF) / 255.0;
                        /* sRGB to linear approx (cheap piecewise inline) */
                        double lr = (r <= 0.04045) ? (r / 12.92) : pow((r + 0.055) / 1.055, 2.4);
                        double lg = (g <= 0.04045) ? (g / 12.92) : pow((g + 0.055) / 1.055, 2.4);
                        double lb = (b <= 0.04045) ? (b / 12.92) : pow((b + 0.055) / 1.055, 2.4);
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
        int ss_stride = cairo_image_surface_get_stride(surface_ss);
        int sw = ss_w, sh = ss_h;
        uint32_t *tmp = malloc(sw * sh * sizeof(uint32_t));
        uint32_t *tmp2 = NULL;
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
                tmp2 = malloc(sw * sh * sizeof(uint32_t));
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
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr_down = cairo_create(surface);
    /* shrink from ss× to 1× using the adaptive supersample factor */
    cairo_scale(cr_down, 1.0 / (double)ss, 1.0 / (double)ss);
    cairo_set_source_surface(cr_down, surface_ss, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr_down), CAIRO_FILTER_BEST);
    cairo_paint(cr_down);
    cairo_destroy(cr_down);

    unsigned char *data = cairo_image_surface_get_data(surface);

    /* Tiny unsharp mask to restore edge crispness after downsampling.
     * We use a small 3x3 box blur to get a blurred version, then add
     * amount*(orig - blur) back to the original. Operates on premultiplied
     * ARGB channels (This is acceptable for a small amount). */
    if (!g_no_unsharp) {
        /* Reduce unsharp amount for higher SSAA: when supersampling is strong
         * we need much less sharpening (or almost none) to avoid reintroducing
         * blockiness. */
    /* Disable unsharp at very high SSAA to avoid reintroducing
     * aliasing-like artifacts; reduce amount progressively as ss grows. */
    double amount = 0.6; /* default strength */
    if (ss >= 6) amount = 0.0; /* very high SSAA: no sharpening */
        else if (ss >= 4) amount = 0.30;
        else if (ss == 3) amount = 0.5;
        int sw = w, sh = h;
        int stride = cairo_image_surface_get_stride(surface);
        uint32_t *orig = malloc(sw * sh * sizeof(uint32_t));
        uint32_t *blur = malloc(sw * sh * sizeof(uint32_t));
        if (orig && blur) {
            /* copy pixels */
            for (int y = 0; y < sh; y++) {
                for (int x = 0; x < sw; x++) {
                    orig[y*sw + x] = *(uint32_t*)(data + y*stride + x*4);
                }
            }
            /* compute 3x3 box blur into blur[] */
            for (int y = 0; y < sh; y++) {
                for (int x = 0; x < sw; x++) {
                    uint64_t sa=0,sr=0,sg=0,sb=0; int cnt=0;
                    for (int dy=-1; dy<=1; dy++) {
                        int yy = y + dy; if (yy < 0 || yy >= sh) continue;
                        for (int dx=-1; dx<=1; dx++) {
                            int xx = x + dx; if (xx < 0 || xx >= sw) continue;
                            uint32_t px = orig[yy*sw + xx];
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
                    blur[y*sw + x] = ((uint32_t)na<<24) | ((uint32_t)nr<<16) | ((uint32_t)ng<<8) | (uint32_t)nb;
                }
            }
            /* apply unsharp: dest = clamp(orig + amount*(orig - blur)) */
            for (int y = 0; y < sh; y++) {
                for (int x = 0; x < sw; x++) {
                    uint32_t o = orig[y*sw + x];
                    uint32_t b = blur[y*sw + x];
                    int oa = (o >> 24) & 0xFF;
                    int orr = (o >> 16) & 0xFF; int ogr = (o >> 8) & 0xFF; int ob = o & 0xFF;
                    int ba = (b >> 24) & 0xFF;
                    int brr = (b >> 16) & 0xFF; int bgr = (b >> 8) & 0xFF; int bb = b & 0xFF;
                    int na = oa; /* keep alpha */
                    int nr = (int)round(orr + amount * (orr - brr));
                    int ng = (int)round(ogr + amount * (ogr - bgr));
                    int nbv = (int)round(ob + amount * (ob - bb));
                    if (nr < 0) nr = 0; if (nr > 255) nr = 255;
                    if (ng < 0) ng = 0; if (ng > 255) ng = 255;
                    if (nbv < 0) nbv = 0; if (nbv > 255) nbv = 255;
                    *(uint32_t*)(data + y*stride + x*4) = ((uint32_t)na<<24) | ((uint32_t)nr<<16) | ((uint32_t)ng<<8) | (uint32_t)nbv;
                }
            }
        }
        free(orig); free(blur);
    }

    /* HD edge-aware smoothing: for HD (disp_h <= 1080) and high SSAA,
     * apply a small 3x3 smoothing on semi-transparent edge pixels that
     * weights neighbors by alpha and color similarity in premultiplied
     * space. This reduces blocky, high-contrast quantization edges while
     * preserving solid strokes. */
    if (disp_h <= 1080 && ss >= 3) {
        int sw = w, sh = h;
        int stride = cairo_image_surface_get_stride(surface);
        uint32_t *tmp_edge = malloc(sw * sh * sizeof(uint32_t));
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
        uint32_t *tmp = malloc(sw * sh * sizeof(uint32_t));
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
                            int pr = (p >> 16) & 0xFF; int pg = (p >> 8) & 0xFF; int pb = p & 0xFF; int pa = (p >> 24) & 0xFF;
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
                            int pr = (p >> 16) & 0xFF; int pg = (p >> 8) & 0xFF; int pb = p & 0xFF; int pa = (p >> 24) & 0xFF;
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

    bm.idxbuf = calloc(w*h, 1);
    bm.palette = malloc(16*sizeof(uint32_t));
    init_palette(bm.palette, palette_mode);

    int stride = cairo_image_surface_get_stride(surface);

    /* precompute foreground premultiplied display components (0..255) */
    double fg_disp_r = fr * fa * 255.0;
    double fg_disp_g = fg * fa * 255.0;
    double fg_disp_b = fb * fa * 255.0;

    /* Apply Floyd–Steinberg error-diffusion dithering to reduce banding and
     * blockiness when mapping down to the limited 16-color palette. We keep
     * per-channel error buffers for the current and next row. */
    double *err_r_cur = calloc(w+2, sizeof(double));
    double *err_g_cur = calloc(w+2, sizeof(double));
    double *err_b_cur = calloc(w+2, sizeof(double));
    double *err_r_next = calloc(w+2, sizeof(double));
    double *err_g_next = calloc(w+2, sizeof(double));
    double *err_b_next = calloc(w+2, sizeof(double));

    for (int yy = 0; yy < h; yy++) {
        /* zero the next-row error buffers */
        for (int i = 0; i < w+2; i++) { err_r_next[i] = err_g_next[i] = err_b_next[i] = 0.0; }

        for (int xx = 0; xx < w; xx++) {
            uint32_t argb = *(uint32_t*)(data + yy*stride + xx*4);
            uint8_t a = (argb >> 24) & 0xFF;
                /* lower the threshold so very soft antialiased edges still
                 * pick nearby colors instead of being forced transparent. */
                if (a < 16) {
                    bm.idxbuf[yy*w+xx] = 0;
                    continue;
                }

            /* Use premultiplied/display components directly (Cairo stores
             * premultiplied ARGB). Operating in display space avoids halos
             * when compositing semi-transparent pixels into the 16-color
             * palette. Errors are propagated in display units (0..255). */
            double rd = (double)((argb >> 16) & 0xFF) + err_r_cur[xx+1];
            double gd = (double)((argb >> 8) & 0xFF) + err_g_cur[xx+1];
            double bd = (double)(argb & 0xFF) + err_b_cur[xx+1];
            if (rd < 0) rd = 0; if (rd > 255) rd = 255;
            if (gd < 0) gd = 0; if (gd > 255) gd = 255;
            if (bd < 0) bd = 0; if (bd > 255) bd = 255;
            /* Bias semi-transparent glyph edge pixels toward foreground
             * display color to reduce dark halos when quantizing to 16 colors. */
            if (a > 24 && a < 255) {
                double an = (double)a / 255.0;
                double bias = pow(an, 1.05) * 0.6; /* tuned base */
                /* Reduce bias at higher SSAA: strong SSAA needs less artificial bias */
                bias *= (3.0 / (double)ss);
                if (bias > 0.92) bias = 0.92;
                if (bias < 0.05) bias = 0.05; /* keep a tiny bias for very soft edges */
                rd = rd * (1.0 - bias) + fg_disp_r * bias;
                gd = gd * (1.0 - bias) + fg_disp_g * bias;
                bd = bd * (1.0 - bias) + fg_disp_b * bias;
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

            /* Distribute errors: FS weights: right 7/16, down-left 3/16, down 5/16, down-right 1/16 */
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

        /* swap current and next error buffers */
        double *tmp;
        tmp = err_r_cur; err_r_cur = err_r_next; err_r_next = tmp;
        tmp = err_g_cur; err_g_cur = err_g_next; err_g_next = tmp;
        tmp = err_b_cur; err_b_cur = err_b_next; err_b_next = tmp;
    }

    free(err_r_cur); free(err_g_cur); free(err_b_cur);
    free(err_r_next); free(err_g_next); free(err_b_next);

    /* Post-dither neighbor-majority cleanup (two-pass):
     * 1) Conservative 4-neighbor pass: remove isolated dark pixels surrounded
     *    by bright neighbors (handles single-pixel speckles).
     * 2) Wider 8-neighbor pass: remove small linear runs/blocks (2-3 px)
     *    by replacing dark pixels that are in a bright neighborhood.
     * Both passes pick the brightest neighbor as replacement to avoid
     * biasing toward mid-brightness colors.
     */
    if (w > 4 && h > 4 && bm.idxbuf && bm.palette) {
        uint8_t *clean_idx = malloc((size_t)w * (size_t)h);
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
                            uint32_t npal = bm.palette[bm.idxbuf[neighbor_idx4[n]]];
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
                                int ni = bm.idxbuf[neighbor_idx4[n]];
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
                            uint32_t npal = bm.palette[bm.idxbuf[neighbor_idx8[n]]];
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
                                int ni = bm.idxbuf[neighbor_idx8[n]];
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

    bm.x = text_x - pad;
    bm.y = text_y - pad;
    bm.w = w;
    bm.h = h;

    // Cleanup
    pango_font_description_free(desc);
    g_object_unref(layout_dummy);
    g_object_unref(ctx_dummy);
    cairo_destroy(cr_dummy);
    cairo_surface_destroy(dummy);
    g_object_unref(layout_real);
    g_object_unref(ctx_real);
    cairo_destroy(cr);
    cairo_surface_destroy(surface_ss);
    cairo_surface_destroy(surface);

    return bm;
}

void parse_hex_color(const char *hex, double *r, double *g, double *b, double *a) {
    if (!hex || hex[0] != '#') { *r=*g=*b=1.0; *a=1.0; return; }
    unsigned int rr=255, gg=255, bb=255, aa=255;
    if (strlen(hex)==7) {
        sscanf(hex+1,"%02x%02x%02x",&rr,&gg,&bb);
    } else if (strlen(hex)==9) {
        sscanf(hex+1,"%02x%02x%02x%02x",&rr,&gg,&bb,&aa);
    }
    *r=rr/255.0; *g=gg/255.0; *b=bb/255.0; *a=aa/255.0;
}
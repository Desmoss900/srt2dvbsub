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

/* Module-global pango fontmap to avoid repeated creation across renders */
static PangoFontMap *g_pango_fontmap = NULL;
static void pango_fontmap_cleanup(void) {
    if (g_pango_fontmap) {
        g_object_unref(g_pango_fontmap);
        g_pango_fontmap = NULL;
    }
}

/* Public cleanup callable by main to deterministically release Pango/fontmap
 * resources before calling FcFini() (fontconfig finalizer). */
void render_pango_cleanup(void) {
    pango_fontmap_cleanup();
}

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
        // Simple 8-color broadcast style
        pal[0]=0x00000000; pal[1]=0xFFFFFFFF; pal[2]=0xFFFFFF00;
        pal[3]=0xFF00FFFF; pal[4]=0xFF00FF00; pal[5]=0xFFFF00FF;
        pal[6]=0xFFFF0000; pal[7]=0xFF0000FF; pal[8]=0xFF000000;
        for(int i=9;i<16;i++) pal[i]=0x00000000;

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
        pal[9]  = 0x80FFFFFF; // half white
        pal[10] = 0x80FFFF00; // half yellow
        pal[11] = 0x8000FFFF; // half cyan
        pal[12] = 0x8000FF00; // half green
        pal[13] = 0x80FF00FF; // half magenta
        pal[14] = 0x80FF0000; // half red
        pal[15] = 0x800000FF; // half blue

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
static int nearest_palette_index_display(uint32_t *palette, int npal, double rd, double gd, double bd) {
    int best = 1;
    double bestdiff = 1e308;
    for (int i = 0; i < npal; i++) {
        uint32_t p = palette[i];
        double pa = ((p >> 24) & 0xFF) / 255.0;
        double pr = ((p >> 16) & 0xFF) * pa;
        double pg = ((p >> 8) & 0xFF) * pa;
        double pb = (p & 0xFF) * pa;
        double dr = rd - pr;
        double dg = gd - pg;
        double db = bd - pb;
        double diff = dr*dr + dg*dg + db*db;
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
    if (!g_pango_fontmap) {
        g_pango_fontmap = pango_cairo_font_map_new();
        /* Register cleanup at process exit to release the fontmap */
        atexit(pango_fontmap_cleanup);
    }

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
    PangoContext *ctx_dummy = pango_font_map_create_context(g_pango_fontmap);
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
    // Use a larger supersample factor for SD (reduce blockiness). For
    // typical SD (<=576) use 3× supersampling (good speed/quality tradeoff), otherwise 2×.
    // Increasing ss increases memory/CPU roughly with ss^2.
    int ss = (disp_h <= 576) ? 3 : 2;
    int pad = 8; // pad to accommodate strokes when upscaled
    int ss_w = (lw + 2*pad) * ss;
    int ss_h = (lh + 2*pad) * ss;
    cairo_surface_t *surface_ss = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ss_w, ss_h);
    cairo_t *cr = cairo_create(surface_ss);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    cairo_scale(cr, (double)ss, (double)ss);  // draw at larger resolution then downscale

    PangoContext *ctx_real = pango_font_map_create_context(g_pango_fontmap);
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
    double stroke_w = 0.9 + (fontsize * 0.045); // base user-space stroke width
    if (ss >= 4) stroke_w *= 0.85; // slightly thinner stroke for 4x SSAA
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
    /* Optionally apply a very mild 1-pixel box blur on the supersampled
     * surface to reduce remaining high-frequency aliasing when using
     * 3× SSAA without introducing noticeable smearing. This is cheap
     * and much less aggressive than the previous blur. */
    if (ss >= 3) {
        unsigned char *ss_data = cairo_image_surface_get_data(surface_ss);
        int ss_stride = cairo_image_surface_get_stride(surface_ss);
        int sw = ss_w, sh = ss_h;
        /* allocate temporary buffer for horizontal pass */
        uint32_t *tmp = malloc(sw * sh * sizeof(uint32_t));
        if (tmp) {
            /* simple 3x3 box blur implemented as two passes (horizontal then vertical)
             * operating on premultiplied ARGB uint32 pixels. */
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
    {
        const double amount = 0.6; /* strength of sharpening (0..1) */
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

    bm.idxbuf = calloc(w*h, 1);
    bm.palette = malloc(16*sizeof(uint32_t));
    init_palette(bm.palette, palette_mode);

    int stride = cairo_image_surface_get_stride(surface);

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
            if (a < 30) { // be slightly more permissive for anti-aliased edges
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
            /* Find nearest palette entry in display space (accounts for
             * palette entry alpha when computing visual color). */
            int idx = nearest_palette_index_display(bm.palette, 16, rd, gd, bd);
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
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

    // --- Dynamic font size ---
    int dyn_font = (int)(disp_h * 0.05); // ~5% of screen height
    bool add_bg = false;
    if (dyn_font < 18) dyn_font = 18;
    if (dyn_font > 48) dyn_font = 48;
    fontsize = dyn_font;

    // Create common font description
    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, fontfam ? fontfam : "Lato");
    //pango_font_description_set_weight(desc, PANGO_WEIGHT_HEAVY);
    pango_font_description_set_absolute_size(desc, fontsize * PANGO_SCALE);

    // --- Dummy layout for measurement ---
    cairo_surface_t *dummy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr_dummy = cairo_create(dummy);
    PangoLayout *layout_dummy = pango_cairo_create_layout(cr_dummy);
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

    // --- Supersampled rendering surface (2×) ---
    int pad = 5;
    int ss_w = (lw+2*pad) * 2;
    int ss_h = (lh+2*pad) * 2;
    cairo_surface_t *surface_ss = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ss_w, ss_h);
    cairo_t *cr = cairo_create(surface_ss);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    cairo_scale(cr, 2.0, 2.0);  // scale back down when drawing

    PangoLayout *layout_real = pango_cairo_create_layout(cr);
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

    // Shadow first (offset by a couple pixels)
    if (shadowcolor) {
        cairo_save(cr);
        cairo_translate(cr, 2, 2);
        cairo_set_source_rgba(cr, sr, sg, sb, sa);
        pango_cairo_show_layout(cr, layout_real);
        cairo_restore(cr);
    }

    // Outline: stroke path
    cairo_save(cr);
    cairo_set_line_width(cr, 2.5);
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
    int w = lw+2*pad;
    int h = lh+2*pad;
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr_down = cairo_create(surface);
    cairo_scale(cr_down, 0.5, 0.5); // shrink from 2× to 1×
    cairo_set_source_surface(cr_down, surface_ss, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr_down), CAIRO_FILTER_BEST);
    cairo_paint(cr_down);
    cairo_destroy(cr_down);

    unsigned char *data = cairo_image_surface_get_data(surface);

    bm.idxbuf = calloc(w*h, 1);
    bm.palette = malloc(16*sizeof(uint32_t));
    init_palette(bm.palette, palette_mode);

    int stride = cairo_image_surface_get_stride(surface);
    for (int yy=0; yy<h; yy++) {
        for (int xx=0; xx<w; xx++) {
            uint32_t argb = *(uint32_t*)(data + yy*stride + xx*4);
            uint8_t a = (argb >> 24) & 0xFF;
            if (a < 50) {
                bm.idxbuf[yy*w+xx] = 0;
                continue;
            }
            bm.idxbuf[yy*w+xx] = nearest_palette_index(bm.palette, 16, argb);
        }
    }

    bm.x = text_x - pad;
    bm.y = text_y - pad;
    bm.w = w;
    bm.h = h;

    // Cleanup
    pango_font_description_free(desc);
    g_object_unref(layout_dummy);
    cairo_destroy(cr_dummy);
    cairo_surface_destroy(dummy);
    g_object_unref(layout_real);
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
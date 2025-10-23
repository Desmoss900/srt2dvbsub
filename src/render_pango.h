#ifndef RENDER_PANGO_H
#define RENDER_PANGO_H

#include <stdint.h>

typedef struct {
    uint8_t *idxbuf;
    uint32_t *palette;
    int w,h,x,y;
    int nb_colors;
} Bitmap;

Bitmap render_text_pango(const char *markup,
                          int disp_w, int disp_h,
                          int fontsize, const char *fontfam,
                          const char *fgcolor,
                          const char *outlinecolor,
                          const char *shadowcolor,
                          int align_code,
                          const char *palette_mode);

char* srt_to_pango_markup(const char *srt_text);

void parse_hex_color(const char *hex, double *r, double *g, double *b, double *a);

/* Cleanup resources allocated by render_pango (call before FcFini()) */
void render_pango_cleanup(void);

/* Runtime knobs */
void render_pango_set_ssaa_override(int ssaa);
void render_pango_set_no_unsharp(int no_unsharp);

#endif
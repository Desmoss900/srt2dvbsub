#define _POSIX_C_SOURCE 200809L
#include "debug_png.h"
#include <cairo/cairo.h>
#include <stdio.h>

void save_bitmap_png(const Bitmap *bm, const char *filename) {
    if (!bm || !bm->idxbuf || !bm->palette) return;

    // Create ARGB surface from palette indices
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bm->w, bm->h);
    unsigned char *data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);

    for (int y = 0; y < bm->h; y++) {
        for (int x = 0; x < bm->w; x++) {
            uint8_t idx = bm->idxbuf[y * bm->w + x];
            uint32_t argb = bm->palette[idx];
            *(uint32_t *)(data + y * stride + x * 4) = argb;
        }
    }

    cairo_surface_mark_dirty(surface);
    cairo_surface_write_to_png(surface, filename);
    cairo_surface_destroy(surface);

    printf("Wrote debug PNG: %s\n", filename);
}
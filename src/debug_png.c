#define _POSIX_C_SOURCE 200809L
#include "debug_png.h"
#include <cairo/cairo.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

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
    // Ensure parent directory exists
    char *dirend = strrchr(filename, '/');
    if (dirend) {
        size_t dirlen = dirend - filename;
        char dirbuf[PATH_MAX];
        if (dirlen >= sizeof(dirbuf)) dirlen = sizeof(dirbuf)-1;
        memcpy(dirbuf, filename, dirlen);
        dirbuf[dirlen] = '\0';

        // create parents recursively
        char tmp[PATH_MAX];
        tmp[0] = '\0';
        for (char *p = dirbuf; *p; p++) {
            size_t len = strlen(tmp);
            tmp[len] = *p;
            tmp[len+1] = '\0';
            if (*p == '/') {
                if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
                    // ignore errors here; we'll detect write failure below
                }
            }
        }
        // final directory
        if (strlen(tmp) > 0) {
            if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
                // ignore, will be reported by cairo if write fails
            }
        }
    }

    cairo_status_t status = cairo_surface_write_to_png(surface, filename);
    cairo_surface_destroy(surface);

    char fullpath[PATH_MAX];
    if (getcwd(fullpath, sizeof(fullpath)) != NULL) {
        size_t need = strlen(fullpath) + 1 + strlen(filename) + 1;
        char *expected_alloc = malloc(need);
        if (expected_alloc) {
            snprintf(expected_alloc, need, "%s/%s", fullpath, filename);
            if (status == CAIRO_STATUS_SUCCESS) {
                fprintf(stderr, "Wrote debug PNG: %s (expected %s)\n", filename, expected_alloc);
            } else {
                const char *s = cairo_status_to_string(status);
                fprintf(stderr, "Failed to write PNG %s: %s (expected %s)\n", filename, s, expected_alloc);
            }
            free(expected_alloc);
        } else {
            if (status == CAIRO_STATUS_SUCCESS)
                fprintf(stderr, "Wrote debug PNG: %s\n", filename);
            else
                fprintf(stderr, "Failed to write PNG %s: %s\n", filename, cairo_status_to_string(status));
        }
    } else {
        if (status == CAIRO_STATUS_SUCCESS)
            fprintf(stderr, "Wrote debug PNG: %s\n", filename);
        else
            fprintf(stderr, "Failed to write PNG %s: %s\n", filename, cairo_status_to_string(status));
    }
}
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
#include "debug_png.h"
#include <cairo/cairo.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

/*
 * save_bitmap_png
 * ----------------
 * Write a diagnostic PNG from an internal Bitmap structure.
 *
 * Parameters:
 *   bm       - pointer to a Bitmap previously produced by the renderer.
 *              The function requires that `bm->idxbuf` (palette index
 *              buffer) and `bm->palette` (array of 32-bit ARGB entries)
 *              are non-NULL. If these fields are missing the function
 *              returns without side effects.
 *   filename - path (relative or absolute) where the PNG will be
 *              written. The function will attempt to create parent
 *              directories when possible.
 *
 * Behavior and guarantees:
 *   - This helper is intended only for debugging and is non-fatal.
 *   - On success it writes a PNG and prints a one-line status to
 *     stderr. On failure it prints an explanatory message to stderr.
 *   - The function makes no allocations that the caller must free.
 */
void save_bitmap_png(const Bitmap *bm, const char *filename) {
    /*
     * Sanity checks: ensure caller passed a valid bitmap with index
     * buffer and palette. If any required field is missing simply
     * return — PNG writing is strictly a debug convenience and should
     * not abort the program.
     */
    if (!bm || !bm->idxbuf || !bm->palette) return;

    /*
     * Create a temporary ARGB Cairo image surface sized to the
     * bitmap dimensions. We'll expand the 16-color palette indices
     * into full 32-bit ARGB pixels on this surface and then ask
     * Cairo to write the surface to a PNG file.
     */
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bm->w, bm->h);
    unsigned char *data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);

    /*
     * Expand palette indices into ARGB pixel values row-by-row. The
     * bitmap's idxbuf is indexed [y * width + x] and contains palette
     * entries (0..15). We copy the 32-bit palette word directly into
     * the Cairo surface memory (native endianness assumed).
     */
    for (int y = 0; y < bm->h; y++) {
        for (int x = 0; x < bm->w; x++) {
            uint8_t idx = bm->idxbuf[y * bm->w + x];
            uint32_t argb = bm->palette[idx];
            /* write as 32-bit pixel using surface stride */
            *(uint32_t *)(data + y * stride + x * 4) = argb;
        }
    }

    /* Tell Cairo that we modified the surface pixels directly. */
    cairo_surface_mark_dirty(surface);

    /*
     * Ensure parent directory exists by attempting to create each
     * parent path component. We tolerate failures here and rely on
     * Cairo's write call to report an error if writing ultimately fails.
     */
    char *dirend = strrchr(filename, '/');
    if (dirend) {
        size_t dirlen = dirend - filename;
        char dirbuf[PATH_MAX];
        if (dirlen >= sizeof(dirbuf)) dirlen = sizeof(dirbuf)-1;
        memcpy(dirbuf, filename, dirlen);
        dirbuf[dirlen] = '\0';

        /*
         * Build parent directories iteratively. This simple approach
         * avoids calling an external 'mkdir -p' and works with
         * relative and absolute paths. Note: we only attempt to create
         * directories and ignore EEXIST to allow concurrent runs.
         */
        char tmp[PATH_MAX];
        tmp[0] = '\0';
        for (char *p = dirbuf; *p; p++) {
            size_t len = strlen(tmp);
            tmp[len] = *p;
            tmp[len+1] = '\0';
            if (*p == '/') {
                if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
                    /* ignore errors here; we'll detect write failure below */
                }
            }
        }
        /* Final directory create (in case path didn't end with a slash) */
        if (strlen(tmp) > 0) {
            if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
                /* ignore, will be reported by cairo if write fails */
            }
        }
    }

    /* Ask Cairo to write the surface to PNG and free the surface. */
    cairo_status_t status = cairo_surface_write_to_png(surface, filename);
    cairo_surface_destroy(surface);

    /*
     * Print an informative message to stderr including the full
     * expected path (cwd + filename) when possible. This helps
     * correlate PNG writes with the debug logs users see.
     */
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
            /* If allocation fails, still print a minimal status string. */
            if (status == CAIRO_STATUS_SUCCESS)
                fprintf(stderr, "Wrote debug PNG: %s\n", filename);
            else
                fprintf(stderr, "Failed to write PNG %s: %s\n", filename, cairo_status_to_string(status));
        }
    } else {
        /* getcwd failed — print the basic status message */
        if (status == CAIRO_STATUS_SUCCESS)
            fprintf(stderr, "Wrote debug PNG: %s\n", filename);
        else
            fprintf(stderr, "Failed to write PNG %s: %s\n", filename, cairo_status_to_string(status));
    }
}
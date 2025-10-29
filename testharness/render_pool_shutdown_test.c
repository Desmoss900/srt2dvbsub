#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <libavutil/mem.h>
#include "../src/render_pool.h"

/* Minimal stub that mimics the real renderer enough for pool testing. */
Bitmap render_text_pango(const char *markup,
                         int disp_w, int disp_h,
                         int fontsize, const char *fontfam,
                         const char *fgcolor, const char *outlinecolor,
                         const char *shadowcolor, int align_code,
                         const char *palette_mode)
{
    (void)markup; (void)disp_w; (void)disp_h; (void)fontsize;
    (void)fontfam; (void)fgcolor; (void)outlinecolor; (void)shadowcolor;
    (void)align_code; (void)palette_mode;
    Bitmap bm = {0};
    const int w = 32, h = 16;
    bm.w = w; bm.h = h; bm.nb_colors = 4;
    bm.idxbuf = av_malloc(w * h);
    if (!bm.idxbuf) return bm;
    bm.idxbuf_len = (size_t)w * (size_t)h;
    /* fill with a simple pattern */
    for (int i = 0; i < w*h; i++) bm.idxbuf[i] = (uint8_t)(i % bm.nb_colors);
    bm.palette = av_malloc(sizeof(uint32_t) * 16);
    if (!bm.palette) { av_free(bm.idxbuf); bm.idxbuf = NULL; return bm; }
    for (int i = 0; i < 16; i++) bm.palette[i] = 0xff000000 | (i * 0x00101010);
    bm.palette_bytes = 16 * sizeof(uint32_t);
    /* simulate variable work time */
    usleep((rand() % 50 + 10) * 1000);
    return bm;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    srand((unsigned)time(NULL));
    printf("START render_pool_shutdown_test\n");
    if (render_pool_init(2) != 0) {
        fprintf(stderr, "failed to init render pool\n");
        return 2;
    }

    const int JOBS = 32;
    for (int i = 0; i < JOBS; i++) {
        char buf[64]; snprintf(buf, sizeof(buf), "job-%d", i);
        if (render_pool_submit_async(i / 4, i, buf, 320, 240, 12, "Sans", "#ffffff", "#000000", NULL, 5, "broadcast") != 0) {
            fprintf(stderr, "submit failed for %d\n", i);
        }
    }

    /* give a short window for some jobs to start and others to remain queued */
    usleep(50 * 1000);

    printf("Calling render_pool_shutdown() now (tests queued + running jobs)\n");
    render_pool_shutdown();
    printf("render_pool_shutdown() returned cleanly\n");
    return 0;
}

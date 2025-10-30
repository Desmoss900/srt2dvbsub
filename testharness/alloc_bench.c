#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>
#include <libavutil/mem.h>

/*
 * Simple allocation benchmark to measure cost of allocating index plane
 * + palette separately in a tight loop. Run locally to compare against
 * alternatives you might try.
 */

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    size_t w = 720, h = 480;
    if (argc >= 3) { w = (size_t)atoi(argv[1]); h = (size_t)atoi(argv[2]); }
    size_t pixel_count = w * h;
    size_t palette_bytes = 16 * sizeof(uint32_t);
    const size_t iterations = 20000;

    double t0 = now_sec();
    for (size_t i = 0; i < iterations; i++) {
        uint8_t *idx = av_malloc(pixel_count);
        if (!idx) { fprintf(stderr, "alloc idx failed\n"); return 1; }
        uint8_t *pal = av_mallocz(palette_bytes);
        if (!pal) { av_free(idx); fprintf(stderr, "alloc pal failed\n"); return 1; }
        av_free(idx);
        av_free(pal);
    }
    double t1 = now_sec();
    printf("Separate alloc/free: %.6f s (%.3f allocs/s)\n", t1 - t0, iterations/(t1-t0));

    /* try single combined allocation (for measurement only) */
    t0 = now_sec();
    for (size_t i = 0; i < iterations; i++) {
        void *blk = av_malloc(pixel_count + palette_bytes);
        if (!blk) { fprintf(stderr, "alloc blk failed\n"); return 1; }
        /* In practice we must be careful about freeing sub-pointers. */
        av_free(blk);
    }
    t1 = now_sec();
    printf("Single combined alloc/free: %.6f s (%.3f allocs/s)\n", t1 - t0, iterations/(t1-t0));

    return 0;
}

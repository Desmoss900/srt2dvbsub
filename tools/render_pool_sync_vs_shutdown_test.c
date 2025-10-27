#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <libavutil/mem.h>
#include "../src/render_pool.h"

/* stub renderer used by tests */
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
    const int w = 16, h = 8;
    bm.w = w; bm.h = h; bm.nb_colors = 4;
    bm.idxbuf = av_malloc(w * h);
    if (!bm.idxbuf) return bm;
    for (int i = 0; i < w*h; i++) bm.idxbuf[i] = (uint8_t)(i % bm.nb_colors);
    bm.palette = av_malloc(sizeof(uint32_t) * 16);
    if (!bm.palette) { av_free(bm.idxbuf); bm.idxbuf = NULL; return bm; }
    for (int i = 0; i < 16; i++) bm.palette[i] = 0xff000000 | (i * 0x00101010);
    /* simulate some work */
    usleep((rand() % 20 + 5) * 1000);
    return bm;
}

#define CLIENT_THREADS 8
#define ITERATIONS 100

typedef struct { int id; int completed; } client_t;

void *client_thread(void *arg) {
    client_t *c = (client_t*)arg;
    for (int i = 0; i < ITERATIONS; i++) {
        Bitmap bm = render_pool_render_sync("hello", 320,240,12, "Sans", "#fff", "#000", NULL, 5, "broadcast");
        if (bm.idxbuf) {
            av_free(bm.idxbuf); av_free(bm.palette);
            c->completed++;
        }
        /* slight pause between calls */
        usleep(1000);
    }
    return NULL;
}

int main(void) {
    srand((unsigned)time(NULL));
    printf("START rp_sync_vs_shutdown_test\n");
    if (render_pool_init(4) != 0) {
        fprintf(stderr, "render_pool_init failed\n");
        return 2;
    }
    pthread_t thr[CLIENT_THREADS];
    client_t clients[CLIENT_THREADS];
    for (int i = 0; i < CLIENT_THREADS; i++) {
        clients[i].id = i; clients[i].completed = 0;
        pthread_create(&thr[i], NULL, client_thread, &clients[i]);
    }

    /* Let clients start and run a bit, then shutdown the pool concurrently */
    usleep(50 * 1000);
    printf("Invoking render_pool_shutdown() while clients running\n");
    render_pool_shutdown();
    printf("render_pool_shutdown returned\n");

    for (int i = 0; i < CLIENT_THREADS; i++) pthread_join(thr[i], NULL);
    int total = 0;
    for (int i = 0; i < CLIENT_THREADS; i++) {
        printf("client %d completed %d sync calls\n", i, clients[i].completed);
        total += clients[i].completed;
    }
    printf("TOTAL completed: %d\n", total);
    return 0;
}

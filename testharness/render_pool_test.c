#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include "render_pool.h"
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

/* forward declarations */
static long long now_us(void);

/* Stub renderer: simulate work and print start/finish so the test shows per-job activity. */
Bitmap render_text_pango(const char *markup,
                         int disp_w, int disp_h,
                         int fontsize, const char *fontfam,
                         const char *fgcolor,
                         const char *outlinecolor,
                         const char *shadowcolor,
                         int align_code,
                         const char *palette_mode) {
    (void)disp_w; (void)disp_h; (void)fontsize; (void)fontfam;
    (void)fgcolor; (void)outlinecolor; (void)shadowcolor; (void)align_code; (void)palette_mode;
    /* Print start, sleep a bit to simulate render cost, then print finish. */
    pthread_t tid = pthread_self();
    printf("[%6lldus] worker[%lu] start render: '%s'\n", (long long)now_us(), (unsigned long)tid, markup ? markup : "(null)");
    /* sleep 20..120 ms */
    int delay = 20000 + (rand() % 100000);
    usleep(delay);
    printf("[%6lldus] worker[%lu] finished render: '%s' (sleep %d us)\n", (long long)now_us(), (unsigned long)tid, markup ? markup : "(null)", delay);
    Bitmap b = {0};
    b.idxbuf = NULL; b.palette = NULL; b.w = 0; b.h = 0; b.nb_colors = 0;
    return b;
}

/* Timestamp helper: base time and now_us() returning microseconds. */
static struct timeval tv0;
static long long now_us(void) {
    struct timeval t; gettimeofday(&t, NULL);
    return (long long)(t.tv_sec - tv0.tv_sec) * 1000000LL + (t.tv_usec - tv0.tv_usec);
}

int main(void) {
    printf("Starting render_pool test\n");
    if (render_pool_init(8) != 0) {
        fprintf(stderr, "failed to init pool\n");
        return 1;
    }
    /* seed PRNG for simulated render durations */
    srand((unsigned)time(NULL));
    /* Helper to print timestamps */
    gettimeofday(&tv0, NULL);

    printf("Submitting jobs (pool 8 workers)\n");
    // int total_jobs = 16;
    int submitted = 0;
    int completed_by_pool = 0;
    int completed_by_sync = 0;
    bool completed_flag[32] = {0};
    for (int i = 0; i < 32; i++) {
        char txt[64];
        snprintf(txt, sizeof(txt), "job-%d", i);
        long long t1 = now_us();
        int res = render_pool_submit_async(i, i, txt, 640, 480, 0, NULL, NULL, NULL, NULL, 1, NULL);
        long long t2 = now_us();
        if (res != 0) {
            fprintf(stderr, "[%6lldus] submit %2d FAILED (res=%d)\n", (long long)t2, i, res);
        } else {
            submitted++;
            printf("[%6lldus] submitted %2d ('%s') in %lld us\n", (long long)t2, i, txt, (long long)(t2 - t1));
        }
    }

    /* Quick immediate check: try_get should usually return 0 (not ready) or 1 (fast) */
    printf("\nImmediate try_get results:\n");
    int found = 0;
    for (int i = 0; i < 16; i++) {
        Bitmap out = {0};
        int r = render_pool_try_get(i, i, &out);
        if (r == 1) {
            printf("  job %2d: completed immediately (w=%d h=%d)\n", i, out.w, out.h);
            /* free result if any (none in our stub) */
            if (out.idxbuf) free(out.idxbuf);
            if (out.palette) free(out.palette);
            found++;
            completed_flag[i] = true;
            completed_by_pool++;
        } else if (r == 0) {
            printf("  job %2d: queued (not ready)\n", i);
        } else {
            printf("  job %2d: not found (-1)\n", i);
        }
    }

    /* Sleep a bit to let workers process some jobs */
    usleep(250 * 1000);

    printf("\nSecond try_get after 500ms:\n");
    int found2 = 0;
    for (int i = 0; i < 32; i++) {
        Bitmap out = {0};
        int r = render_pool_try_get(i, i, &out);
        if (r == 1) {
            printf("  job %2d: completed (w=%d h=%d)\n", i, out.w, out.h);
            if (out.idxbuf) free(out.idxbuf);
            if (out.palette) free(out.palette);
            found2++;
            completed_flag[i] = true;
            completed_by_pool++;
        } else if (r == 0) {
            printf("  job %2d: still queued\n", i);
        } else {
            printf("  job %2d: not found (-1)\n", i);
        }
    }

    /* For any remaining queued jobs, wait briefly for them to finish; if
     * they still haven't completed, use the synchronous fallback. This
     * avoids submitting a new redundant render job while the original
     * pending job is still being processed by the pool. */
    printf("\nSynchronous render fallback for pending jobs (wait then fallback):\n");
    for (int i = 0; i < 32; i++) {
        const int wait_ms = 500; /* max wait per job */
        const int poll_interval_us = 10 * 1000; /* 10ms */
        int waited = 0;
        Bitmap out = {0};
        int r = render_pool_try_get(i, i, &out);
        if (completed_flag[i]) {
            printf("  job %2d: already completed earlier; skipping fallback\n", i);
            continue;
        }
        while (r == 0 && waited < wait_ms) {
            usleep(poll_interval_us);
            waited += poll_interval_us / 1000;
            r = render_pool_try_get(i, i, &out);
        }
        if (r == 1) {
            printf("  job %2d: finished while waiting (w=%d h=%d)\n", i, out.w, out.h);
            if (out.idxbuf) free(out.idxbuf);
            if (out.palette) free(out.palette);
            completed_flag[i] = true;
            completed_by_pool++;
        } else if (r == 0) {
            /* Still queued after waiting -> fallback to synchronous render */
            printf("  job %2d: still queued after %dms, invoking synchronous fallback\n", i, wait_ms);
            Bitmap sync = render_pool_render_sync("<b>sync</b>", 640, 480, 0, NULL, NULL, NULL, NULL, 1, NULL);
            printf("  job %2d: sync render returned (w=%d h=%d)\n", i, sync.w, sync.h);
            if (sync.idxbuf) free(sync.idxbuf);
            if (sync.palette) free(sync.palette);
            completed_flag[i] = true;
            completed_by_sync++;
        } else {
            /* r == -1: job not found (maybe already consumed) */
            printf("  job %2d: not found when checking fallback (already consumed)\n", i);
        }
    }
    /* Final summary */
    printf("\nSummary:\n");
    printf("  submitted: %d\n", submitted);
    printf("  completed by pool: %d\n", completed_by_pool);
    printf("  completed by sync fallback: %d\n", completed_by_sync);
    int total_done = completed_by_pool + completed_by_sync;
    printf("  total completed: %d\n", total_done);

    /* Now shutdown (workers may still be running; shutdown waits) */
    printf("\nShutting down pool...\n");
    render_pool_shutdown();

    printf("render_pool shutdown completed\n");
    return 0;
}

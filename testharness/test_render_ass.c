#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include "../src/render_ass.h"
#include "../src/palette.h"

/* Provide a minimal debug_level for render_ass.c linking. */
#include "../src/debug.h"

/* Test hex color parsing behavior */
static void test_hex_colors(void) {
    char out[32];

    render_ass_hex_to_ass_color("#112233", out, sizeof(out));
    assert(strcmp(out, "&HFF332211") == 0);

    render_ass_hex_to_ass_color("#AA112233", out, sizeof(out));
    assert(strcmp(out, "&H55332211") == 0);

    render_ass_hex_to_ass_color(NULL, out, sizeof(out));
    assert(strcmp(out, "&H00FFFFFF") == 0);

    render_ass_hex_to_ass_color("not-a-color", out, sizeof(out));
    assert(strcmp(out, "&H00FFFFFF") == 0);

    printf("test_hex_colors: ok\n");
}

/* Test tile validation helper */
static void test_tile_validation(void) {
    uint8_t small_bitmap[4] = {1,2,3,4};

    /* valid small tile */
    assert(render_ass_validate_image_tile(2,2,2, small_bitmap) == 1);

    /* stride smaller than width -> invalid */
    assert(render_ass_validate_image_tile(3,2,2, small_bitmap) == 0);

    /* null bitmap -> invalid */
    assert(render_ass_validate_image_tile(2,2,2, NULL) == 0);

    /* huge tile -> invalid (exceeds MAX_TILE_PIXELS in implementation) */
    int big_stride = 100000;
    int big_h = 200;
    /* expect 100000*200 = 20,000,000 > 10,000,000 cap */
    assert(render_ass_validate_image_tile(big_stride, big_h, big_stride, small_bitmap) == 0);

    printf("test_tile_validation: ok\n");
}

/* Concurrency test for render_ass_lock/render_ass_unlock. We assert
 * that only one thread may be inside the critical section at once by
 * checking a shared counter. */
#define NTHREADS 4
#define LOOPS 2000
static volatile int in_cs = 0;

/* Define debug_level for debug.h logging used by the production code. */
int debug_level = 0;

static void *thread_fn(void *v) {
    (void)v;
    for (int i = 0; i < LOOPS; i++) {
        render_ass_lock();
        /* critical section: should be single-threaded */
        assert(in_cs == 0);
        in_cs = 1;
    /* tiny pause to amplify race chances if locking broken */
    sched_yield();
        in_cs = 0;
        render_ass_unlock();
    }
    return NULL;
}

static void test_locking(void) {
    pthread_t th[NTHREADS];
    for (int i = 0; i < NTHREADS; i++) {
        int rc = pthread_create(&th[i], NULL, thread_fn, NULL);
        assert(rc == 0);
    }
    for (int i = 0; i < NTHREADS; i++) pthread_join(th[i], NULL);
    printf("test_locking: ok\n");
}

/* Optional libass-dependent smoke tests: attempt to init libass and
 * exercise a couple of API paths. If libass isn't available the test
 * will skip these checks rather than fail. */
static void test_libass_smoke(void) {
    ASS_Library *lib = render_ass_init();
    if (!lib) {
        printf("test_libass_smoke: libass not available, skipping\n");
        return;
    }

    ASS_Renderer *r = render_ass_renderer(lib, 720, 576);
    assert(r != NULL);

    ASS_Track *t = render_ass_new_track(lib);
    assert(t != NULL);

    render_ass_set_style(t, "Sans", 24, "#112233", "#000000", "#000000");
    render_ass_add_event(t, "Hello world", 0, 2000);

    render_ass_free_track(t);
    render_ass_done(lib, r);

    printf("test_libass_smoke: ok\n");
}

int main(void) {
    test_hex_colors();
    test_tile_validation();
    test_locking();
    test_libass_smoke();

    printf("test_render_ass: all tests passed\n");
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>

#include "../src/qc.h"
#include "../src/srt_parser.h"

/* Provide globals referenced by qc.c */
int debug_level = 0;
int video_w = 1280;
int video_h = 720;
/* srt_parser.c expects this global when cfg is NULL */
int use_ass = 0;

int main(void) {
    SRTEntry cur = {0}, prev = {0};

    /* Test 1: empty/null text should not increment qc_error_count */
    qc_reset_counts();
    cur.start_ms = 0; cur.end_ms = 1000; cur.text = NULL;
    qc_check_entry("test.srt", 0, &cur, NULL, NULL);
    int errors = atomic_load_explicit(&qc_error_count, memory_order_relaxed);
    assert(errors == 0);

    /* Test 2: end <= start should increment error count */
    qc_reset_counts();
    cur.start_ms = 1000; cur.end_ms = 500; cur.text = "hello";
    qc_check_entry("test.srt", 1, &cur, NULL, NULL);
    errors = atomic_load_explicit(&qc_error_count, memory_order_relaxed);
    assert(errors == 1);

    /* Test 3: overlap shouldn't increment error counter */
    qc_reset_counts();
    prev.start_ms = 0; prev.end_ms = 2000; prev.text = "prev";
    cur.start_ms = 1500; cur.end_ms = 2500; cur.text = "overlap";
    qc_check_entry("test.srt", 2, &cur, &prev, NULL);
    errors = atomic_load_explicit(&qc_error_count, memory_order_relaxed);
    assert(errors == 0);

    /* Test 4: long single line triggers WARN but not ERROR */
    qc_reset_counts();
    char longline[256];
    for (int i = 0; i < 200; i++) longline[i] = 'A' + (i % 26);
    longline[200] = '\0';
    cur.start_ms = 0; cur.end_ms = 1000; cur.text = longline;
    qc_check_entry("test.srt", 3, &cur, NULL, NULL);
    errors = atomic_load_explicit(&qc_error_count, memory_order_relaxed);
    assert(errors == 0);

    printf("test_qc: all checks passed\n");
    return 0;
}

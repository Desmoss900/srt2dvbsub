#define _POSIX_C_SOURCE 200809L
#include "bench.h"
#include <stdio.h>
#include <stdint.h>
#include <time.h>

BenchStats bench = {0};

int64_t bench_now(void) {
    struct timespec ts;
    #if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
    #else
    // Fallback: use gettimeofday if clock_gettime not available
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
    #endif
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

void bench_start(void) {
    bench = (BenchStats){0};  // reset
}

void bench_report(void) {
    if (!bench.enabled) return;
    printf("\n--- Benchmark Report ---\n");
    printf("Cues rendered: %d\n", bench.cues_rendered);
    printf("Cues encoded: %d\n", bench.cues_encoded);
    printf("Packets muxed: %d\n", bench.packets_muxed);
    printf("Parse time:   %.3f ms\n", bench.t_parse_us / 1000.0);
    printf("Render time:  %.3f ms\n", bench.t_render_us / 1000.0);
    printf("Encode time:  %.3f ms\n", bench.t_encode_us / 1000.0);
    printf("Mux time:     %.3f ms\n", bench.t_mux_us / 1000.0);
}
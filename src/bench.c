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
*   Email: [license@chili-iptv.de]  
*   Website: [www.chili-iptv.de]
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

/*
 * bench.c
 * ------
 * Lightweight timing helpers used to instrument the srt2dvb tool. The
 * implementation provides a global `bench` accumulator (defined in
 * bench.h) and three convenience functions:
 *
 *  - bench_now():  return a monotonic timestamp in microseconds
 *  - bench_start(): reset/initialize the global bench counters
 *  - bench_report(): print a human-readable summary of accumulated timers
 *
 * The functions deliberately keep their dependencies small (time.h)
 * so they can be called early in program startup and used from a variety
 * of places. The global `bench` object is intentionally simple; callers
 * are responsible for any synchronization if they update it from worker
 * threads.
 */

#define _POSIX_C_SOURCE 200809L
#include "bench.h"
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <limits.h>

/* Global benchmark accumulators. Zero-initialized by default and updated
 * under `bench_mutex` for thread safety. */
BenchStats bench = {0};

/* Mutex to protect concurrent updates to the global bench counters. */
static pthread_mutex_t bench_mutex = PTHREAD_MUTEX_INITIALIZER;

void bench_add_encode_us(int64_t us) {
    if (us <= 0) return;
    pthread_mutex_lock(&bench_mutex);
    bench.t_encode_us += us;
    pthread_mutex_unlock(&bench_mutex);
}

void bench_add_mux_us(int64_t us) {
    if (us <= 0) return;
    pthread_mutex_lock(&bench_mutex);
    bench.t_mux_us += us;
    pthread_mutex_unlock(&bench_mutex);
}

void bench_add_mux_sub_us(int64_t us) {
    if (us <= 0) return;
    pthread_mutex_lock(&bench_mutex);
    bench.t_mux_sub_us += us;
    pthread_mutex_unlock(&bench_mutex);
}

void bench_add_parse_us(int64_t us) {
    if (us <= 0) return;
    pthread_mutex_lock(&bench_mutex);
    bench.t_parse_us += us;
    pthread_mutex_unlock(&bench_mutex);
}

void bench_add_render_us(int64_t us) {
    if (us <= 0) return;
    pthread_mutex_lock(&bench_mutex);
    bench.t_render_us += us;
    pthread_mutex_unlock(&bench_mutex);
}

void bench_inc_cues_encoded(void) {
    pthread_mutex_lock(&bench_mutex);
    if (bench.cues_encoded < INT_MAX)
        bench.cues_encoded++;
    else
        bench.cues_encoded = INT_MAX;
    pthread_mutex_unlock(&bench_mutex);
}

void bench_inc_packets_muxed(void) {
    pthread_mutex_lock(&bench_mutex);
    if (bench.packets_muxed < INT_MAX)
        bench.packets_muxed++;
    else
        bench.packets_muxed = INT_MAX;
    pthread_mutex_unlock(&bench_mutex);
}

void bench_inc_packets_muxed_sub(void) {
    pthread_mutex_lock(&bench_mutex);
    if (bench.packets_muxed_sub < INT_MAX)
        bench.packets_muxed_sub++;
    else
        bench.packets_muxed_sub = INT_MAX;
    pthread_mutex_unlock(&bench_mutex);
}

void bench_inc_cues_rendered(void) {
    pthread_mutex_lock(&bench_mutex);
    if (bench.cues_rendered < INT_MAX)
        bench.cues_rendered++;
    else
        bench.cues_rendered = INT_MAX;
    pthread_mutex_unlock(&bench_mutex);
}

void bench_set_enabled(int enabled) {
    pthread_mutex_lock(&bench_mutex);
    bench.enabled = enabled ? 1 : 0;
    pthread_mutex_unlock(&bench_mutex);
}


/*
 * bench_now
 * ---------
 * Return a monotonic timestamp measured in microseconds.
 *
 * This helper prefers CLOCK_MONOTONIC where available to avoid issues
 * caused by wall-clock adjustments. When CLOCK_MONOTONIC is not present
 * the implementation falls back to gettimeofday(). The value returned
 * is intended for delta calculations (e.g., t2 - t1 gives elapsed us).
 */
int64_t bench_now(void) {
    struct timespec ts;

    /* Attempt to use a monotonic clock source if the platform supports it.
     * clock_gettime(CLOCK_MONOTONIC) provides high-resolution monotonic
     * timestamps that won't jump when the system time is changed. */
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    /* Fallback path: use gettimeofday() for microsecond resolution. This
     * can be affected by system time changes, but remains portable. */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    /* Convert seconds + microseconds to a single microsecond timestamp. */
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
#endif

    /* Convert timespec (seconds + nanoseconds) to microseconds and return. */
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}


/*
 * bench_start
 * -----------
 * Reset the global benchmark accumulators to their initial state.
 *
 * This function zeroes the `bench` global object so subsequent
 * accumulations start from a known baseline. Call this once at program
 * startup when entering bench mode.
 */
void bench_start(void) {
    pthread_mutex_lock(&bench_mutex);
    bench = (BenchStats){0};
    pthread_mutex_unlock(&bench_mutex);
}


/*
 * bench_report
 * ------------
 * Emit a human-readable summary of accumulated benchmark statistics to
 * stdout. This prints counters followed by accumulated times (converted
 * from microseconds to milliseconds). If benchmarking is disabled the
 * function returns immediately.
 */
void bench_report(void) {
    pthread_mutex_lock(&bench_mutex);
    BenchStats snapshot = bench;
    pthread_mutex_unlock(&bench_mutex);

    /* If benchmarking is not enabled, do not print anything. */
    if (!snapshot.enabled) return;

    /* Print summary header and simple event counters. */
    printf("\n\n--- Benchmark Report ---\n");
    printf("Cues rendered: %d\n", snapshot.cues_rendered);
    printf("Cues encoded: %d\n", snapshot.cues_encoded);
    printf("Packets muxed: %d\n", snapshot.packets_muxed);
    if (snapshot.packets_muxed_sub > 0)
        printf("  of which subtitle packets: %d\n", snapshot.packets_muxed_sub);

    /* Convert accumulated microsecond totals to milliseconds for human
     * readability and print them with 3 decimal places. */
    printf("Parse time:   %.3f ms\n", snapshot.t_parse_us / 1000.0);
    printf("Render time:  %.3f ms\n", snapshot.t_render_us / 1000.0);
    printf("Encode time:  %.3f ms\n", snapshot.t_encode_us / 1000.0);
    printf("Mux time:     %.3f ms\n", snapshot.t_mux_us / 1000.0);
    if (snapshot.packets_muxed_sub > 0)
        printf("  Subtitle mux time: %.3f ms\n", snapshot.t_mux_sub_us / 1000.0);
}

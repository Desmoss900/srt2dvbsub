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

/* Global benchmark accumulators. Zero-initialized by default. Clients
 * may toggle `bench.enabled` to start/stop reporting. Note: updates to
 * fields of this struct are not synchronized — callers should serialize
 * access if used concurrently. */
BenchStats bench = {0};


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
    /* Use aggregate initialization to set all fields to zero in one
     * atomic-looking assignment. Note: this does not provide thread
     * safety; callers must synchronize access if used concurrently. */
    bench = (BenchStats){0};
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
    /* If benchmarking is not enabled, do not print anything. */
    if (!bench.enabled) return;

    /* Print summary header and simple event counters. */
    printf("\n--- Benchmark Report ---\n");
    printf("Cues rendered: %d\n", bench.cues_rendered);
    printf("Cues encoded: %d\n", bench.cues_encoded);
    printf("Packets muxed: %d\n", bench.packets_muxed);

    /* Convert accumulated microsecond totals to milliseconds for human
     * readability and print them with 3 decimal places. */
    printf("Parse time:   %.3f ms\n", bench.t_parse_us / 1000.0);
    printf("Render time:  %.3f ms\n", bench.t_render_us / 1000.0);
    printf("Encode time:  %.3f ms\n", bench.t_encode_us / 1000.0);
    printf("Mux time:     %.3f ms\n", bench.t_mux_us / 1000.0);
}
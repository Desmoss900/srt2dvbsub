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
#pragma once
#ifndef BENCH_H
#define BENCH_H

#include <stdint.h>

/**
 * @file bench.h
 * @brief Lightweight benchmarking helpers for srt2dvb
 *
 * Small, simple benchmarking helpers used by the srt2dvb tool to collect
 * coarse-grained timing statistics for parsing, rendering, encoding and
 * muxing operations. These helpers are intended for developer diagnostics
 * and profiling during debug/bench runs and are not used in production
 * performance-critical inner loops except where explicitly enabled.
 *
 * Conventions and units:
 *  - All timing fields measuring durations use microseconds (us).
 *  - Counters are simple integer accumulators.
 *  - The helper functions are not guaranteed to be thread-safe; callers
 *    should serialize access to the global `bench` state when used from
 *    multiple threads.
 *
 * Example usage:
 * @code
 *   bench_start();
 *   int64_t t0 = bench_now();
 *   // do work
 *   bench.t_parse_us += bench_now() - t0;
 *   bench.cues_rendered++;
 *   bench.enabled = 1; // enable reporting
 *   bench_report();
 * @endcode
 */

/**
 * @struct BenchStats
 * @brief Accumulators and counters for simple benchmarking.
 *
 * This structure holds coarse-grained timing accumulators (microseconds)
 * and integer counters used by the srt2dvb benchmarking helpers. The
 * fields are intentionally simple and callers must take care to
 * serialize access if updating from multiple threads.
 */
typedef struct {
    /** Flag: non-zero when benchmarking is enabled. */
    int enabled;

    /** Accumulated time spent parsing SRT files (microseconds). */
    int64_t t_parse_us;

    /** Accumulated time spent rendering subtitle bitmaps (microseconds). */
    int64_t t_render_us;

    /** Accumulated time spent encoding subtitles into bitstream (microseconds). */
    int64_t t_encode_us;

    /** Accumulated time spent muxing packets into output (microseconds). */
    int64_t t_mux_us;

    /** Number of subtitle cues rendered. */
    int cues_rendered;

    /** Number of subtitle cues handed to the encoder. */
    int cues_encoded;

    /** Number of packets written to the output. */
    int packets_muxed;
} BenchStats;

/**
 * @brief Global benchmarking state
 *
 * A single global accumulator used by the bench helpers. It is
 * zero-initialized by default. Callers may set `bench.enabled = 1` to
 * enable reporting. Access is not synchronized; protect updates when
 * accessed from multiple threads.
 *
 * Example:
 * @code
 *   bench.enabled = 1;
 *   bench.cues_rendered++;
 * @endcode
 */
extern BenchStats bench;

/**
 * @brief Reset and initialize the global bench counters
 *
 * Zeroes the `bench` global structure so subsequent accumulations start
 * from a clean baseline. Typically called once at program startup when
 * entering bench/debug mode.
 *
 * Example:
 * @code
 *   bench_start();
 * @endcode
 */
void bench_start(void);

/**
 * @brief Return a high-resolution monotonic timestamp in microseconds
 *
 * This function returns a microsecond-resolution timestamp suitable for
 * delta timing. The implementation prefers a monotonic clock when
 * available; callers should compute elapsed time as t2 - t1.
 *
 * @return timestamp in microseconds (int64_t)
 *
 * Example:
 * @code
 *   int64_t t0 = bench_now();
 *   // work
 *   int64_t elapsed_us = bench_now() - t0;
 * @endcode
 */
int64_t bench_now(void);

/**
 * @brief Print a human-readable report of accumulated bench stats
 *
 * Emit counters and accumulated times (converted to milliseconds) to
 * stdout. If `bench.enabled` is zero the function returns without
 * printing anything.
 *
 * Example:
 * @code
 *   bench.enabled = 1;
 *   bench_report();
 * @endcode
 */
void bench_report(void);

#endif
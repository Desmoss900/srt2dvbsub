/*
* Copyright (c) 2025 Mark E. Rosche, Capsaworks Project
* All rights reserved.
*
* PERSONAL USE LICENSE - NON-COMMERCIAL ONLY
* ────────────────────────────────────────────────────────────────
* This software is provided for personal, educational, and non-commercial
* use only. You are granted permission to use, copy, and modify this
* software for your own personal or educational purposes, provided that
* this copyright and license notice appears in all copies or substantial
* portions of the software.
*
* PERMITTED USES:
*   ✓ Personal projects and experimentation
*   ✓ Educational purposes and learning
*   ✓ Non-commercial testing and evaluation
*   ✓ Individual hobbyist use
*
* PROHIBITED USES:
*   ✗ Commercial use of any kind
*   ✗ Incorporation into products or services sold for profit
*   ✗ Use within organizations or enterprises for revenue-generating activities
*   ✗ Modification, redistribution, or hosting as part of any commercial offering
*   ✗ Licensing, selling, or renting this software to others
*   ✗ Using this software as a foundation for commercial services
*
* No commercial license is available. For inquiries regarding any use not
* explicitly permitted above, contact:
*   Mark E. Rosche, Capsaworks Project
*   Email: license@capsaworks-project.de
*   Website: www.capsaworks-project.de
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
* By using this software, you agree to these terms and conditions.
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

    /** Accumulated time spent muxing subtitle packets we generate (microseconds). */
    int64_t t_mux_sub_us;

    /** Number of subtitle cues rendered. */
    int cues_rendered;

    /** Number of subtitle cues handed to the encoder. */
    int cues_encoded;

    /** Number of packets written to the output. */
    int packets_muxed;

    /** Number of subtitle packets written to the output. */
    int packets_muxed_sub;
} BenchStats;

/**
 * @brief Global benchmarking state
 *
 * A single global accumulator used by the bench helpers. It is
 * zero-initialized by default. Callers may set `bench.enabled = 1` to
 * enable reporting. Direct field updates require external synchronization,
 * but the helper functions in this module already take an internal lock.
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

/* Thread-safe bench update helpers. Use these when updating global
 * bench counters from worker threads to avoid data races. Each helper
 * acquires the internal mutex, applies a saturated update (clamping at
 * INT_MAX for counters) and then releases the lock. */
void bench_add_encode_us(int64_t us);
void bench_add_mux_us(int64_t us);
void bench_add_mux_sub_us(int64_t us);
void bench_add_parse_us(int64_t us);
void bench_add_render_us(int64_t us);
void bench_inc_cues_encoded(void);
void bench_inc_packets_muxed(void);
void bench_inc_packets_muxed_sub(void);
void bench_inc_cues_rendered(void);
void bench_set_enabled(int enabled);

#endif

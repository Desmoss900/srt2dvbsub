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
#ifndef QC_H
#define QC_H

#include "srt_parser.h"
#include <stdio.h>

/**
 * @file qc.h
 * @brief Quality control checks for SRT entries.
 *
 * Provide a compact set of heuristics and checks to detect common issues in
 * SRT files (overlaps, bad durations, overly long lines, control characters,
 * etc.). Results are emitted as one-line QC messages either to the provided
 * `qc` FILE* or to stderr when `qc` is NULL.
 *
 * Example:
 * @code
 *   FILE *qc = fopen("qc.log", "w");
 *   qc_check_entry("subs.srt", idx, &cur, &prev, qc);
 *   fclose(qc);
 * @endcode
 *
 * @param filename Human-readable source filename used in log output.
 * @param index    Zero-based cue index used in logs.
 * @param entry    Pointer to the current SRTEntry to test (must be non-NULL).
 * @param prev     Optional pointer to the previous SRTEntry to check for overlaps.
 * @param qc       Optional FILE* to write machine-readable QC output. If NULL,
 *                 QC messages are written to stderr.
 */
void qc_check_entry(const char *filename, int index,
                    const SRTEntry *entry, const SRTEntry *prev,
                    FILE *qc);

/**
 * Global counter of QC-level ERROR messages emitted by qc_check_entry().
 * Call qc_reset_counts() to reset to zero before a batch run.
 */
extern int qc_error_count;

/**
 * Reset internal QC counters (errors). Useful when running batch QC-only
 * passes so the caller can print an aggregate summary after processing.
 */
void qc_reset_counts(void);

#endif
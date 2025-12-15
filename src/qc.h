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
#ifndef QC_H
#define QC_H

#include "srt_parser.h"
#include <stdio.h>
#include <stdatomic.h>

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
extern atomic_int qc_error_count;

/**
 * Reset internal QC counters (errors). Useful when running batch QC-only
 * passes so the caller can print an aggregate summary after processing.
 */
void qc_reset_counts(void);

#endif
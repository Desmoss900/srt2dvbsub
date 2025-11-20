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
 * Audit & Correction Summary (added 2025-10-28)
 * --------------------------------------------
 * Issues found and fixes applied to `qc.c`:
 *  - Null-dereference: guarded control-character and closing-tag
 *    checks against NULL `cur->text` to avoid crashes.
 *  - Data race: converted `qc_error_count` to `atomic_int` and used
 *    C11 atomic ops for reset/increment to make concurrent runs safe.
 *  - Diagnostic buffers: increased some snprintf buffers to reduce
 *    truncation risk.
 *  - Tests: added `testharness/test_qc.c` to exercise common QC cases.
 *
 * Notes: UTF-8 counting is heuristic; consider full UTF-8 validation
 * if stricter checks are required. Atomics were chosen for low
 * overhead; a pthread mutex can be used instead if preferred.
 */

#define _POSIX_C_SOURCE 200809L
#include "qc.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdatomic.h>

/* Provide a short module name for LOG() */
#define DEBUG_MODULE "qc"
#include "debug.h"

// ANSI colors (only if stderr is a terminal)
#define COL_RED   "\033[31m"
#define COL_YEL   "\033[33m"
#define COL_CYN   "\033[36m"
#define COL_RST   "\033[0m"

extern int debug_level;
/* Extern video dimensions (set in main.c) so QC thresholds can match
 * the wrapping heuristics used by the parser. These are intentionally
 * consulted here to choose SD vs HD thresholds for line-length checks. */
extern int video_w;
extern int video_h;

/* Local thresholds matching srt_parser's MAX_CHARS_SD / MAX_CHARS_HD */
#define QC_MAX_CHARS_SD 37
#define QC_MAX_CHARS_HD 67

/*
 * log_qc
 * ------
 * Internal helper to write a single QC line. When `qc` is supplied the
 * message is written to that FILE* in a plain, machine-friendly format. If
 * `debug_level` is set the same message is also echoed to stderr with ANSI
 * coloring to aid human inspection.
 */
atomic_int qc_error_count = 0;

/*
 * Resets the error count for quality control to zero.
 * This function should be called to clear any accumulated errors
 * before starting a new quality control process.
 */
void qc_reset_counts(void) {
    atomic_store_explicit(&qc_error_count, 0, memory_order_relaxed);
}

/*
 * Logs a QC (quality control) message to the specified file or stderr.
 *
 * Parameters:
 *   qc        - Pointer to the QC file (FILE*). If NULL, logs to stderr.
 *   level     - String indicating the log level (e.g., "ERROR", "WARNING").
 *   color     - ANSI color code string for colored output (used in debug mode).
 *   filename  - Name of the file associated with the log message.
 *   cue_idx   - Index of the cue related to the log message.
 *   msg       - The log message to be written.
 *
 * Behavior:
 *   - Writes the log message to the QC file if provided, otherwise to stderr.
 *   - In debug mode, also outputs a colored log message using LOG macro.
 *   - If the log level is "ERROR", increments the global error counter for batch QC reporting.
 */
static void log_qc(FILE *qc, const char *level, const char *color,
                   const char *filename, int cue_idx, const char *msg) {
    /* Write plain line to qc file when present, otherwise to stderr. */
    if (qc) {
        fprintf(qc, "%s: cue %d %s: %s\n", filename, cue_idx, level, msg);
        if (debug_level > 0) {
            LOG(1, "%s%s: cue %d %s: %s%s\n", color, filename, cue_idx, level, msg, COL_RST);
        }
    } else {
        fprintf(stderr, "%s: cue %d %s: %s\n", filename, cue_idx, level, msg);
    }

    /* If this line is an ERROR, increment the global error counter so
     * callers running batch QC can report a summary. */
    if (level && strcmp(level, "ERROR") == 0) {
        atomic_fetch_add_explicit(&qc_error_count, 1, memory_order_relaxed);
    }
}

/*
 * qc_check_entry
 * --------------
 * Run a battery of heuristic quality-control checks on a single SRT entry
 * and emit one-line messages describing warnings/errors found. This
 * function intentionally keeps checks conservative and non-fatal; it only
 * reports issues to aid downstream processing and human inspection.
 */
void qc_check_entry(const char *filename, int cue_idx,
                    const SRTEntry *cur, const SRTEntry *prev, FILE *qc) {
    /*
     * 1) Overlap: current start before previous end indicates overlapping
     *    cues which can confuse renderers or lead to ambiguous presentation.
     */
    if (prev && cur->start_ms < prev->end_ms) {
        log_qc(qc, "OVERLAP", COL_RED, filename, cue_idx,
               "overlaps previous cue");
    }

    /*
     * 2) end <= start: invalid or zero-length cues are treated as errors
     *    because renderers assume positive durations.
     */
    if (cur->end_ms <= cur->start_ms) {
        log_qc(qc, "ERROR", COL_RED, filename, cue_idx,
               "end <= start timestamp");
    }

    /*
     * 3) Very short cues (<250ms): warn because these often indicate
     *    formatting/timing issues; not considered fatal.
     */
    if ((cur->end_ms - cur->start_ms) < 250) {
        log_qc(qc, "WARN", COL_YEL, filename, cue_idx,
               "duration too short (<250ms)");
    }

    /* 4) Very long cues (>10s) */
    if ((cur->end_ms - cur->start_ms) > 10000) {
        log_qc(qc, "WARN", COL_YEL, filename, cue_idx,
               "duration unusually long (>10s)");
    }

    /* 5) Character-per-line check: compute the maximum run-length on any
     * line to detect lines that will wrap poorly when rendered. We operate
     * on UTF-8 codepoints (not bytes) so multi-byte glyphs count as a
     * single visible character. The threshold is chosen to match the
     * wrapping behavior used in the parser (SD vs HD rules). */
    const unsigned char *t = (const unsigned char *)cur->text;
    int maxlen = 0, curlen = 0, lines = 1;
    while (t && *t) {
        if (*t == '\n') {
            if (curlen > maxlen) maxlen = curlen;
            curlen = 0;
            lines++;
            t++;
            /* Check if this is the last character - if so, don't count the trailing newline */
            if (!*t) lines--;
            continue;
        }
        int adv = 1;
        if ((*t & 0x80) == 0) adv = 1;
        else if ((*t & 0xE0) == 0xC0) adv = 2;
        else if ((*t & 0xF0) == 0xE0) adv = 3;
        else if ((*t & 0xF8) == 0xF0) adv = 4;
        else adv = 1;
        /* Count one visible character and advance by UTF-8 sequence length */
        curlen++;
        t += adv;
    }
    if (curlen > maxlen) maxlen = curlen;

    int is_hd = (video_w > 720 || video_h > 576);
    int threshold = is_hd ? QC_MAX_CHARS_HD : QC_MAX_CHARS_SD;
    if (maxlen > threshold) {
        char msg[128];
        snprintf(msg, sizeof(msg), "line exceeds %d chars (%d)", threshold, maxlen);
        log_qc(qc, "WARN", COL_YEL, filename, cue_idx, msg);
    }

    /* 6) Too many lines (>3) */
    if (lines > 3) {
        char msg[128];
        snprintf(msg, sizeof(msg), "too many lines (%d)", lines);
        log_qc(qc, "WARN", COL_YEL, filename, cue_idx, msg);
    }

    /* 7) Control characters detection */
    if (cur->text) {
        for (t = (const unsigned char *)cur->text; *t; t++) {
            if (*t < 0x20 && *t != '\n' && *t != '\t') {
                log_qc(qc, "WARN", COL_YEL, filename, cue_idx,
                       "contains control characters");
                break;
            }
        }
    }

    /* 8) Empty text */
    if (!cur->text || strlen(cur->text) == 0) {
        log_qc(qc, "WARN", COL_YEL, filename, cue_idx,
               "empty cue text");
    }

    /* 9) Excessively long text (>200 chars) */
    if (cur->text && strlen(cur->text) > 200) {
        log_qc(qc, "WARN", COL_YEL, filename, cue_idx,
               "cue too verbose (>200 chars)");
    }

    /* 10) Informational: closing tags detected (parser normalized them). */
    if (cur->text && (strstr(cur->text, "</span>") || strstr(cur->text, "</i>") ||
        strstr(cur->text, "</b>") || strstr(cur->text, "</u>"))) {
        log_qc(qc, "INFO", COL_CYN, filename, cue_idx,
               "markup normalized/auto-closed");
    }
}
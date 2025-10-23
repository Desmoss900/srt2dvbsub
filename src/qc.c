#define _POSIX_C_SOURCE 200809L
#include "qc.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// ANSI colors (only if stderr is a terminal)
#define COL_RED   "\033[31m"
#define COL_YEL   "\033[33m"
#define COL_CYN   "\033[36m"
#define COL_RST   "\033[0m"

extern int debug_level;

static void log_qc(FILE *qc, const char *level, const char *color,
                   const char *filename, int cue_idx, const char *msg) {
    // Write plain line to qc file when present, otherwise to stderr
    if (qc) {
        fprintf(qc, "%s: cue %d %s: %s\n", filename, cue_idx, level, msg);
        // Also to stderr with colors (if debug enabled)
        if (debug_level > 0) {
            fprintf(stderr, "%s%s: cue %d %s: %s%s\n",
                    color, filename, cue_idx, level, msg, COL_RST);
        }
    } else {
        // When QC file is not enabled, emit the plain QC line to stderr so
        // callers see the same messages on stderr.
        fprintf(stderr, "%s: cue %d %s: %s\n", filename, cue_idx, level, msg);
    }
}

void qc_check_entry(const char *filename, int cue_idx,
                    const SRTEntry *cur, const SRTEntry *prev, FILE *qc) {
    // 1. Overlapping cues
    if (prev && cur->start_ms < prev->end_ms) {
        log_qc(qc, "OVERLAP", COL_RED, filename, cue_idx,
               "overlaps previous cue");
    }

    // 2. Negative/zero duration
    if (cur->end_ms <= cur->start_ms) {
        log_qc(qc, "ERROR", COL_RED, filename, cue_idx,
               "end <= start timestamp");
    }

    // 3. Very short cue (< 250 ms)
    if ((cur->end_ms - cur->start_ms) < 250) {
        log_qc(qc, "WARN", COL_YEL, filename, cue_idx,
               "duration too short (<250ms)");
    }

    // 4. Very long cue (> 10s)
    if ((cur->end_ms - cur->start_ms) > 10000) {
        log_qc(qc, "WARN", COL_YEL, filename, cue_idx,
               "duration unusually long (>10s)");
    }

    // 5. Character-per-line check
    const char *t = cur->text;
    int maxlen = 0, curlen = 0, lines = 1;
    for (; *t; t++) {
        if (*t == '\n') {
            if (curlen > maxlen) maxlen = curlen;
            curlen = 0;
            lines++;
        } else {
            curlen++;
        }
    }
    if (curlen > maxlen) maxlen = curlen;

    if (maxlen > 42) {
        char msg[64];
        snprintf(msg, sizeof(msg), "line exceeds %d chars (%d)", 42, maxlen);
        log_qc(qc, "WARN", COL_YEL, filename, cue_idx, msg);
    }

    // 6. Too many lines (>3)
    if (lines > 3) {
        char msg[64];
        snprintf(msg, sizeof(msg), "too many lines (%d)", lines);
        log_qc(qc, "WARN", COL_YEL, filename, cue_idx, msg);
    }

    // 7. Control characters
    for (t = cur->text; *t; t++) {
        if (*t < 0x20 && *t != '\n' && *t != '\t') {
            log_qc(qc, "WARN", COL_YEL, filename, cue_idx,
                   "contains control characters");
            break;
        }
    }

    // 8. Empty text
    if (!cur->text || strlen(cur->text) == 0) {
        log_qc(qc, "WARN", COL_YEL, filename, cue_idx,
               "empty cue text");
    }

    // 9. Excessively long text (> 200 chars)
    if (cur->text && strlen(cur->text) > 200) {
        log_qc(qc, "WARN", COL_YEL, filename, cue_idx,
               "cue too verbose (>200 chars)");
    }

    // 10. Closing tags already handled in parser,
    // here just note auto-closure
    if (strstr(cur->text, "</span>") || strstr(cur->text, "</i>") ||
        strstr(cur->text, "</b>") || strstr(cur->text, "</u>")) {
        log_qc(qc, "INFO", COL_CYN, filename, cue_idx,
               "markup normalized/auto-closed");
    }
}
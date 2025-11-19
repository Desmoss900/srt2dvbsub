
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
#ifndef SRT_PARSER_H
#define SRT_PARSER_H

#include <stdio.h>
#include <stdint.h>

/*
 * @file srt_parser.h
 * @brief Lightweight SRT/HTML/ASS parsing utilities.
 *
 * This module provides a small, dependency-free SRT parser used to
 * convert .srt files into an in-memory array of cues (start/end/text).
 * The parser performs basic normalization: it strips a leading UTF-8 BOM,
 * converts simple HTML tags into ASS overrides, optionally preserves ASS
 * markup, and normalizes whitespace/line-wrapping to fit SD/HD constraints.
 *
 * The parser is intentionally conservative and suitable for offline
 * preprocessing of subtitle files. It does not implement a full ASS parser
 * — for complex ASS features the project optionally uses libass via the
 * render_ass module.
 */

/*
 * Single parsed subtitle entry. 
 */
typedef struct {
    int64_t start_ms; /**< start time in milliseconds */
    int64_t end_ms;   /**< end time in milliseconds */
    char *text;       /**< UTF-8 markup text (caller frees) */
    int alignment;    /**< alignment code parsed from {\anX} (1..9) */
} SRTEntry;

/*
 * Parse an SRT file into an array of SRTEntry structures.
 *
 * @param filename Path to the SRT file to parse.
 * @param entries_out Output pointer that will be set to a malloc()'d
 *        array of SRTEntry structs. The caller is responsible for freeing
 *        each entry's `text` and the array itself.
 * @param qc Optional FILE* where quality-control warnings are written
 *        (may be NULL).
 * @return The number of parsed entries on success, or -1 on failure.
 */
int parse_srt(const char *filename, SRTEntry **entries_out, FILE *qc);

/* Validation severity levels for parser robustness */
typedef enum {
    SRT_VALIDATE_STRICT = 0,   /* Reject malformed cues, fail parse */
    SRT_VALIDATE_LENIENT = 1,  /* Log and skip malformed cues, continue */
    SRT_VALIDATE_AUTO_FIX = 2  /* Attempt to salvage malformed cues */
} SRTValidationLevel;

/*
 * Parser configuration struct to avoid relying on external globals.
 * New callers should prefer `parse_srt_cfg()` and pass an explicit
 * configuration to make parsing deterministic and testable.
 */
typedef struct {
    int use_ass;              /* preserve ASS markup instead of converting to ASS */
    int video_w;              /* video width used to decide SD/HD wrapping heuristics */
    int video_h;              /* video height used to decide SD/HD wrapping heuristics */
    
    /* Robustness enhancements */
    int validation_level;     /* SRTValidationLevel: STRICT, LENIENT, or AUTO_FIX */
    int max_line_length;      /* Maximum visible characters per line (0=unlimited) */
    int max_line_count;       /* Maximum lines per subtitle (0=unlimited) */
    int auto_fix_duplicates;  /* Auto-renumber duplicate cue IDs (1=yes, 0=no) */
    int auto_fix_encoding;    /* Auto-sanitize invalid UTF-8 (1=yes, 0=no) */
    int warn_on_short_duration;  /* Warn if cue duration < 100ms */
    int warn_on_long_duration;   /* Warn if cue duration > 30 seconds */
} SRTParserConfig;

/*
 * Statistics collected during parsing for robustness reporting.
 */
typedef struct {
    int total_cues;           /* Total cues encountered */
    int valid_cues;           /* Successfully parsed and stored */
    int skipped_cues;         /* Malformed or rejected cues */
    
    int duplicate_ids_fixed;  /* Duplicate IDs detected and corrected */
    int sequences_fixed;      /* Non-sequential ID ranges fixed */
    int overlaps_corrected;   /* Overlapping cues adjusted */
    int encoding_errors_fixed;/* Invalid UTF-8 sequences corrected */
    
    int encoding_warnings;    /* UTF-8 issues encountered */
    int timing_warnings;      /* Short/long duration warnings */
    int validation_warnings;  /* Other validation issues */
    
    int64_t min_duration;     /* Shortest cue duration in ms */
    int64_t max_duration;     /* Longest cue duration in ms */
    int64_t avg_duration;     /* Average cue duration in ms */
    
    int64_t min_gap;          /* Smallest gap between cues in ms */
    int64_t max_gap;          /* Largest gap between cues in ms */
} SRTParserStats;

/* Backwards-compatible variant that accepts an explicit configuration.
 * If `cfg` is NULL, behavior matches `parse_srt()` and uses current globals.
 */
int parse_srt_cfg(const char *filename, SRTEntry **entries_out, FILE *qc, const SRTParserConfig *cfg);

/* Extended variant that also returns parsing statistics. */
int parse_srt_with_stats(const char *filename, SRTEntry **entries_out, FILE *qc, 
                         const SRTParserConfig *cfg, SRTParserStats *stats_out);

/*
 * Convert minimal HTML (<i>, <b>, <font>) into ASS overrides. Caller
 * must free the returned string. 
 * */
char* srt_html_to_ass(const char *in);

/*
 * Strip ASS/HTML tags from a string and return a newly allocated
 * plain-text copy. Caller frees the result. 
 */
char* strip_tags(const char *in);

/*
 * Print parser statistics in human-readable format to the given FILE.
 * Does nothing if stats is NULL or all fields are zero.
 */
void srt_report_stats(const SRTParserStats *stats, FILE *out);

/*
 * Analyze gaps between consecutive cues and report timing anomalies.
 * Flags large gaps (>5s) which may indicate missing cues.
 */
void srt_analyze_gaps(const SRTEntry *entries, int n_cues, FILE *out);

/*
 * Print a timing summary table of the first N cues.
 * Useful for quick visual inspection. If max_rows <= 0, defaults to 10.
 */
void srt_print_timing_summary(const SRTEntry *entries, int n_cues, FILE *out, int max_rows);

#endif
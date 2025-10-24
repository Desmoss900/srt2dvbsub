
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

#endif
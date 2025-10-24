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
#ifndef SUBTRACK_H
#define SUBTRACK_H

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include "srt_parser.h"
#include "render_ass.h"

/*
 * @file subtrack.h
 * @brief Per-subtitle-track state and ownership semantics.
 *
 * The SubTrack structure holds everything required to emit a single
 * subtitle language track: the parsed cues, encoder context, and optional
 * libass track when ASS rendering is enabled. The struct intentionally
 * stores raw pointers to keep the implementation straightforward; callers
 * must follow the documented ownership rules below.
 */

/*
 * Per-track state used while emitting DVB/MP4 subtitle streams.
 *
 * Ownership notes:
 *  - `entries` is a malloc'd array returned by the SRT parser; the
 *    SubTrack owner is responsible for freeing each entry->text and the
 *    array itself when the track is torn down.
 *  - `stream` and `codec_ctx` are owned by the muxer/encoder subsystem
 *    and should not be free()'d by callers of SubTrack unless explicitly
 *    transferred.
 */
typedef struct SubTrack {
    SRTEntry *entries;      /**< Parsed cue array (caller frees entries[i].text and array) */
    int count;              /**< Number of parsed cues in `entries` */
    int cur_sub;            /**< Index of the currently active/next cue */
    AVStream *stream;       /**< AVStream used for this subtitle track (muxer-owned) */
    AVCodecContext *codec_ctx; /**< Per-track encoder context (muxer-owned) */
#ifdef HAVE_LIBASS
    ASS_Track *ass_track;   /**< Optional libass track if ASS rendering enabled */
#endif
    const char *lang;       /**< ISO language tag (not owned) */
    const char *filename;   /**< Source filename for this track (not owned) */
    int forced;             /**< Non-zero if the track is marked as 'forced' */
    int hi;                 /**< High-priority flag (internal use) */
    int64_t last_pts;       /**< Last emitted PTS for this track (for monotonicity) */
    int effective_delay_ms; /**< Per-track delay applied to cue timing in ms */
} SubTrack;

#endif 

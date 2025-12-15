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
    /* Per-track temporary buffer reused when encoding subtitles. This
     * avoids repeated av_malloc/av_free churn for every encoded cue.
     * Allocated lazily by encode_and_write_subtitle and intentionally
     * retained for the process lifetime. */
    uint8_t *enc_tmpbuf;
    /* Size of the allocated enc_tmpbuf in bytes (0 when not allocated). */
    size_t enc_tmpbuf_size;
    /* Count of consecutive times the encoder filled the buffer completely.
     * When this exceeds a small threshold we will increase the buffer size
     * automatically to reduce truncation. */
    int enc_tmpbuf_full_count;
} SubTrack;

#endif 

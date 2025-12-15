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

/*
 * muxsub.c
 *
 * Helper to encode an AVSubtitle using the provided encoder context and
 * write the resulting packet into the output format context. This file
 * provides a single exported helper `encode_and_write_subtitle` used by
 * the main program flow. The implementation documents buffer ownership,
 * bench timing updates and conditional debug logging.
 */

#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/mem.h>
#include "subtrack.h"
#include "bench.h"
#include "mux_write.h"
/* Provide a short module name for LOG() */
#define DEBUG_MODULE "muxsub"
#include "debug.h"

/* srt2dvbsub.c defines the global debug_level; declare it here. */
extern int debug_level;

/* Size of the temporary buffer used to hold encoded subtitle bytes. This is
 * chosen to be large enough for typical DVB/teletext encoded subtitle data
 * but not excessively large. */
#define SUB_BUF_SIZE 65536
/* Maximum buffer size we'll grow to automatically (1 MiB). */
#define MAX_SUB_BUF_SIZE (1<<20)
/* Number of times the encoder must fill the buffer before we auto-grow. */
#define FULL_COUNT_THRESHOLD 2

/*
* encode_and_write_subtitle
* -------------------------
* Encode the provided AVSubtitle using `ctx`, wrap the encoded bytes
* into an AVPacket and write the packet into `out_fmt`. The function
* updates per-track PTS/DTS monotonicity, updates bench counters when
* requested, and logs conditional debug information using the global
* `debug_level` variable.
*
* Ownership summary:
*  - The caller retains ownership of `sub`. This function does not
*    free `sub`.
*  - The function allocates temporary memory for encoding and an
*    AVPacket for writing; both are freed inside the function.
*/

void encode_and_write_subtitle(AVCodecContext *ctx,
                                      AVFormatContext *out_fmt,
                                      SubTrack *track,
                                      AVSubtitle *sub,
                                      int64_t pts90,
                                      int bench_mode,
                                      const char *dbg_png)
{
    /**
     * Checks if the subtitle event is valid.
     * If the subtitle event (`sub`) is NULL, logs a debug message (if `debug_level` > 2)
     * indicating that an empty or bad subtitle event is being skipped, and returns early.
     */
    if (!sub) {
        if (debug_level > 2) {
            LOG(3, "Skipping empty/bad subtitle event\n");
        }
        return;
    }

    /* Defensive input validation: ensure required pointers are present
     * before allocating temporary buffers or dereferencing fields. */
    if (!ctx || !out_fmt) {
        LOG(1, "Null encoder context or output format context\n");
        return;
    }
    if (!track || !track->stream) {
        LOG(1, "Null track or stream pointer\n");
        return;
    }

    /* Lazily allocate and reuse a per-track temporary buffer to avoid
     * repeated av_malloc/av_free churn on every subtitle encode. The
     * buffer is intentionally kept for the process lifetime. */
    uint8_t *tmpbuf = track->enc_tmpbuf;
    if (!tmpbuf) {
        size_t alloc_size = track->enc_tmpbuf_size ? track->enc_tmpbuf_size : SUB_BUF_SIZE;
        track->enc_tmpbuf = av_malloc(alloc_size);
        if (!track->enc_tmpbuf) {
            LOG(1, "out of memory: cannot allocate per-track encode buffer\n");
            return;
        }
        track->enc_tmpbuf_size = alloc_size;
        tmpbuf = track->enc_tmpbuf;
    }

    /* Encode and optionally measure encode time for bench stats. */
    int64_t t_enc = bench_now();
    int size = avcodec_encode_subtitle(ctx, tmpbuf, SUB_BUF_SIZE, sub);
    
    /*
     * If debugging is enabled (debug_level > 0), this statement logs the return value
     * of the avcodec_encode_subtitle function to stderr, prefixed with "[muxsub]".
     * The logged value 'size' typically represents the result of the subtitle encoding operation.
     */
    if (debug_level > 0)
        LOG(1, "avcodec_encode_subtitle returned %d\n", size);

    /*
     * If benchmarking mode is enabled, accumulate the time taken for encoding
     * by adding the elapsed time (current time minus start time) to the
     * total encoding time counter.
     */
    if (bench_mode) {
        bench_add_encode_us(bench_now() - t_enc);
    }

    /* If encoder produced no bytes, return. The per-track buffer is
     * intentionally retained for reuse and must not be freed here. Log
     * at debug level so callers can diagnose unexpected empty output. */
    if (size <= 0) {
        LOG(2, "encoder produced no bytes (size=%d) [stream=%d pts=%lld]\n",
            size, track->stream->index, (long long)pts90);
        return;
    }

    if (bench_mode)
        bench_inc_cues_encoded();

    /* If the encoder returned exactly the provided buffer size there is a
     * risk the output was truncated. Log this at debug level to help tune
     * SUB_BUF_SIZE if necessary. */
    if ((size_t)size >= track->enc_tmpbuf_size) {
        /* Encoder filled the buffer (or reported equal): increment counter
         * and consider growing the buffer after a few occurrences. */
        track->enc_tmpbuf_full_count++;
        LOG(2, "encoder filled buffer (%zu bytes) [stream=%d pts=%lld] count=%d\n",
            track->enc_tmpbuf_size, track->stream->index, (long long)pts90, track->enc_tmpbuf_full_count);
        if (track->enc_tmpbuf_full_count >= FULL_COUNT_THRESHOLD && track->enc_tmpbuf_size < MAX_SUB_BUF_SIZE) {
            size_t new_size = track->enc_tmpbuf_size * 2;
            if (new_size > MAX_SUB_BUF_SIZE) new_size = MAX_SUB_BUF_SIZE;
            uint8_t *newbuf = av_realloc(track->enc_tmpbuf, new_size);
            if (newbuf) {
                track->enc_tmpbuf = newbuf;
                track->enc_tmpbuf_size = new_size;
                LOG(1, "increased per-track encode buffer to %zu bytes for stream %d\n", new_size, track->stream->index);
            } else {
                LOG(1, "failed to grow per-track encode buffer to %zu bytes for stream %d\n", new_size, track->stream->index);
            }
            track->enc_tmpbuf_full_count = 0;
        }
    } else {
        /* Reset counter when output fits comfortably. */
        track->enc_tmpbuf_full_count = 0;
    }
    
    /**
     * Allocates an AVPacket structure and returns a pointer to it.
     * The AVPacket is used to store compressed data (such as audio or video frames)
     * and related metadata for processing in FFmpeg.
     * 
     * Returns:
     *   A pointer to an allocated AVPacket, or NULL if allocation fails.
     */
    AVPacket *pkt = av_packet_alloc();
    
    /**
     * Checks if the packet pointer 'pkt' is NULL.
     * If 'pkt' is NULL, frees the memory allocated for 'tmpbuf' and returns from the function.
     * This prevents further processing when there is no valid packet and ensures proper memory cleanup.
     */
    if (!pkt) {
        LOG(1, "av_packet_alloc failed [stream=%d pts=%lld]\n", track->stream->index, (long long)pts90);
        return;
    }

    /**
     * Attempts to allocate a new AVPacket with the specified size.
     * If allocation fails (av_new_packet returns a negative value), 
     * frees the temporary buffer and the packet, then returns early.
     */
    int ret_new = av_new_packet(pkt, size);
    if (ret_new < 0) {
        char errbuf[128];
        av_strerror(ret_new, errbuf, sizeof(errbuf));
        LOG(1, "av_new_packet failed: %d (%s) [stream=%d pts=%lld]\n",
            ret_new, errbuf, track->stream->index, (long long)pts90);
        av_packet_free(&pkt);
        return;
    }

    /*
     * Copies 'size' bytes from the temporary buffer 'tmpbuf' into the packet's data buffer 'pkt->data'.
     * This operation overwrites the contents of 'pkt->data' with the data from 'tmpbuf'.
     */
    memcpy(pkt->data, tmpbuf, size);
    
    /* Attach packet to the track's stream; caller must ensure track->stream. */
    pkt->stream_index = track->stream->index;

    /* Enforce per-track monotonic PTS/DTS in our internal 90kHz timeline;
     * bump by 90 ticks (1ms) if needed. We keep `track->last_pts` in the
     * same 90kHz units used throughout the code so comparisons are simple.
     */
    if (track->last_pts != AV_NOPTS_VALUE && pts90 <= track->last_pts) {
        pts90 = track->last_pts + 90; /* bump 1ms forward */
    }
    track->last_pts = pts90;

    /* Convert internal 90kHz PTS to the stream's timebase expected by
     * libavformat. By convention we create subtitle streams with a
     * time_base of {1,90000} so this will be a noop; however being
     * explicit here makes the code resilient if the stream time_base
     * changes for any reason. */
    AVRational tb_90k = (AVRational){1, 90000};
    int64_t pkt_pts = pts90;
    if (track->stream && (track->stream->time_base.num != 1 || track->stream->time_base.den != 90000)) {
        pkt_pts = av_rescale_q(pts90, tb_90k, track->stream->time_base);
    }
    pkt->pts = pkt_pts;
    pkt->dts = pkt_pts;

    /* Mux/write the packet and update bench counters / logging. */
    int64_t t0 = bench_now();

    /*
     * Writes a packet to the output format context in an interleaved manner.
     * Returns the result of the write operation.
     *
     * Parameters:
     *   out_fmt - pointer to the output format context.
     *   pkt     - pointer to the packet to be written.
     *
     * The function ensures proper interleaving of audio/video/subtitle streams
     * when writing to the output file.
     */
    int ret = safe_av_interleaved_write_frame(out_fmt, pkt);
    
    /**
     * Logs the result of av_interleaved_write_frame if debugging is enabled.
     *
     * If debug_level is greater than 0:
     *   - If ret is negative, retrieves the error string for ret using av_strerror
     *     and prints an error message to stderr including the error code and description.
     *   - If ret is non-negative and dbg_png is set, prints a message to stderr indicating
     *     that encoding from PNG occurred, including the PNG filename.
     */
    if (debug_level > 0) {
        if (ret < 0) {
            char errbuf[128];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG(1, "av_interleaved_write_frame returned %d (%s) [stream=%d pts=%lld]\n",
                ret, errbuf, pkt->stream_index, (long long)pkt->pts);
        } else {
            if (dbg_png)
                LOG(1, "encoded from PNG: %s [stream=%d pts=%lld]\n",
                    dbg_png, pkt->stream_index, (long long)pkt->pts);
        }
    }
    
    /**
     * If benchmarking mode is enabled, update benchmarking statistics:
     * - Add the elapsed time since t0 to the total muxing time (bench.t_mux_us).
     * - Increment the count of encoded cues (bench.cues_encoded).
     * - Increment the count of muxed packets (bench.packets_muxed).
     */
    if (bench_mode) {
        int64_t delta_mux = bench_now() - t0;
        bench_add_mux_us(delta_mux);
        bench_add_mux_sub_us(delta_mux);
        if (ret >= 0) {
            bench_inc_packets_muxed();
            bench_inc_packets_muxed_sub();
        }
    }

    /**
     * Frees the memory allocated for an AVPacket and sets the pointer to NULL.
     * This function is typically used to clean up after processing or sending a packet,
     * ensuring that no memory leaks occur.
     *
     * param - pkt Pointer to the AVPacket pointer to be freed.
     */
    av_packet_free(&pkt);
}

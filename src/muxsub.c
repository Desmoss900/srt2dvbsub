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
#include "subtrack.h"
#include "bench.h"

/* srt2dvbsub.c defines the global debug_level; declare it here. */
extern int debug_level;

/* Size of the temporary buffer used to hold encoded subtitle bytes. This is
 * chosen to be large enough for typical DVB/teletext encoded subtitle data
 * but not excessively large. */
#define SUB_BUF_SIZE 65536

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
            fprintf(stderr, "Skipping empty/bad subtitle event\n");
        }
        return;
    }

    /* Allocate temporary buffer for encoder output. Freed on all paths. */
    uint8_t *tmpbuf = av_malloc(SUB_BUF_SIZE);
    if (!tmpbuf)
        return; /* out of memory: nothing we can do here */

    /* Encode and optionally measure encode time for bench stats. */
    int64_t t_enc = bench_now();
    int size = avcodec_encode_subtitle(ctx, tmpbuf, SUB_BUF_SIZE, sub);
    
    /*
     * If debugging is enabled (debug_level > 0), this statement logs the return value
     * of the avcodec_encode_subtitle function to stderr, prefixed with "[muxsub]".
     * The logged value 'size' typically represents the result of the subtitle encoding operation.
     */
    if (debug_level > 0)
        fprintf(stderr, "[muxsub] avcodec_encode_subtitle returned %d\n", size);

    /*
     * If benchmarking mode is enabled, accumulate the time taken for encoding
     * by adding the elapsed time (current time minus start time) to the
     * total encoding time counter.
     */
    if (bench_mode)
        bench.t_encode_us += bench_now() - t_enc;

    /* If encoder produced no bytes, free the buffer and return. */
    if (size <= 0) {
        av_free(tmpbuf);
        return;
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
        av_free(tmpbuf);
        return;
    }

    /**
     * Attempts to allocate a new AVPacket with the specified size.
     * If allocation fails (av_new_packet returns a negative value), 
     * frees the temporary buffer and the packet, then returns early.
     */
    if (av_new_packet(pkt, size) < 0) {
        av_free(tmpbuf);
        av_packet_free(&pkt);
        return;
    }

    /*
     * Copies 'size' bytes from the temporary buffer 'tmpbuf' into the packet's data buffer 'pkt->data'.
     * This operation overwrites the contents of 'pkt->data' with the data from 'tmpbuf'.
     */
    memcpy(pkt->data, tmpbuf, size);
    
    /**
     * Frees the memory allocated for the buffer pointed to by tmpbuf.
     * Uses the FFmpeg av_free() function to safely deallocate memory.
     * Ensure that tmpbuf was previously allocated with av_malloc() or similar.
     */
    av_free(tmpbuf);

    /* Attach packet to the track's stream; caller must ensure track->stream. */
    pkt->stream_index = track->stream->index;

    /* Enforce per-track monotonic PTS/DTS; bump by 90 ticks (1ms) if needed. */
    if (track->last_pts != AV_NOPTS_VALUE && pts90 <= track->last_pts) {
        pts90 = track->last_pts + 90; /* bump 1ms forward */
    }
    
    /*
     * Set the presentation timestamp (PTS) and decoding timestamp (DTS) of the packet to the value of pts90.
     * Also update the track's last_pts to pts90 to keep track of the most recent timestamp.
     */
    pkt->pts = pts90;
    pkt->dts = pts90;
    track->last_pts = pts90;

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
    int ret = av_interleaved_write_frame(out_fmt, pkt);
    
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
            fprintf(stderr, "[dvb] av_interleaved_write_frame returned %d (%s)\n", ret, errbuf);
        } else {
            if (dbg_png)
                fprintf(stderr, "[dvb] encoded from PNG: %s\n", dbg_png);
        }
    }
    
    /**
     * If benchmarking mode is enabled, update benchmarking statistics:
     * - Add the elapsed time since t0 to the total muxing time (bench.t_mux_us).
     * - Increment the count of encoded cues (bench.cues_encoded).
     * - Increment the count of muxed packets (bench.packets_muxed).
     */
    if (bench_mode) {
        bench.t_mux_us += bench_now() - t0;
        bench.cues_encoded++;
        bench.packets_muxed++;
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

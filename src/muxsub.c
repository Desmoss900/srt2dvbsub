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
*   Email: [license@chili-iptv.info]  *   Website: [www.chili-iptv.info]
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

#define _POSIX_C_SOURCE 200809L
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include "subtrack.h"
#include "bench.h"

/* srt2dvbsub.c defines the global debug_level; declare it here. */
extern int debug_level;

void encode_and_write_subtitle(AVCodecContext *ctx,
                                      AVFormatContext *out_fmt,
                                      SubTrack *track,
                                      AVSubtitle *sub,
                                      int64_t pts90,
                                      int bench_mode,
                                      const char *dbg_png)
{
    if (!sub)
    {
        if (debug_level > 2)
        {
            fprintf(stderr, "Skipping empty/bad subtitle event\n");
        }
        return;
    }

#define SUB_BUF_SIZE 65536
    uint8_t *tmpbuf = av_malloc(SUB_BUF_SIZE);
    if (!tmpbuf)
        return;

    int64_t t_enc = bench_now();
    int size = avcodec_encode_subtitle(ctx, tmpbuf, SUB_BUF_SIZE, sub);
    if (debug_level > 0) fprintf(stderr, "[muxsub] avcodec_encode_subtitle returned %d\n", size);
    if (bench_mode)
        bench.t_encode_us += bench_now() - t_enc;

    if (size > 0)
    {
        AVPacket *pkt = av_packet_alloc();
        if (!pkt)
        {
            av_free(tmpbuf);
            return;
        }
        if (av_new_packet(pkt, size) < 0)
        {
            av_free(tmpbuf);
            av_packet_free(&pkt);
            return;
        }
        memcpy(pkt->data, tmpbuf, size);
        av_free(tmpbuf);

        pkt->stream_index = track->stream->index;

        // Per-track last_pts to ensure monotonicity per subtitle track
        if (track->last_pts != AV_NOPTS_VALUE && pts90 <= track->last_pts)
        {
            pts90 = track->last_pts + 90; // bump 1ms forward to avoid non-monotonic DTS
        }
        pkt->pts = pts90;
        pkt->dts = pts90;
        track->last_pts = pts90;

        int64_t t0 = bench_now();
        int ret = av_interleaved_write_frame(out_fmt, pkt);
        if (debug_level > 0)
        {
            if (ret < 0)
            {
                char errbuf[128];
                av_strerror(ret, errbuf, sizeof(errbuf));
                fprintf(stderr, "[dvb] av_interleaved_write_frame returned %d (%s)\n", ret, errbuf);
            }
            else
            {
                if (dbg_png)
                    fprintf(stderr, "[dvb] encoded from PNG: %s\n", dbg_png);
            }
        }
        if (bench_mode)
        {
            bench.t_mux_us += bench_now() - t0;
            bench.cues_encoded++;
            bench.packets_muxed++;
        }
        av_packet_free(&pkt);
    }
    else
    {
        av_free(tmpbuf);
    }
}

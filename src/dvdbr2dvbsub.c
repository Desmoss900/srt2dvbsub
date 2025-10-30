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

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <getopt.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>

#include <libavutil/error.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include "cpu_count.h"
#include "runtime_opts.h"
#include <libavutil/mem.h>

#include "srt_parser.h"
#include "render_pango.h"
#include "render_ass.h"
#include "dvb_sub.h"
#include "debug_png.h"
#include "qc.h"
#include "bench.h"
#include "mux_write.h"
#include "utils.h"


/* Provide a short module name for LOG() */
#define DEBUG_MODULE "dvdbr2dvbsub"
#include "debug.h"

typedef struct {
    Bitmap bm;
    int start_display_time;
    int end_display_time;
} SubEvent;

typedef struct {
    SubEvent *events;
    int count;
    AVStream *stream;
    AVCodecContext *codec_ctx;
    AVCodecContext *dec_ctx;
    const char *lang;
    int forced;
    int hi;
    int64_t last_pts;
    int effective_delay_ms;
    int64_t first_subtitle_pts90;
} GraphicSubTrack;

static int __dbg_png_seq = 0;

static void bitmap_release(Bitmap *bm)
{
    if (!bm) return;
    if (bm->idxbuf) { av_free(bm->idxbuf); bm->idxbuf = NULL; }
    if (bm->palette) { av_free(bm->palette); bm->palette = NULL; }
    bm->nb_colors = 0;
    bm->palette_bytes = 0;
    bm->idxbuf_len = 0;
    bm->w = bm->h = bm->x = bm->y = 0;
}

static void subevent_release(SubEvent *ev)
{
    if (!ev) return;
    bitmap_release(&ev->bm);
    ev->start_display_time = 0;
    ev->end_display_time = 0;
}

static void graphic_subtrack_clear(GraphicSubTrack *track)
{
    if (!track) return;
    if (track->events) {
        for (int i = 0; i < track->count; i++) subevent_release(&track->events[i]);
        free(track->events);
        track->events = NULL;
    }
    track->count = 0;
    if (track->codec_ctx) {
        avcodec_free_context(&track->codec_ctx);
        track->codec_ctx = NULL;
    }
    if (track->dec_ctx) {
        avcodec_free_context(&track->dec_ctx);
        track->dec_ctx = NULL;
    }
    if (track->lang) {
        free((void*)track->lang);
        track->lang = NULL;
    }
    track->forced = 0;
    track->hi = 0;
    track->last_pts = AV_NOPTS_VALUE;
    track->effective_delay_ms = 0;
    track->first_subtitle_pts90 = AV_NOPTS_VALUE;
    track->stream = NULL;
}

static void print_dvdbr_usage(void)
{
    printf("Usage: dvdbr2dvbsub --input in.ts --output out.ts --languages eng[,deu] [options]\n");
    printf("Try 'dvdbr2dvbsub --help' for more information.\n");
}

static void print_dvdbr_help(void)
{
    print_version();
    printf("Usage: dvdbr2dvbsub --input in.ts --output out.ts --languages eng[,deu] [options]\n\n");
    printf("Options:\n");
    printf("  -I, --input FILE            Input Media (ts, mkv, mp4)\n");
    printf("  -o, --output FILE           Output TS file with DVB subtitles muxed in\n");
    printf("  -l, --languages CODES       Comma-separated DVB language codes\n");
    printf("      --src-fps FPS           Override detected source frame rate\n");
    printf("      --dst-fps FPS           Target frame rate when remapping PTS\n");
    printf("      --delay MS              Global subtitle delay in milliseconds\n");
    printf("      --forced                Mark output subtitles as forced\n");
    printf("      --hi                    Mark output subtitles as hearing-impaired\n");    
    printf("      --debug N               Set libav debug verbosity (0..2)\n");
    printf("      --bench                 Enable benchmark timing output\n");
    printf("      --version               Show version information and exit\n");
    printf("  -h, --help                  Show this help text and exit\n\n");
    printf("Examples:\n");
    printf("  dvdbr2dvbsub --input main.ts --output muxed.ts --languages eng,deu\n");
    printf("  dvdbr2dvbsub --input bd.m2ts --output out.ts --languages eng --delay 150\n");
    printf("  dvdbr2dvbsub --input bd.m2ts --qc-only --srt captions.srt --languages eng\n");
    printf("\n");
}

/* signal handling: set this flag in the handler and check in main loops */
static volatile sig_atomic_t stop_requested = 0;
static void dvdbr_signal_request_stop(int sig)
{
    handle_signal(sig, &stop_requested);
}

// Convert an RGBA plane to an indexed buffer with up-to-16-color palette.
static void rgba_to_indexed(uint8_t *rgba, int linesize, int w, int h,
                            uint8_t **idxbuf_out, uint32_t **palette_out, int *nb_colors_out)
{
    uint8_t *idx = av_malloc(w * h);
    if (!idx) return;
    uint32_t *palette = av_mallocz(AVPALETTE_SIZE);
    if (!palette) {
        av_free(idx);
        return;
    }
    int colors = 0;
    for (int y = 0; y < h; y++) {
        uint8_t *row = rgba + y * linesize;
        for (int x = 0; x < w; x++) {
            uint8_t r = row[x*4 + 0];
            uint8_t g = row[x*4 + 1];
            uint8_t b = row[x*4 + 2];
            uint8_t a = row[x*4 + 3];
            uint32_t col = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)a;
            int found = -1;
            for (int i = 0; i < colors; i++) {
                if (palette[i] == col) { found = i; break; }
            }
            if (found < 0) {
                if (colors < 16) {
                    palette[colors] = col;
                    found = colors;
                    colors++;
                } else {
                    found = 0; // fallback to first palette entry
                }
            }
            idx[y*w + x] = (uint8_t)found;
        }
    }
    *idxbuf_out = idx;
    *palette_out = palette;
    *nb_colors_out = colors;
}

// Nearest-neighbor resize for RGBA image
static uint8_t *rgba_resize_nn(uint8_t *src, int src_linesize, int src_w, int src_h, int dst_w, int dst_h)
{
    uint8_t *dst = av_malloc((size_t)dst_w * dst_h * 4);
    if (!dst) return NULL;
    for (int y = 0; y < dst_h; y++) {
        int sy = (y * src_h) / dst_h;
        uint8_t *drow = dst + (size_t)y * dst_w * 4;
        uint8_t *srow = src + (size_t)sy * src_linesize;
        for (int x = 0; x < dst_w; x++) {
            int sx = (x * src_w) / dst_w;
            uint8_t *sp = srow + sx * 4;
            uint8_t *dp = drow + x * 4;
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
        }
    }
    return dst;
}

// Resize indexed (palette) image nearest-neighbor. src_idx is src_w*src_h bytes.
static uint8_t *indexed_resize_nn(uint8_t *src_idx, int src_w, int src_h, int dst_w, int dst_h)
{
    uint8_t *dst = av_malloc((size_t)dst_w * dst_h);
    if (!dst) return NULL;
    for (int y = 0; y < dst_h; y++) {
        int sy = (y * src_h) / dst_h;
        for (int x = 0; x < dst_w; x++) {
            int sx = (x * src_w) / dst_w;
            dst[y*dst_w + x] = src_idx[sy*src_w + sx];
        }
    }
    return dst;
}

/* Provide a small compatibility helper for best-effort packet timestamp.
   Some older libavcodec versions don't expose av_packet_get_best_effort_timestamp
   and the AVPacket struct may lack best_effort_timestamp field. Use pts/dts as fallback. */
static inline int64_t get_packet_best_effort_timestamp(const AVPacket *pkt)
{
    if (!pkt) return AV_NOPTS_VALUE;
    if (pkt->pts != AV_NOPTS_VALUE) return pkt->pts;
    if (pkt->dts != AV_NOPTS_VALUE) return pkt->dts;
    return AV_NOPTS_VALUE;
}

// Local encoder+mux wrapper for GraphicSubTrack (legacy encode API)
static void encode_and_write_subtitle(AVCodecContext *ctx,
                                     AVFormatContext *out_fmt,
                                     GraphicSubTrack *track,
                                     AVSubtitle *sub,
                                     int64_t pts90,
                                     int bench_mode,
                                     const char *dbg_png)
{
    if (!sub) return;
    if (debug_level > 0) LOG(1, "Encoding sub num_rects=%d\n", sub->num_rects);
    if (sub->num_rects > 0) {
        AVSubtitleRect *r = sub->rects[0];
        if (debug_level > 0) LOG(1, "rect w=%d h=%d nb_colors=%d\n", r->w, r->h, r->nb_colors);
        if (r->data[1] && debug_level > 0) {
            uint32_t *pal = (uint32_t*)r->data[1];
            LOG(1, "palette 0x%08x 0x%08x 0x%08x 0x%08x\n", pal[0], pal[1], pal[2], pal[3]);
        }
    }
#if 0
    // prefer send/receive when available; legacy encoder is used here
#endif
    #define SUB_BUF_SIZE 65536
    uint8_t *tmpbuf = av_malloc(SUB_BUF_SIZE);
    if (!tmpbuf) return;

    int64_t t_enc = bench_now();
    int size = avcodec_encode_subtitle(ctx, tmpbuf, SUB_BUF_SIZE, sub);
    if (bench_mode) {
        int64_t delta = bench_now() - t_enc;
        bench_add_encode_us(delta);
    }
    if (debug_level > 0) LOG(1, "avcodec_encode_subtitle returned %d\n", size);
    if (size > 0 && debug_level >= 2) {
        int dump = size > 32 ? 32 : size;
        fprintf(stderr, "[dvb-debug] encoded first %d bytes:\n", dump);
        for (int i = 0; i < dump; i++) fprintf(stderr, "%02x ", tmpbuf[i] & 0xff);
        fprintf(stderr, "\n");
    }

    if (size > 0) {
        if (bench_mode)
            bench_inc_cues_encoded();
        AVPacket *pkt = av_packet_alloc();
        if (!pkt) {
            av_free(tmpbuf);
            return;
        }
        if (av_new_packet(pkt, size) < 0) {
            av_free(tmpbuf);
            av_packet_free(&pkt);
            return;
        }
        memcpy(pkt->data, tmpbuf, size);
        av_free(tmpbuf);
        pkt->stream_index = track->stream->index;

        if (track->last_pts != AV_NOPTS_VALUE && pts90 <= track->last_pts) {
            pts90 = track->last_pts + 90;
        }
        // set pkt timestamps in 90k base then rescale to stream timebase for muxer
        pkt->pts = pts90;
        pkt->dts = pts90;
        track->last_pts = pts90;
        av_packet_rescale_ts(pkt, (AVRational){1,90000}, track->stream->time_base);

        int64_t t0 = bench_now();
        int ret = safe_av_interleaved_write_frame(out_fmt, pkt);
        if (debug_level > 0) {
            if (ret < 0) {
                char errbuf[128]; av_strerror(ret, errbuf, sizeof(errbuf));
                LOG(1, "av_interleaved_write_frame returned %d (%s)\n", ret, errbuf);
            } else {
                LOG(1, "av_interleaved_write_frame returned %d (pkt size=%d)\n", ret, pkt->size);
                if (dbg_png) LOG(1, "encoded from PNG: %s\n", dbg_png);
            }
        }
        /* Diagnostic: flush and report AVIO position so we can tell if anything hit disk */
        if (ret >= 0 && out_fmt && out_fmt->pb) {
            avio_flush(out_fmt->pb);
            int64_t pos = avio_tell(out_fmt->pb);
            if (debug_level > 0) LOG(1, "after write avio_tell=%lld\n", (long long)pos);
        }
        if (bench_mode) {
            int64_t delta = bench_now() - t0;
            bench_add_mux_us(delta);
            if (ret >= 0)
                bench_inc_packets_muxed();
        }
        av_packet_free(&pkt);
    } else {
        av_free(tmpbuf);
    }
}

int main(int argc, char **argv) {
    const char *input=NULL, *output=NULL;
    char *srt_list=NULL, *lang_list=NULL;
    int forced=0, hi=0, qc_only=0, bench_mode=0;
    int subtitle_delay_ms=0;
    double src_fps = 0.0, dst_fps = 0.0;

    static struct option long_opts[] = {
        {"input",     required_argument, 0, 'I'},
        {"output",    required_argument, 0, 'o'},
        {"srt",       required_argument, 0, 's'},
        {"languages", required_argument, 0, 'l'},
        {"forced",    no_argument,       0, 1000},
        {"hi",        no_argument,       0, 1001},
        {"debug",     required_argument, 0, 1002},
        {"bench",     no_argument,       0, 1004},
        {"delay",     required_argument, 0, 1012},
        {"src-fps",   required_argument, 0, 1013},
        {"dst-fps",   required_argument, 0, 1014},
        {"version",   no_argument,       0, 1015},
        {"help",      no_argument,       0, 'h'},
        {0,0,0,0}
    };

    int opt, long_index=0;
    while ((opt=getopt_long(argc,argv,"I:o:s:l:h",long_opts,&long_index))!=-1) {
        switch(opt) {
        case 'I': input=optarg; break;
        case 'o': output=optarg; break;
        case 's':
            srt_list = strdup(optarg);
            if (!srt_list) {
                fprintf(stderr, "Error: out of memory duplicating --srt argument\n");
                return 1;
            }
            break;
        case 'l':
            lang_list = strdup(optarg);
            if (!lang_list) {
                fprintf(stderr, "Error: out of memory duplicating --languages argument\n");
                return 1;
            }
            break;
        case 1000: forced=1; break;
        case 1001: hi=1; break;
        case 1002: debug_level=atoi(optarg); break;
        case 1004: bench_mode=1; break;
        case 1012: subtitle_delay_ms = atoi(optarg); break;
    case 1013: src_fps = atof(optarg); break;
        case 1014: dst_fps = atof(optarg); break;
        case 1015:
            print_version();
            return 0;
        case 'h':
            print_dvdbr_help();
            return 0;
        default:
            print_dvdbr_usage();
            return 1;
        }
    }

    if(!input||!output||!lang_list){
        fprintf(stderr,"Error: --input, --output and --languages are required\n");
        print_dvdbr_usage();
        return 1;
    }

    if (debug_level > 1)
        av_log_set_level(AV_LOG_INFO);
    else if (debug_level == 1)
        av_log_set_level(AV_LOG_ERROR);
    else
        av_log_set_level(AV_LOG_QUIET);

    bench_start();
    bench_set_enabled(bench_mode);

    // Progress tracking
    time_t prog_start_time = time(NULL);
    int64_t total_duration_pts90 = AV_NOPTS_VALUE; // in 90k units
    int64_t current_pts90 = 0;
    long pkt_count = 0;
    long subs_found = 0;

    int ret = 0;
    FILE *qc = NULL;
    int ntracks = 0;
    GraphicSubTrack tracks[8] = {0};
    AVPacket *pkt = NULL;
#define FAIL(code) do { ret = (code); goto cleanup; } while (0)
    if (qc_only) {
        qc = fopen("qc_log.txt", "w");
        if (!qc) { perror("qc_log.txt"); FAIL(1); }
    }

    // Open input
    avformat_network_init();
    AVFormatContext *in_fmt=NULL;
    if(avformat_open_input(&in_fmt,input,NULL,NULL)<0){
        fprintf(stderr,"Cannot open input\n"); FAIL(-1);
    }
    avformat_find_stream_info(in_fmt,NULL);

    int video_index=-1, first_audio_index=-1;
    for (unsigned i=0;i<in_fmt->nb_streams;i++) {
        AVStream *st=in_fmt->streams[i];
        if (st->codecpar->codec_type==AVMEDIA_TYPE_VIDEO && video_index<0) {
            video_index=i;
            if (st->codecpar->width > 0)  video_w=st->codecpar->width;
            if (st->codecpar->height > 0) video_h=st->codecpar->height;
        }
        if (st->codecpar->codec_type==AVMEDIA_TYPE_AUDIO && first_audio_index<0) {
            first_audio_index=i;
        }
    }

    /* If available, read source video FPS from input stream */
    if (video_index >= 0) {
        AVRational ar = in_fmt->streams[video_index]->avg_frame_rate;
        if (ar.num && ar.den) src_fps = av_q2d(ar);
        if (debug_level > 0) fprintf(stderr, "Detected source video index=%d fps=%f\n", video_index, src_fps);
    }

    /* Compute input start PTS (90k base) so we can emit timestamps relative to stream start */
    int64_t input_start_pts90 = 0;
    if (in_fmt->start_time != AV_NOPTS_VALUE) {
        input_start_pts90 = av_rescale_q(in_fmt->start_time, AV_TIME_BASE_Q, (AVRational){1,90000});
    } else if (video_index >= 0 && in_fmt->streams[video_index]->start_time != AV_NOPTS_VALUE) {
        input_start_pts90 = av_rescale_q(in_fmt->streams[video_index]->start_time,
                                         in_fmt->streams[video_index]->time_base,
                                         (AVRational){1,90000});
    }
    if (debug_level > 0) fprintf(stderr, "input_start_pts90=%lld\n", (long long)input_start_pts90);

    // compute total duration if available
    if (in_fmt->duration != AV_NOPTS_VALUE && in_fmt->duration > 0) {
        total_duration_pts90 = av_rescale_q(in_fmt->duration, AV_TIME_BASE_Q, (AVRational){1,90000});
    } else if (video_index >= 0 && in_fmt->streams[video_index]->duration != AV_NOPTS_VALUE) {
        total_duration_pts90 = av_rescale_q(in_fmt->streams[video_index]->duration, in_fmt->streams[video_index]->time_base, (AVRational){1,90000});
    }

    // Print status header with FPS conversion and separators (kept above in-place progress line)
    {
        int dash_count = 60;
        for (int i = 0; i < dash_count; i++) fputc('-', stdout);
        fputc('\n', stdout);
        if (dst_fps > 0.0 && src_fps > 0.0)
            fprintf(stdout, "FPS Conversion: %.3f->%.3f\n", src_fps, dst_fps);
        else if (dst_fps > 0.0)
            fprintf(stdout, "FPS Conversion: src->%.3f\n", dst_fps);
        else if (src_fps > 0.0)
            fprintf(stdout, "FPS Conversion: %.3f->dst\n", src_fps);
        else
            fprintf(stdout, "FPS Conversion: unknown\n");
        for (int i = 0; i < dash_count; i++) fputc('-', stdout);
        fputc('\n', stdout);
        fflush(stdout);
    }

    // Ensure pngs directory exists for debug output only when debug level > 0
    if (debug_level > 0) {
        if (mkdir("pngs", 0755) < 0 && errno != EEXIST) {
            fprintf(stderr, "Warning: could not create pngs/ directory: %s\n", strerror(errno));
        }
    }

    /* Track first video packet PTS (90k) seen during demux to use as an alternate reference */
    int seen_first_video = 0;
    int64_t first_video_pts90 = AV_NOPTS_VALUE;
    int pkt_debug_limit = 200;
    int pkt_debug_count = 0;

    // Find subtitle streams in input
    int sub_stream_indices[8];
    int n_sub_streams = 0;
    char *save_lang=NULL;
    char *tok_lang = strtok_r(lang_list, ",", &save_lang);
    while (tok_lang && n_sub_streams < 8) {
        // Find matching subtitle stream
        int found = -1;
        for (unsigned i=0; i<in_fmt->nb_streams; i++) {
            AVStream *st = in_fmt->streams[i];
            if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                AVDictionaryEntry *lang_entry = av_dict_get(st->metadata, "language", NULL, 0);
                const char *lang = lang_entry ? lang_entry->value : NULL;
                if (lang && strcasecmp(lang, tok_lang) == 0) {
                    found = i;
                    break;
                }
            }
        }
        if (found >= 0) {
            sub_stream_indices[n_sub_streams] = found;
            n_sub_streams++;
            if (debug_level > 0) printf("Found subtitle stream %d for language %s\n", found, tok_lang);
        } else {
            fprintf(stderr, "Warning: No subtitle stream found for language %s\n", tok_lang);
        }
        tok_lang = strtok_r(NULL, ",", &save_lang);
    }

    // Prepare output format context for MPEG-TS
    AVFormatContext *out_fmt = NULL;
    if (avformat_alloc_output_context2(&out_fmt, NULL, "mpegts", output) < 0) {
        fprintf(stderr, "Cannot alloc out_fmt\n");
        FAIL(-1);
    }

    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_DVB_SUBTITLE);
    if (!codec) {
        fprintf(stderr,"DVB subtitle encoder not found\n");
        FAIL(-1);
    }
    if (debug_level > 0) fprintf(stderr, "Found DVB subtitle encoder: %p\n", (void*)codec);

    for (int s=0; s<n_sub_streams; s++) {
        int sub_idx = sub_stream_indices[s];
        AVStream *sub_st = in_fmt->streams[sub_idx];
        AVDictionaryEntry *lang_entry = av_dict_get(sub_st->metadata, "language", NULL, 0);
        const char *lang = lang_entry ? lang_entry->value : "und";

        if (debug_level > 0) fprintf(stderr, "Starting processing subtitle stream %d (lang=%s)\n", sub_idx, lang);

        const char *lang_src = lang ? lang : "und";
        tracks[ntracks].lang = strdup(lang_src);
        if (!tracks[ntracks].lang) {
            fprintf(stderr, "Error: out of memory duplicating subtitle track language\n");
            graphic_subtrack_clear(&tracks[ntracks]);
            FAIL(-1);
        }
        tracks[ntracks].forced = forced;
        tracks[ntracks].hi = hi;
        tracks[ntracks].last_pts = AV_NOPTS_VALUE;
    tracks[ntracks].first_subtitle_pts90 = AV_NOPTS_VALUE;
        tracks[ntracks].effective_delay_ms = subtitle_delay_ms;

        // Open subtitle decoder for this track (we will decode in the single demux loop below)
        tracks[ntracks].dec_ctx = avcodec_alloc_context3(NULL);
        if (!tracks[ntracks].dec_ctx) {
            fprintf(stderr, "Failed to alloc subtitle decoder context\n");
            graphic_subtrack_clear(&tracks[ntracks]);
            FAIL(-1);
        }
        avcodec_parameters_to_context(tracks[ntracks].dec_ctx, sub_st->codecpar);
        const AVCodec *sub_decoder = avcodec_find_decoder(sub_st->codecpar->codec_id);
        if (!sub_decoder) {
            fprintf(stderr, "Subtitle decoder not found for codec %d\n", sub_st->codecpar->codec_id);
            graphic_subtrack_clear(&tracks[ntracks]);
            FAIL(-1);
        }
        if (avcodec_open2(tracks[ntracks].dec_ctx, sub_decoder, NULL) < 0) {
            fprintf(stderr, "Failed to open subtitle decoder\n");
            graphic_subtrack_clear(&tracks[ntracks]);
            FAIL(-1);
        }

        // No pre-collection: events will be decoded/converted/encoded on-the-fly in the demux loop
        tracks[ntracks].events = NULL;
        tracks[ntracks].count = 0;

        // Create output subtitle stream
        tracks[ntracks].stream = avformat_new_stream(out_fmt, NULL);
        if (!tracks[ntracks].stream) {
            fprintf(stderr, "Failed to create output stream for track %s\n", lang);
            graphic_subtrack_clear(&tracks[ntracks]);
            FAIL(-1);
        }
        tracks[ntracks].stream->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
        tracks[ntracks].stream->codecpar->codec_id = AV_CODEC_ID_DVB_SUBTITLE;
        tracks[ntracks].stream->time_base = (AVRational){1,90000};
        tracks[ntracks].stream->start_time = 0;
        av_dict_set(&tracks[ntracks].stream->metadata, "language", lang, 0);
        if (forced) av_dict_set(&tracks[ntracks].stream->metadata, "forced", "1", 0);
        if (hi) av_dict_set(&tracks[ntracks].stream->metadata, "hearing_impaired", "1", 0);

        // Allocate encoder context per track
        tracks[ntracks].codec_ctx = avcodec_alloc_context3(codec);
        if (!tracks[ntracks].codec_ctx) {
            fprintf(stderr, "Failed to alloc codec context for track %s\n", lang);
            graphic_subtrack_clear(&tracks[ntracks]);
            FAIL(-1);
        }
        tracks[ntracks].codec_ctx->time_base = (AVRational){1,90000};
        tracks[ntracks].codec_ctx->width = video_w;
        tracks[ntracks].codec_ctx->height = video_h;

    /* Configure encoder threading: prefer auto CPU count */
        if (enc_threads <= 0) {
            tracks[ntracks].codec_ctx->thread_count = get_cpu_count();
        } else {
            tracks[ntracks].codec_ctx->thread_count = enc_threads;
        }
#if defined(FF_THREAD_FRAME)
    tracks[ntracks].codec_ctx->thread_type = FF_THREAD_FRAME;
#elif defined(FF_THREAD_SLICE)
    tracks[ntracks].codec_ctx->thread_type = FF_THREAD_SLICE;
#endif

    if (avcodec_open2(tracks[ntracks].codec_ctx, codec, NULL) < 0) {
            fprintf(stderr, "Failed to open DVB subtitle encoder for track %s\n", lang);
            graphic_subtrack_clear(&tracks[ntracks]);
            FAIL(-1);
        }

        // Copy codec parameters from encoder context to the output stream so muxer knows stream params
        if (avcodec_parameters_from_context(tracks[ntracks].stream->codecpar, tracks[ntracks].codec_ctx) < 0) {
            fprintf(stderr, "Warning: failed to copy codec params to output stream for track %s\n", lang);
        }

        if (debug_level > 0) {
            fprintf(stderr, "Opened DVB encoder for graphic sub track %d (%s): w=%d h=%d\n",
                ntracks, tracks[ntracks].lang,
                tracks[ntracks].codec_ctx->width,
                tracks[ntracks].codec_ctx->height);
        }

        ntracks++;
    }

    if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_fmt->pb, output, AVIO_FLAG_WRITE) < 0) {
            fprintf(stderr, "Error: could not open output file %s\n", output);
            FAIL(-1);
        }
    }

    if (avformat_write_header(out_fmt, NULL) < 0) {
        fprintf(stderr, "Error: could not write header for output file\n");
        FAIL(-1);
    }
    /* If there is no video stream, emit a tiny blank subtitle at PTS=1 so
     * the output stream start_time is initialized. If there is a video
     * stream we wait until we see the first video PTS and then emit the
     * blank at (first_video_pts90 + 1) inside the demux loop below. */
    if (video_index < 0) {
        for (int t = 0; t < ntracks; t++) {
            AVSubtitle blank_sub = {0};
            blank_sub.pts = 1;  /* AVSubtitle.pts is in AV_TIME_BASE units */
            blank_sub.end_display_time = 2; /* 1ms display */
            encode_and_write_subtitle(tracks[t].codec_ctx, out_fmt, &tracks[t], &blank_sub, 1, bench_mode, NULL);
            avsubtitle_free(&blank_sub);
        }
    }
    if (debug_level > 0) {
        if (dst_fps > 0.0) fprintf(stderr, "Subtitle PTS will be scaled: src_fps=%f dst_fps=%f scale=%f\n", src_fps, dst_fps, (src_fps>0.0? src_fps/dst_fps: 0.0));
    }

    /* Install simple signal handlers so Ctrl-C triggers orderly shutdown */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = dvdbr_signal_request_stop;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Single demux loop: decode subtitle packets for tracked streams and encode/write immediately
    pkt = av_packet_alloc();
    AVSubtitle sub;
    while (av_read_frame(in_fmt, pkt) >= 0) {
        if (stop_requested) {
            if (debug_level > 0) fprintf(stderr, "[dvdbr2dvbsub] stop requested (signal), breaking demux loop\n");
            av_packet_unref(pkt);
            break;
        }
        pkt_count++;
        if (pkt->pts != AV_NOPTS_VALUE) {
            current_pts90 = av_rescale_q(pkt->pts, in_fmt->streams[pkt->stream_index]->time_base, (AVRational){1,90000});
        }
        // progress print every N packets
        if ((pkt_count & 0x3f) == 0) {
            double elapsed = difftime(time(NULL), prog_start_time);
            double pct = 0.0;
            double eta = 0.0;
            if (total_duration_pts90 != AV_NOPTS_VALUE && total_duration_pts90 > 0) {
                pct = (double)(current_pts90 - input_start_pts90) / (double)total_duration_pts90;
                if (pct < 0.0) pct = 0.0;
                if (pct > 1.0) pct = 1.0;
                if (pct > 0.001) {
                    double total_est = elapsed / pct;
                    eta = total_est - elapsed;
                }
            }
            int mins = (int)(elapsed / 60.0);
            int secs = (int)(elapsed) % 60;
            int eta_m = (int)(eta / 60.0);
            int eta_s = (int)(eta) % 60;
            if (total_duration_pts90 != AV_NOPTS_VALUE)
                fprintf(stdout, "\rProgress: %5.1f%% subs=%ld elapsed=%02d:%02d ETA=%02d:%02d   ", pct*100.0, subs_found, mins, secs, eta_m, eta_s);
            else
                fprintf(stdout, "\rProgress: pkt=%ld subs=%ld elapsed=%02d:%02d   ", pkt_count, subs_found, mins, secs);
            fflush(stdout);
        }
        // record first video pts seen
        if (!seen_first_video && video_index >= 0 && pkt->stream_index == video_index) {
            if (pkt->pts != AV_NOPTS_VALUE) {
                first_video_pts90 = av_rescale_q(pkt->pts, in_fmt->streams[video_index]->time_base, (AVRational){1,90000});
                seen_first_video = 1;
                if (debug_level > 0) fprintf(stderr, "first_video_pts90=%lld\n", (long long)first_video_pts90);

                /* Emit a tiny blank subtitle event at first_video_pts90 + 1
                 * so the output subtitle stream start_time aligns with the
                 * first video PTS and the display is cleared at stream start. */
                for (int t = 0; t < ntracks; t++) {
                    AVSubtitle blank_sub = {0};
                    blank_sub.pts = 1; /* AVSubtitle.pts is not used by encoder here */
                    blank_sub.end_display_time = 2;
                    encode_and_write_subtitle(tracks[t].codec_ctx, out_fmt, &tracks[t], &blank_sub, first_video_pts90 + 1, bench_mode, NULL);
                    avsubtitle_free(&blank_sub);
                }
            }
        }

    if (debug_level >= 3 && pkt_debug_count < pkt_debug_limit) {
        int64_t best = get_packet_best_effort_timestamp(pkt);
            fprintf(stderr, "pkt: stream=%d pts=%lld dts=%lld best_effort=%lld size=%d\n",
                    pkt->stream_index,
            (long long)pkt->pts,
            (long long)pkt->dts,
            (long long)best,
                    pkt->size);
            pkt_debug_count++;
        }
        for (int ti = 0; ti < ntracks; ti++) {
            if (pkt->stream_index == in_fmt->streams[sub_stream_indices[ti]]->index) {
                if (debug_level > 0) fprintf(stderr, "Read packet stream %d (subtitle), size %d\n", pkt->stream_index, pkt->size);
                int got_sub = 0;
                int dec_ret = avcodec_decode_subtitle2(tracks[ti].dec_ctx, &sub, &got_sub, pkt);
                if (debug_level > 0) fprintf(stderr, "Decode ret %d, got_sub %d (track %d)\n", dec_ret, got_sub, ti);
                if (dec_ret >= 0 && got_sub) {
                    subs_found++;
                    // Convert to Bitmap and encode immediately
                    Bitmap bm = {0};
                    if (sub.num_rects > 0) {
                        AVSubtitleRect *r = sub.rects[0];
                        int orig_x = r->x;
                        int orig_y = r->y;
                        int orig_w = r->w;
                        int orig_h = r->h;
                        // infer source bitmap full width from linesize when available
                        int src_image_w = r->w;
                        if (r->data[1] == NULL && r->data[0]) {
                            if (r->linesize[0] > 0) src_image_w = r->linesize[0] / 4; // RGBA stride
                        } else {
                            if (r->linesize[0] > 0) src_image_w = r->linesize[0]; // indexed stride
                        }
                        // compute integer scale: prefer codec_w / src_image_w, else codec_w / video_w
                        int scale = 1;
                        if (tracks[ti].codec_ctx) {
                            int codec_w = tracks[ti].codec_ctx->width;
                            // If codec target is UHD and subtitle likely comes from 1080p (linesize absent or small)
                            if (codec_w >= 3840 && (src_image_w <= 1920)) {
                                if (1920 > 0 && codec_w % 1920 == 0)
                                    scale = codec_w / 1920; // typical 1920 -> 3840 => 2
                            } else if (src_image_w > 0 && codec_w >= src_image_w && codec_w % src_image_w == 0) {
                                scale = codec_w / src_image_w;
                            } else if (video_w > 0 && codec_w >= video_w && codec_w % video_w == 0) {
                                scale = codec_w / video_w;
                            }
                        }
                        if (debug_level >= 2) {
                            fprintf(stderr, "[dvb-debug] decoded rect: type=%d w=%d h=%d x=%d y=%d nb_colors=%d data0=%p data1=%p linesize0=%d linesize1=%d\n",
                                    r->type, r->w, r->h, r->x, r->y, r->nb_colors, r->data[0], r->data[1], r->linesize[0], r->linesize[1]);
                        }
                        if (r->data[1] == NULL && r->data[0]) {
                            uint8_t *idxbuf;
                            uint32_t *palette;
                            int nb_colors;
                            if (scale <= 1) {
                                rgba_to_indexed(r->data[0], r->linesize[0], r->w, r->h, &idxbuf, &palette, &nb_colors);
                                bm.w = r->w;
                                bm.h = r->h;
                                bm.x = r->x;
                                bm.y = r->y;
                            } else {
                                int dst_w = r->w * scale;
                                int dst_h = r->h * scale;
                                uint8_t *resized = rgba_resize_nn(r->data[0], r->linesize[0], r->w, r->h, dst_w, dst_h);
                                if (resized) {
                                    rgba_to_indexed(resized, dst_w * 4, dst_w, dst_h, &idxbuf, &palette, &nb_colors);
                                    av_free(resized);
                                    bm.w = dst_w;
                                    bm.h = dst_h;
                                    bm.x = r->x * scale;
                                    bm.y = r->y * scale;
                                } else {
                                    rgba_to_indexed(r->data[0], r->linesize[0], r->w, r->h, &idxbuf, &palette, &nb_colors);
                                    bm.w = r->w;
                                    bm.h = r->h;
                                    bm.x = r->x;
                                    bm.y = r->y;
                                }
                            }
                            bm.idxbuf = idxbuf;
                            bm.idxbuf_len = (size_t)bm.w * (size_t)bm.h;
                            bm.palette = palette;
                            bm.nb_colors = nb_colors;
                            bm.palette_bytes = (size_t)bm.nb_colors * sizeof(uint32_t);
                            if (debug_level > 0) {
                        int codec_w = tracks[ti].codec_ctx ? tracks[ti].codec_ctx->width : 0;
                        int codec_h = tracks[ti].codec_ctx ? tracks[ti].codec_ctx->height : 0;
                        fprintf(stderr, "[dvb-coords] decode track=%d orig=(x=%d,y=%d,w=%d,h=%d) src_w=%d scale=%d final=(x=%d,y=%d,w=%d,h=%d) codec=(%d,%d)\n",
                            ti, orig_x, orig_y, orig_w, orig_h, src_image_w, scale, bm.x, bm.y, bm.w, bm.h, codec_w, codec_h);
                                        char fn[PATH_MAX];
                                        snprintf(fn, sizeof(fn), "pngs/dvb_debug_%03d.png", __dbg_png_seq++);
                                        save_bitmap_png(&bm, fn);
                                        /* record last png for later reporting */
                                        char *last_png = strdup(fn);
                                        if (last_png) {
                                            /* store pointer in bm structure temporarily via bm.palette (safe short-term) */
                                            /* We'll pass a separate pointer via a small local variable below when encoding */
                                        }
                                    }
                        } else {
                            // scale already computed above
                            if (scale <= 1) {
                                bm.idxbuf = av_malloc(r->w * r->h);
                                bm.idxbuf_len = (size_t)r->w * (size_t)r->h;
                                uint8_t *dst = bm.idxbuf;
                                uint8_t *src = (uint8_t*)r->data[0];
                                int src_stride = r->linesize[0] ? r->linesize[0] : r->w;
                                for (int y = 0; y < r->h; y++) {
                                    memcpy(dst, src, r->w);
                                    dst += r->w;
                                    src += src_stride;
                                }
                                if (r->data[1]) {
                                    int palette_entries = r->nb_colors ? r->nb_colors : (r->linesize[1] ? (r->linesize[1] / 4) : 16);
                                    if (palette_entries <= 0) palette_entries = 16;
                                    bm.nb_colors = palette_entries;
                                    bm.palette = av_mallocz(bm.nb_colors * sizeof(uint32_t));
                                    memcpy(bm.palette, r->data[1], bm.nb_colors * sizeof(uint32_t));
                                    bm.palette_bytes = (size_t)bm.nb_colors * sizeof(uint32_t);
                                } else {
                                    bm.nb_colors = r->nb_colors ? r->nb_colors : 16;
                                    bm.palette = av_mallocz(bm.nb_colors * sizeof(uint32_t));
                                    /* record length of allocated palette so callers can validate copies */
                                    bm.palette_bytes = (size_t)bm.nb_colors * sizeof(uint32_t);
                                }
                                bm.w = r->w;
                                bm.h = r->h;
                                bm.x = r->x;
                                bm.y = r->y;
                            } else {
                                int dst_w = r->w * scale;
                                int dst_h = r->h * scale;
                                uint8_t *src_idx = av_malloc(r->w * r->h);
                                uint8_t *dst_idx = NULL;
                                uint8_t *src = (uint8_t*)r->data[0];
                                int src_stride = r->linesize[0] ? r->linesize[0] : r->w;
                                for (int y = 0; y < r->h; y++) {
                                    memcpy(src_idx + y * r->w, src, r->w);
                                    src += src_stride;
                                }
                                dst_idx = indexed_resize_nn(src_idx, r->w, r->h, dst_w, dst_h);
                                av_free(src_idx);
                                if (!dst_idx) {
                                    // fallback: copy as-is
                                    bm.idxbuf = av_malloc(r->w * r->h);
                                    bm.idxbuf_len = (size_t)r->w * (size_t)r->h;
                                    uint8_t *dst2 = bm.idxbuf;
                                    uint8_t *src2 = (uint8_t*)r->data[0];
                                    int src_stride2 = r->linesize[0] ? r->linesize[0] : r->w;
                                    for (int y = 0; y < r->h; y++) {
                                        memcpy(dst2, src2, r->w);
                                        dst2 += r->w;
                                        src2 += src_stride2;
                                    }
                                    if (r->data[1]) {
                                        int palette_entries = r->nb_colors ? r->nb_colors : (r->linesize[1] ? (r->linesize[1] / 4) : 16);
                                        if (palette_entries <= 0) palette_entries = 16;
                                        bm.nb_colors = palette_entries;
                                        bm.palette = av_malloc(bm.nb_colors * sizeof(uint32_t));
                                        memcpy(bm.palette, r->data[1], bm.nb_colors * sizeof(uint32_t));
                                        bm.palette_bytes = (size_t)bm.nb_colors * sizeof(uint32_t);
                                    } else {
                                        bm.nb_colors = r->nb_colors ? r->nb_colors : 16;
                                        bm.palette = av_mallocz(bm.nb_colors * sizeof(uint32_t));
                                    }
                                    bm.w = r->w;
                                    bm.h = r->h;
                                    bm.x = r->x;
                                    bm.y = r->y;
                                } else {
                                    bm.idxbuf = dst_idx;
                                    bm.idxbuf_len = (size_t)dst_w * (size_t)dst_h;
                                    if (r->data[1]) {
                                        int palette_entries = r->nb_colors ? r->nb_colors : (r->linesize[1] ? (r->linesize[1] / 4) : 16);
                                        if (palette_entries <= 0) palette_entries = 16;
                                        bm.nb_colors = palette_entries;
                                        bm.palette = av_mallocz(bm.nb_colors * sizeof(uint32_t));
                                        memcpy(bm.palette, r->data[1], bm.nb_colors * sizeof(uint32_t));
                                        bm.palette_bytes = (size_t)bm.nb_colors * sizeof(uint32_t);
                                    } else {
                                        bm.nb_colors = r->nb_colors ? r->nb_colors : 16;
                                        bm.palette = av_mallocz(bm.nb_colors * sizeof(uint32_t));
                                    }
                                    bm.w = dst_w;
                                    bm.h = dst_h;
                                    bm.x = r->x * scale;
                                    bm.y = r->y * scale;
                                }
                            }
                            if (debug_level > 0) {
                int codec_w = tracks[ti].codec_ctx ? tracks[ti].codec_ctx->width : 0;
                int codec_h = tracks[ti].codec_ctx ? tracks[ti].codec_ctx->height : 0;
                fprintf(stderr, "[dvb-coords] decode track=%d orig=(x=%d,y=%d,w=%d,h=%d) src_w=%d scale=%d final=(x=%d,y=%d,w=%d,h=%d) codec=(%d,%d)\n",
                    ti, orig_x, orig_y, orig_w, orig_h, src_image_w, scale, bm.x, bm.y, bm.w, bm.h, codec_w, codec_h);
                                char fn[PATH_MAX];
                                snprintf(fn, sizeof(fn), "pngs/dvb_debug_%03d.png", __dbg_png_seq++);
                                save_bitmap_png(&bm, fn);
                                char *last_png = strdup(fn);
                                (void)last_png;
                            }
                        }
                    }

                    if (debug_level >= 2) {
                        fprintf(stderr, "[dvb-debug] Bitmap: w=%d h=%d x=%d y=%d nb_colors=%d idxbuf=%p palette=%p\n",
                                bm.w, bm.h, bm.x, bm.y, bm.nb_colors, (void*)bm.idxbuf, (void*)bm.palette);
                        if (bm.idxbuf && bm.w * bm.h > 0) {
                            int samples = bm.w * bm.h > 8 ? 8 : bm.w * bm.h;
                            fprintf(stderr, "[dvb-debug] idxbuf first %d samples:", samples);
                            for (int si = 0; si < samples; si++) fprintf(stderr, " %d", bm.idxbuf[si]);
                            fprintf(stderr, "\n");
                        }
                        if (bm.palette && bm.nb_colors > 0) {
                            int pc = bm.nb_colors > 8 ? 8 : bm.nb_colors;
                            fprintf(stderr, "[dvb-debug] palette first %d entries:\n", pc);
                            for (int pi=0; pi<pc; pi++) fprintf(stderr, "%08x ", bm.palette[pi]);
                            fprintf(stderr, "\n");
                        }
                    }
                    AVSubtitle *dvb_sub = make_subtitle(bm, sub.start_display_time, sub.end_display_time);
                    if (debug_level >= 2 && dvb_sub && dvb_sub->num_rects > 0) {
                        AVSubtitleRect *r = dvb_sub->rects[0];
                        fprintf(stderr, "[dvb-debug] dvb_sub rect: nb_colors=%d\n", r->nb_colors);
                        if (r->data[1]) {
                            uint32_t *pal = (uint32_t*)r->data[1];
                            int pc = r->nb_colors > 8 ? 8 : r->nb_colors;
                            fprintf(stderr, "[dvb-debug] dvb palette first %d entries:\n", pc);
                            for (int pi=0; pi<pc; pi++) fprintf(stderr, "%08x ", pal[pi]);
                            fprintf(stderr, "\n");
                        }
                    }
                    int64_t pts90 = 0;
                    // Prefer subtitle internal PTS if present (AVSubtitle.pts is in AV_TIME_BASE units)
                    if (sub.pts != AV_NOPTS_VALUE && sub.pts != 0) {
                        pts90 = av_rescale_q(sub.pts, AV_TIME_BASE_Q, (AVRational){1,90000});
                        if (debug_level > 0) fprintf(stderr, "used sub.pts=%lld -> pts90=%lld\n", (long long)sub.pts, (long long)pts90);
                    } else if (pkt->pts != AV_NOPTS_VALUE) {
                        pts90 = av_rescale_q(pkt->pts, in_fmt->streams[pkt->stream_index]->time_base, (AVRational){1,90000});
                        if (debug_level > 0) fprintf(stderr, "used pkt.pts=%lld -> pts90=%lld\n", (long long)pkt->pts, (long long)pts90);
                    } else {
                        if (debug_level > 0) fprintf(stderr, "no pts available in pkt or sub; using last_pts fallback\n");
                        if (tracks[ti].last_pts != AV_NOPTS_VALUE) pts90 = tracks[ti].last_pts + 90;
                    }

                    // Record first subtitle PTS for this track (before any rebasing)
                    if (tracks[ti].first_subtitle_pts90 == AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE) {
                        tracks[ti].first_subtitle_pts90 = av_rescale_q(pkt->pts,
                                                                      in_fmt->streams[pkt->stream_index]->time_base,
                                                                      (AVRational){1,90000});
                        if (debug_level > 0) fprintf(stderr, "first_subtitle_pts90(track %d)=%lld\n", ti, (long long)tracks[ti].first_subtitle_pts90);
                    }

                    // Use absolute PTS for subtitles, but apply frame rate scaling
                    if (dst_fps > 0.0 && src_fps > 0.0) {
                        double scale = src_fps / dst_fps;
                        pts90 = (int64_t)llround((double)pts90 * scale);
                        if (debug_level > 0) fprintf(stderr, "Scaled pts90 by %f -> %lld\n", scale, (long long)pts90);
                    }
                    pts90 += (tracks[ti].effective_delay_ms * 90);
                    if (debug_level > 0) fprintf(stderr, "Encoding immediate event for track %d at pts %lld\n", ti, (long long)pts90);
                    encode_and_write_subtitle(tracks[ti].codec_ctx, out_fmt, &tracks[ti], dvb_sub, pts90, bench_mode, NULL);
                    // update immediate progress after writing
                    if ((pkt_count & 0x3f) == 0) {
                        double elapsed = difftime(time(NULL), prog_start_time);
                        int mins = (int)(elapsed / 60.0);
                        int secs = (int)(elapsed) % 60;
                        fprintf(stdout, "\rProgress: subs=%ld elapsed=%02d:%02d   ", subs_found, mins, secs);
                        fflush(stdout);
                    }
                    /* free encoded subtitle internal buffers then the struct */
                    free_subtitle(&dvb_sub);
                    /* free temporary bitmap buffers we allocated during conversion */
                    if (bm.idxbuf) av_free(bm.idxbuf);
                    if (bm.palette) av_free(bm.palette);
                    avsubtitle_free(&sub);
                }
            }
        }
        av_packet_unref(pkt);
    }

    // Flush decoders for remaining subtitles
    for (int ti = 0; ti < ntracks; ti++) {
    AVSubtitle flush_sub;
    int got_sub = 0;
    /* Some libavcodec versions crash if NULL is passed here; pass an empty packet instead */
    AVPacket empty_pkt;
    memset(&empty_pkt, 0, sizeof(empty_pkt));
    /* ensure data/size are explicit */
    empty_pkt.data = NULL;
    empty_pkt.size = 0;
    int dec_ret = avcodec_decode_subtitle2(tracks[ti].dec_ctx, &flush_sub, &got_sub, &empty_pkt);
        if (dec_ret >= 0 && got_sub) {
            Bitmap bm = {0};
            if (flush_sub.num_rects > 0) {
                AVSubtitleRect *r = flush_sub.rects[0];
                int orig_x = r->x;
                int orig_y = r->y;
                int orig_w = r->w;
                int orig_h = r->h;
                int src_image_w = r->w;
                if (r->data[1] == NULL && r->data[0]) {
                    if (r->linesize[0] > 0) src_image_w = r->linesize[0] / 4;
                } else {
                    if (r->linesize[0] > 0) src_image_w = r->linesize[0];
                }
                int scale = 1;
                if (tracks[ti].codec_ctx) {
                    int codec_w = tracks[ti].codec_ctx->width;
                    if (codec_w >= 3840 && (src_image_w <= 1920)) {
                        if (1920 > 0 && codec_w % 1920 == 0)
                            scale = codec_w / 1920;
                    } else if (src_image_w > 0 && codec_w >= src_image_w && codec_w % src_image_w == 0) {
                        scale = codec_w / src_image_w;
                    } else if (video_w > 0 && codec_w >= video_w && codec_w % video_w == 0) {
                        scale = codec_w / video_w;
                    }
                }
                if (r->data[1] == NULL && r->data[0]) {
                    uint8_t *idxbuf;
                    uint32_t *palette;
                    int nb_colors;
                    scale = 1;
                    if (tracks[ti].codec_ctx && tracks[ti].codec_ctx->width >= 3840 && r->w <= 1920)
                        scale = tracks[ti].codec_ctx->width / r->w;
                    if (scale <= 1) {
                        rgba_to_indexed(r->data[0], r->linesize[0], r->w, r->h, &idxbuf, &palette, &nb_colors);
                        bm.w = r->w;
                        bm.h = r->h;
                        bm.x = r->x;
                        bm.y = r->y;
                    } else {
                        int dst_w = r->w * scale;
                        int dst_h = r->h * scale;
                        uint8_t *resized = rgba_resize_nn(r->data[0], r->linesize[0], r->w, r->h, dst_w, dst_h);
                        if (resized) {
                            rgba_to_indexed(resized, dst_w * 4, dst_w, dst_h, &idxbuf, &palette, &nb_colors);
                            av_free(resized);
                            bm.w = dst_w;
                            bm.h = dst_h;
                            bm.x = r->x * scale;
                            bm.y = r->y * scale;
                        } else {
                            rgba_to_indexed(r->data[0], r->linesize[0], r->w, r->h, &idxbuf, &palette, &nb_colors);
                            bm.w = r->w;
                            bm.h = r->h;
                            bm.x = r->x;
                            bm.y = r->y;
                        }
                    }
                    bm.idxbuf = idxbuf;
                    bm.idxbuf_len = (size_t)bm.w * (size_t)bm.h;
                    bm.palette = palette;
                        bm.nb_colors = nb_colors;
                    bm.palette_bytes = (size_t)bm.nb_colors * sizeof(uint32_t);
                    if (debug_level > 0) {
            int codec_w = tracks[ti].codec_ctx ? tracks[ti].codec_ctx->width : 0;
            int codec_h = tracks[ti].codec_ctx ? tracks[ti].codec_ctx->height : 0;
            fprintf(stderr, "[dvb-coords] flush track=%d orig=(x=%d,y=%d,w=%d,h=%d) src_w=%d scale=%d final=(x=%d,y=%d,w=%d,h=%d) codec=(%d,%d)\n",
                ti, orig_x, orig_y, orig_w, orig_h, src_image_w, scale, bm.x, bm.y, bm.w, bm.h, codec_w, codec_h);
                    }
                } else {
                    scale = 1;
                    if (tracks[ti].codec_ctx && tracks[ti].codec_ctx->width >= 3840 && r->w <= 1920)
                        scale = tracks[ti].codec_ctx->width / r->w;
                    if (scale <= 1) {
                        bm.idxbuf = av_malloc(r->w * r->h);
                        bm.idxbuf_len = (size_t)r->w * (size_t)r->h;
                        uint8_t *dst = bm.idxbuf;
                        uint8_t *src = (uint8_t*)r->data[0];
                        int src_stride = r->linesize[0] ? r->linesize[0] : r->w;
                        for (int y = 0; y < r->h; y++) {
                            memcpy(dst, src, r->w);
                            dst += r->w;
                            src += src_stride;
                        }
                        if (r->data[1]) {
                                int palette_entries = r->nb_colors ? r->nb_colors : (r->linesize[1] ? (r->linesize[1] / 4) : 16);
                                if (palette_entries <= 0) palette_entries = 16;
                                bm.nb_colors = palette_entries;
                                bm.palette = av_malloc(bm.nb_colors * sizeof(uint32_t));
                                memcpy(bm.palette, r->data[1], bm.nb_colors * sizeof(uint32_t));
                                bm.palette_bytes = (size_t)bm.nb_colors * sizeof(uint32_t);
                                // Clamp indices to valid range
                                for (int i = 0; i < r->w * r->h; i++) {
                                    if (bm.idxbuf[i] >= bm.nb_colors) bm.idxbuf[i] = 0;
                                }
                            } else {
                                bm.nb_colors = r->nb_colors ? r->nb_colors : 16;
                                bm.palette = av_mallocz(bm.nb_colors * sizeof(uint32_t));
                                /* record allocated palette length for later validation */
                                bm.palette_bytes = (size_t)bm.nb_colors * sizeof(uint32_t);
                            }
                        bm.w = r->w;
                        bm.h = r->h;
                        bm.x = r->x;
                        bm.y = r->y;
                    } else {
                        int dst_w = r->w * scale;
                        int dst_h = r->h * scale;
                        uint8_t *src_idx = av_malloc(r->w * r->h);
                        uint8_t *dst_idx = NULL;
                        uint8_t *src = (uint8_t*)r->data[0];
                        int src_stride = r->linesize[0] ? r->linesize[0] : r->w;
                        for (int y = 0; y < r->h; y++) {
                            memcpy(src_idx + y * r->w, src, r->w);
                            src += src_stride;
                        }
                        dst_idx = indexed_resize_nn(src_idx, r->w, r->h, dst_w, dst_h);
                        av_free(src_idx);
                        if (!dst_idx) {
                            bm.idxbuf = av_malloc(r->w * r->h);
                            bm.idxbuf_len = (size_t)r->w * (size_t)r->h;
                            uint8_t *dst2 = bm.idxbuf;
                            uint8_t *src2 = (uint8_t*)r->data[0];
                            int src_stride2 = r->linesize[0] ? r->linesize[0] : r->w;
                            for (int y = 0; y < r->h; y++) {
                                memcpy(dst2, src2, r->w);
                                dst2 += r->w;
                                src2 += src_stride2;
                            }
                            if (r->data[1]) {
                                int palette_entries = r->nb_colors ? r->nb_colors : (r->linesize[1] ? (r->linesize[1] / 4) : 16);
                                if (palette_entries <= 0) palette_entries = 16;
                                bm.nb_colors = palette_entries;
                                bm.palette = av_malloc(bm.nb_colors * sizeof(uint32_t));
                                memcpy(bm.palette, r->data[1], bm.nb_colors * sizeof(uint32_t));
                                bm.palette_bytes = (size_t)bm.nb_colors * sizeof(uint32_t);
                                for (int i = 0; i < r->w * r->h; i++) if (bm.idxbuf[i] >= bm.nb_colors) bm.idxbuf[i] = 0;
                            } else {
                                bm.nb_colors = r->nb_colors ? r->nb_colors : 16;
                                bm.palette = av_mallocz(bm.nb_colors * sizeof(uint32_t));
                                /* ensure palette_bytes is recorded for zeroed palette */
                                bm.palette_bytes = (size_t)bm.nb_colors * sizeof(uint32_t);
                            }
                            bm.w = r->w;
                            bm.h = r->h;
                            bm.x = r->x;
                            bm.y = r->y;
                        } else {
                                bm.idxbuf = dst_idx;
                                bm.idxbuf_len = (size_t)dst_w * (size_t)dst_h;
                            if (r->data[1]) {
                                int palette_entries = r->nb_colors ? r->nb_colors : (r->linesize[1] ? (r->linesize[1] / 4) : 16);
                                if (palette_entries <= 0) palette_entries = 16;
                                bm.nb_colors = palette_entries;
                                bm.palette = av_malloc(bm.nb_colors * sizeof(uint32_t));
                                memcpy(bm.palette, r->data[1], bm.nb_colors * sizeof(uint32_t));
                                bm.palette_bytes = (size_t)bm.nb_colors * sizeof(uint32_t);
                            } else {
                                bm.nb_colors = r->nb_colors ? r->nb_colors : 16;
                                bm.palette = av_mallocz(bm.nb_colors * sizeof(uint32_t));
                            }
                            bm.w = dst_w;
                            bm.h = dst_h;
                            bm.x = r->x * scale;
                            bm.y = r->y * scale;
                        }
                    }
                }
            }
            if (debug_level >= 2) {
                fprintf(stderr, "[dvb-debug] (flush) Bitmap: w=%d h=%d x=%d y=%d nb_colors=%d idxbuf=%p palette=%p\n",
                        bm.w, bm.h, bm.x, bm.y, bm.nb_colors, (void*)bm.idxbuf, (void*)bm.palette);
                if (bm.palette && bm.nb_colors > 0) {
                    int pc = bm.nb_colors > 8 ? 8 : bm.nb_colors;
                    fprintf(stderr, "[dvb-debug] (flush) palette first %d entries:\n", pc);
                    for (int pi=0; pi<pc; pi++) fprintf(stderr, "%08x ", bm.palette[pi]);
                    fprintf(stderr, "\n");
                }
            }
            AVSubtitle *dvb_sub = make_subtitle(bm, flush_sub.start_display_time, flush_sub.end_display_time);
                int64_t pts90 = 0;
                if (flush_sub.pts != AV_NOPTS_VALUE && flush_sub.pts != 0) {
                    pts90 = av_rescale_q(flush_sub.pts, AV_TIME_BASE_Q, (AVRational){1,90000});
                } else if (tracks[ti].last_pts != AV_NOPTS_VALUE) {
                    pts90 = tracks[ti].last_pts + 90; // bump forward
                }
                // Use absolute PTS for flush subtitles, but apply frame rate scaling
                if (dst_fps > 0.0 && src_fps > 0.0) {
                    double scale = src_fps / dst_fps;
                    pts90 = (int64_t)llround((double)pts90 * scale);
                    if (debug_level > 0) fprintf(stderr, "Scaled flush pts90 by %f -> %lld\n", scale, (long long)pts90);
                }
                pts90 += (tracks[ti].effective_delay_ms * 90);
            encode_and_write_subtitle(tracks[ti].codec_ctx, out_fmt, &tracks[ti], dvb_sub, pts90, bench_mode, NULL);
            free_subtitle(&dvb_sub);
            if (bm.idxbuf) av_free(bm.idxbuf);
            if (bm.palette) av_free(bm.palette);
            avsubtitle_free(&flush_sub);
        }
    }

    av_write_trailer(out_fmt);
    ret = 0;
cleanup:
    if (pkt) {
        av_packet_free(&pkt);
        pkt = NULL;
    }
    for (int t = 0; t < ntracks; t++) {
        graphic_subtrack_clear(&tracks[t]);
    }
    if (out_fmt) {
        if (out_fmt->pb) avio_closep(&out_fmt->pb);
        avformat_free_context(out_fmt);
        out_fmt = NULL;
    }
    if (in_fmt) {
        avformat_close_input(&in_fmt);
        in_fmt = NULL;
    }
    avformat_network_deinit();
    if (qc) {
        fclose(qc);
        qc = NULL;
    }
    if (srt_list) { free(srt_list); srt_list = NULL; }
    if (lang_list) { free(lang_list); lang_list = NULL; }
    if (ret == 0 && bench_mode) bench_report();
    if (ret == 0) {
        fprintf(stdout, "\n");
        fflush(stdout);
    }
    return ret;
}
#undef FAIL

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <getopt.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include "srt_parser.h"
#include "render_pango.h"
#include "dvb_sub.h"
#include "qc.h"
#include "bench.h"
#include "debug_png.h"

typedef struct {
    SRTEntry *entries;
    int count;
    int cur_sub;
    AVStream *stream;
    AVCodecContext *codec_ctx;   // per-track encoder
    const char *lang;
    const char *filename;
    int forced;
    int hi;
} SubTrack;

int debug_level = 0; // Global debug level

#if LIBAVCODEC_VERSION_MAJOR >= 64
#define HAVE_SEND_SUBTITLE 1
#endif

// Wrapper for encoding and muxing a DVB subtitle
static void encode_and_write_subtitle(AVCodecContext *ctx, AVFormatContext *out_fmt,
                                      SubTrack *track, AVSubtitle *sub, int64_t pts90,
                                      int bench_mode)
{
#if HAVE_SEND_SUBTITLE
    int64_t t_enc = bench_now();
    avcodec_send_subtitle(ctx, sub, NULL);
    AVPacket *pkt = av_packet_alloc();
    while (avcodec_receive_packet(ctx, pkt) == 0) {
        pkt->stream_index = track->stream->index;
        pkt->pts = pts90;
        pkt->dts = pkt->pts;
        int64_t t0 = bench_now();
        av_interleaved_write_frame(out_fmt, pkt);
        if (bench_mode) {
            bench.t_mux_us += bench_now() - t0;
            bench.t_encode_us += bench_now() - t_enc;
            bench.cues_encoded++;
            bench.packets_muxed++;
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
#else
    #define SUB_BUF_SIZE 65536
    uint8_t *buf = av_malloc(SUB_BUF_SIZE);
    if (!buf) return;

    int64_t t_enc = bench_now();
    int size = avcodec_encode_subtitle(ctx, buf, SUB_BUF_SIZE, sub);
    if (bench_mode) bench.t_encode_us += bench_now() - t_enc;

    if (size > 0) {
        AVPacket *pkt = av_packet_alloc();
        pkt->data = buf;
        pkt->size = size;
        pkt->stream_index = track->stream->index;
        pkt->pts = pts90;
        pkt->dts = pkt->pts;
        int64_t t0 = bench_now();
        av_interleaved_write_frame(out_fmt, pkt);
        if (bench_mode) {
            bench.t_mux_us += bench_now() - t0;
            bench.cues_encoded++;
            bench.packets_muxed++;
        }
        av_packet_free(&pkt);
    } else {
        av_free(buf);
    }
#endif
}

int main(int argc,char**argv){
    const char *input=NULL, *output=NULL;
    char *srt_list=NULL, *lang_list=NULL;
    int forced=0, hi=0, qc_only=0, bench_mode=0;
    const char *palette_mode="broadcast";  // default palette

    static struct option long_opts[] = {
        {"input",     required_argument, 0, 'I'},
        {"output",    required_argument, 0, 'o'},
        {"srt",       required_argument, 0, 's'},
        {"languages", required_argument, 0, 'l'},
        {"forced",    no_argument,       0, 1000},
        {"hi",        no_argument,       0, 1001},
        {"debug",     required_argument, 0, 1002},
        {"qc-only",   no_argument,       0, 1003},
        {"bench",     no_argument,       0, 1004},
        {"palette",   required_argument, 0, 1005},
        {0,0,0,0}
    };

    int opt,long_index=0;
    while ((opt=getopt_long(argc,argv,"I:o:s:l:",long_opts,&long_index))!=-1) {
        switch(opt) {
        case 'I': input=optarg; break;
        case 'o': output=optarg; break;
        case 's': srt_list=strdup(optarg); break;
        case 'l': lang_list=strdup(optarg); break;
        case 1000: forced=1; break;
        case 1001: hi=1; break;
        case 1002: debug_level=atoi(optarg); break;
        case 1003: qc_only=1; break;
        case 1004: bench_mode=1; bench.enabled=1; break;
        case 1005: palette_mode=strdup(optarg); break;
        default:
            fprintf(stderr,
                "Usage: mux_srt_dvb --input in.mp4 --output out.ts "
                "--srt subs1.srt[,subs2.srt] --languages eng[,deu] "
                "[--palette broadcast|greyscale|ebu-broadcast] [options]\n");
            return 1;
        }
    }

    if(!input||!output||!srt_list||!lang_list){
        fprintf(stderr,"Error: --input, --output, --srt, and --languages are required\n");
        return 1;
    }

    if (debug_level > 1)
        av_log_set_level(AV_LOG_INFO);
    else if (debug_level == 1)
        av_log_set_level(AV_LOG_ERROR);
    else
        av_log_set_level(AV_LOG_QUIET);

    bench_start();

    FILE *qc=fopen("qc_log.txt","w");
    if(!qc){ perror("qc_log.txt"); return 1; }

    // ---------- QC-only ----------
    if (qc_only) {
        char *tok=strtok(srt_list,",");
        char *tok_lang=strtok(lang_list,",");
        while(tok && tok_lang){
            SRTEntry *entries=NULL;
            int64_t t0=bench_now();
            int count=parse_srt(tok,&entries,qc);
            if(bench_mode) bench.t_parse_us+=bench_now()-t0;
            if(debug_level>0)
                printf("QC-only: %s (%s), cues=%d forced=%d hi=%d\n",
                       tok,tok_lang,count,forced,hi);
            for(int j=0;j<count;j++) free(entries[j].text);
            free(entries);
            tok=strtok(NULL,",");
            tok_lang=strtok(NULL,",");
        }
        fclose(qc);
        if(bench_mode) bench_report();
        return 0;
    }

    // ---------- Normal mux ----------
    avformat_network_init();
    AVFormatContext *in_fmt=NULL;
    if(avformat_open_input(&in_fmt,input,NULL,NULL)<0){
        fprintf(stderr,"Cannot open input\n"); return -1;
    }
    avformat_find_stream_info(in_fmt,NULL);

    int video_w=720, video_h=576;
    for(unsigned i=0;i<in_fmt->nb_streams;i++){
        if(in_fmt->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO){
            video_w=in_fmt->streams[i]->codecpar->width;
            video_h=in_fmt->streams[i]->codecpar->height;
            break;
        }
    }

    AVFormatContext *out_fmt=NULL;
    if(avformat_alloc_output_context2(&out_fmt,NULL,"mpegts",output)<0){
        fprintf(stderr,"Cannot alloc out_fmt\n"); return -1;
    }
    for(unsigned i=0;i<in_fmt->nb_streams;i++){
        AVStream *in_st=in_fmt->streams[i];
        AVStream *out_st=avformat_new_stream(out_fmt,NULL);
        avcodec_parameters_copy(out_st->codecpar,in_st->codecpar);
    }

    int ntracks=0; SubTrack tracks[8];
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_DVB_SUBTITLE);

    char *tok=strtok(srt_list,",");
    char *tok_lang=strtok(lang_list,",");
    while(tok && tok_lang && ntracks<8){
        SRTEntry *entries=NULL;
        int64_t t0=bench_now();
        int count=parse_srt(tok,&entries,qc);
        if(bench_mode) bench.t_parse_us+=bench_now()-t0;

        if(count>0){
            tracks[ntracks].entries=entries;
            tracks[ntracks].count=count;
            tracks[ntracks].cur_sub=0;
            tracks[ntracks].lang=strdup(tok_lang);
            tracks[ntracks].filename=strdup(tok);
            tracks[ntracks].forced=forced;
            tracks[ntracks].hi=hi;
            tracks[ntracks].stream=avformat_new_stream(out_fmt,NULL);
            tracks[ntracks].stream->codecpar->codec_type=AVMEDIA_TYPE_SUBTITLE;
            tracks[ntracks].stream->codecpar->codec_id=AV_CODEC_ID_DVB_SUBTITLE;
            tracks[ntracks].stream->time_base=(AVRational){1,90000};
            av_dict_set(&tracks[ntracks].stream->metadata,"language",tok_lang,0);
            if(forced) av_dict_set(&tracks[ntracks].stream->metadata,"forced","1",0);
            if(hi) av_dict_set(&tracks[ntracks].stream->metadata,"hearing_impaired","1",0);

            // allocate encoder context per track
            tracks[ntracks].codec_ctx = avcodec_alloc_context3(codec);
            if (!tracks[ntracks].codec_ctx) {
                fprintf(stderr, "Failed to alloc codec context for track %s\n", tok);
                return -1;
            }
            tracks[ntracks].codec_ctx->time_base = (AVRational){1,90000};
            tracks[ntracks].codec_ctx->width  = video_w;
            tracks[ntracks].codec_ctx->height = video_h;

            if (avcodec_open2(tracks[ntracks].codec_ctx, codec, NULL) < 0) {
                fprintf(stderr, "Failed to open DVB subtitle encoder for track %s\n", tok);
                return -1;
            }

            ntracks++;
        }
        tok=strtok(NULL,",");
        tok_lang=strtok(NULL,",");
    }

    if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_fmt->pb, output, AVIO_FLAG_WRITE) < 0) {
            fprintf(stderr, "Error: could not open output file %s\n", output);
            return -1;
        }
    }

    #define PCR_BIAS_MS     600
    #define PCR_BIAS_TICKS  (PCR_BIAS_MS * 90)

    AVDictionary *mux_opts = NULL;
    //av_dict_set(&mux_opts, "muxrate", "1M", 0);        // force CBR-like pacing
    av_dict_set(&mux_opts, "max_delay", "700000", 0);  // ~700 ms PCR lead
    av_dict_set(&mux_opts, "copyts", "1", 0);
    av_dict_set(&mux_opts, "start_at_zero", "1", 0);

    char bias_str[32];
    snprintf(bias_str, sizeof(bias_str), "%d", PCR_BIAS_TICKS);
    
    av_dict_set(&mux_opts, "initial_offset", bias_str, 0);

    if (avformat_write_header(out_fmt, &mux_opts) < 0) {
        fprintf(stderr, "Error: could not write header for output file\n");
        return -1;
    }

    av_dict_free(&mux_opts);

    const AVCodec *codec=avcodec_find_encoder(AV_CODEC_ID_DVB_SUBTITLE);
    AVCodecContext *ctx=avcodec_alloc_context3(codec);
    ctx->time_base=(AVRational){1,90000};
    avcodec_open2(ctx,codec,NULL);

    ctx->width  = video_w;   // match video resolution
    ctx->height = video_h;

    AVPacket *pkt = av_packet_alloc();
    
    while (av_read_frame(in_fmt, pkt) >= 0) {

        // If PTS missing, fall back to DTS
        if (pkt->pts == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE) {
            pkt->pts = pkt->dts; // emulate +genpts
        }

        // Apply PCR bias to all streams (video/audio/subs)
        if (pkt->pts != AV_NOPTS_VALUE) pkt->pts += PCR_BIAS_TICKS;
        if (pkt->dts != AV_NOPTS_VALUE) pkt->dts += PCR_BIAS_TICKS;

        // Compute current 90kHz clock for subtitle scheduling
        int64_t cur90 = (pkt->pts == AV_NOPTS_VALUE) ? AV_NOPTS_VALUE :
                        av_rescale_q(pkt->pts,
                                     in_fmt->streams[pkt->stream_index]->time_base,
                                     (AVRational){1,90000});

        for (int t = 0; t < ntracks; t++) {
            while (tracks[t].cur_sub < tracks[t].count &&
                  (tracks[t].entries[tracks[t].cur_sub].start_ms * 90 + PCR_BIAS_TICKS) <= cur90) {

                // --- Render bitmap from SRT cue
                char *markup = srt_to_pango_markup(tracks[t].entries[tracks[t].cur_sub].text);

                int64_t t1 = bench_now();
                         
                Bitmap bm = render_text_pango(markup, video_w, video_h,
                                            video_h/18, "DejaVu Sans",
                                            1, 1,
                                            tracks[t].entries[tracks[t].cur_sub].alignment,
                                            palette_mode);

                // Dump CLUT for debugging
                if (debug_level > 0 && bm.palette) {
                    printf("CLUT (%s):\n", palette_mode);
                    for (int ci=0; ci<16; ci++) {
                        printf("  [%2d] = 0x%08X\n", ci, bm.palette[ci]);
                    }
                }                           
                
                if (bench_mode) { bench.t_render_us += bench_now() - t1; bench.cues_rendered++; }
                free(markup);

                // --- Show subtitle at start_ms
                AVSubtitle *sub = make_subtitle(bm,
                                                tracks[t].entries[tracks[t].cur_sub].start_ms,
                                                tracks[t].entries[tracks[t].cur_sub].end_ms);
                if (sub) {
                    sub->start_display_time = 0;
                    sub->end_display_time   = 0;
                    encode_and_write_subtitle(ctx, out_fmt, &tracks[t],
                                            sub,
                                            (tracks[t].entries[tracks[t].cur_sub].start_ms * 90) + PCR_BIAS_TICKS,
                                            bench_mode);

                    printf("Injected sub cue %d on track %s\n",
                        tracks[t].cur_sub,
                        tracks[t].filename);

                    avsubtitle_free(sub);
                }

                // --- Send clear subtitle at end_ms
                AVSubtitle *clr = make_subtitle((Bitmap){0},
                                                tracks[t].entries[tracks[t].cur_sub].end_ms,
                                                tracks[t].entries[tracks[t].cur_sub].end_ms + 1);
                if (clr) {
                    clr->num_rects = 0; // mark as clear
                    clr->start_display_time = 0;
                    clr->end_display_time   = 0;
                    encode_and_write_subtitle(ctx, out_fmt, &tracks[t],
                                            clr,
                                            (tracks[t].entries[tracks[t].cur_sub].end_ms * 90) + PCR_BIAS_TICKS,
                                            bench_mode);
                    avsubtitle_free(clr);
                }

                tracks[t].cur_sub++;
            }
        }

        // Remap AV packet to output stream
        AVStream *out_st = out_fmt->streams[pkt->stream_index];
        pkt->stream_index = out_st->index;

        int64_t t5 = bench_now();
        av_interleaved_write_frame(out_fmt, pkt);
        if (bench_mode) {
            bench.t_mux_us += bench_now() - t5;
            bench.packets_muxed++;
        }
        av_packet_unref(pkt);
    }

    av_write_trailer(out_fmt);
    avcodec_free_context(&ctx);
    avformat_free_context(out_fmt);
    avformat_close_input(&in_fmt);
    fclose(qc);
    av_packet_free(&pkt);

    if(bench_mode) bench_report();
    return 0;
}
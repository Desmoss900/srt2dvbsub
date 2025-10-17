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
#include "render_ass.h"
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
    ASS_Track *ass_track;        // libass track if --ass enabled
    const char *lang;
    const char *filename;
    int forced;
    int hi;
    int64_t last_pts;            // last emitted pts for this track (for monotonicity)
    int effective_delay_ms;   // per-track (audio-matched) delay + CLI fine-tune
} SubTrack;

int debug_level = 0; // Global debug level
int use_ass = 0;
int video_w=720, video_h=480; //Set the min video width and height...this will be overridden by libav probe

#if LIBAVCODEC_VERSION_MAJOR >= 64
#define HAVE_SEND_SUBTITLE 1
#endif

// Wrapper for encoding and muxing a DVB subtitle
static void encode_and_write_subtitle(AVCodecContext *ctx,
                                      AVFormatContext *out_fmt,
                                      SubTrack *track,
                                      AVSubtitle *sub,
                                      int64_t pts90,
                                      int bench_mode)
{
    if (!sub) {
        if (debug_level > 2) {
            fprintf(stderr, "Skipping empty/bad subtitle event\n");
        }
        return;
    }

#if HAVE_SEND_SUBTITLE
    int64_t t_enc = bench_now();
    if (avcodec_send_subtitle(ctx, sub, NULL) < 0) {
        fprintf(stderr, "Failed to send subtitle to encoder\n");
        return;
    }

    AVPacket *pkt = av_packet_alloc();
    while (avcodec_receive_packet(ctx, pkt) == 0) {
        pkt->stream_index = track->stream->index;
        int64_t adj_pts = pts90;
        if (track->last_pts != AV_NOPTS_VALUE && adj_pts <= track->last_pts) {
            adj_pts = track->last_pts + 90; // bump 1ms forward per track
        }
        pkt->pts = adj_pts;
        pkt->dts = adj_pts;
        track->last_pts = adj_pts;
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

        // Per-track last_pts to ensure monotonicity per subtitle track
        if (track->last_pts != AV_NOPTS_VALUE && pts90 <= track->last_pts) {
            pts90 = track->last_pts + 90; // bump 1ms forward to avoid non-monotonic DTS
        }
        pkt->pts = pts90;
        pkt->dts = pts90;
        track->last_pts = pts90;

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
    int subtitle_delay_ms=0;      // user-supplied fine-tune

    // Subtitle style config (shared for ASS and Pango)
    const char *cli_font        = "Robooto";
    int cli_fontsize            = 24;
    const char *cli_fgcolor     = "#FFFFFF";    // hex for Pango
    const char *cli_outlinecolor= "#000000";
    const char *cli_shadowcolor = "#64000000";

    ASS_Library *ass_lib=NULL;
    ASS_Renderer *ass_renderer=NULL;
    

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
        {"ass",       no_argument,       0, 1006},
        {"font",      required_argument, 0, 1007},
        {"fontsize",  required_argument, 0, 1008},
        {"fgcolor",   required_argument, 0, 1009},
        {"outlinecolor", required_argument, 0, 1010},
        {"shadowcolor",  required_argument, 0, 1011},
        {"delay",     required_argument, 0, 1012},        
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
        case 1006: use_ass=1; break;
        case 1007: cli_font = strdup(optarg); break;
        case 1008: cli_fontsize = atoi(optarg); break;
        case 1009: cli_fgcolor = strdup(optarg); break;
        case 1010: cli_outlinecolor = strdup(optarg); break;
        case 1011: cli_shadowcolor = strdup(optarg); break;
        case 1012: subtitle_delay_ms = atoi(optarg); break;                
        default:
            fprintf(stderr,
                "Usage: mux_srt_dvb --input in.mp4 --output out.ts "
                "--srt subs1.srt[,subs2.srt] --languages eng[,deu] "
                "[--palette broadcast|greyscale|ebu-broadcast] [options]\n"
                "  --ass                       Enable libass rendering\n"
                "  --font FONTNAME             Set font family\n"
                "  --fontsize N                Set font size (px)\n"
                "  --fgcolor \"#RRGGBB\"         Set text color\n"
                "  --outlinecolor \"#RRGGBB\"    Set outline color\n"
                "  --shadowcolor \"#RRGGBB\"     Set shadow color\n"
                "  --delay MS                  Subtitle delay in ms (default: 300)\n");
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

    AVFormatContext *out_fmt=NULL;
    if(avformat_alloc_output_context2(&out_fmt,NULL,"mpegts",output)<0){
        fprintf(stderr,"Cannot alloc out_fmt\n"); return -1;
    }
    for(unsigned i=0;i<in_fmt->nb_streams;i++){
        AVStream *in_st=in_fmt->streams[i];
        AVStream *out_st=avformat_new_stream(out_fmt,NULL);
        avcodec_parameters_copy(out_st->codecpar,in_st->codecpar);
        av_dict_copy(&out_st->metadata, in_st->metadata, 0);
    }

    int ntracks=0; SubTrack tracks[8];
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_DVB_SUBTITLE);
    if (!codec) {
        fprintf(stderr,"DVB subtitle encoder not found\n");
        return -1;
    }

    char *save_srt=NULL, *save_lang=NULL;
    char *tok = strtok_r(srt_list, ",", &save_srt);
    char *tok_lang = strtok_r(lang_list, ",", &save_lang);
    while (tok && tok_lang && ntracks < 8) {
        tracks[ntracks].entries=NULL;
        tracks[ntracks].count=0;
        tracks[ntracks].cur_sub=0;
        tracks[ntracks].ass_track=NULL;
        tracks[ntracks].lang=strdup(tok_lang);
        tracks[ntracks].filename=strdup(tok);
        tracks[ntracks].forced=forced;
        tracks[ntracks].hi=hi;
        tracks[ntracks].last_pts = AV_NOPTS_VALUE; // Initialize per-track last_pts

        // No auto-delay from audio start_time; use --delay for manual adjustment
        int track_delay_ms = 0;
        // store per-track effective delay (manual CLI fine-tune only)
        tracks[ntracks].effective_delay_ms = track_delay_ms + subtitle_delay_ms;
        if (debug_level>0) {
            fprintf(stderr,
                    "[main] Track %s lang=%s delay=%dms (auto=%d + cli=%d)\n",
                    tok, tok_lang, tracks[ntracks].effective_delay_ms,
                    track_delay_ms, subtitle_delay_ms);
        }

        if (!use_ass) {
            int64_t t0=bench_now();
            int count=parse_srt(tok,&tracks[ntracks].entries,qc);
            if (bench_mode) bench.t_parse_us+=bench_now()-t0;
            tracks[ntracks].count=count;
        } else {
            if (!ass_lib) {
                ass_lib = render_ass_init();
                ass_renderer = render_ass_renderer(ass_lib, video_w, video_h);
            }
            tracks[ntracks].ass_track = render_ass_new_track(ass_lib);

            // Inject default ASS style (must precede any events)
            render_ass_set_style(tracks[ntracks].ass_track,
                                 cli_font,
                                 cli_fontsize,
                                 cli_fgcolor,     // NOTE: convert in render_ass_set_style
                                 cli_outlinecolor,
                                 cli_shadowcolor);
            if (debug_level > 0) {
                render_ass_debug_styles(tracks[ntracks].ass_track);
            }



            // Parse SRT with our existing parser, inject as ASS events
            SRTEntry *entries=NULL;
            int count=parse_srt(tok,&entries,qc);
            tracks[ntracks].entries=entries; // keep for timings
            tracks[ntracks].count=count;
            for (int j=0;j<count;j++) {
                char *ass_text = srt_html_to_ass(entries[j].text);
                char *plain    = strip_tags(ass_text);
                printf("DEBUG SRT->ASS: [%s] → [%s]\n", entries[j].text, ass_text);
                printf("DEBUG length(text)=%zu\n", strlen(plain));
                free(plain);
                if (!ass_text) ass_text = strdup(entries[j].text);
                render_ass_add_event(tracks[ntracks].ass_track,
                                    ass_text,
                                    entries[j].start_ms + tracks[ntracks].effective_delay_ms,
                                    entries[j].end_ms   + tracks[ntracks].effective_delay_ms);
                free(ass_text);
            }
        }
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

            if (debug_level > 0) {
                printf("Opened DVB encoder for track %d (%s, lang=%s): w=%d h=%d\n",
                    ntracks,
                    tracks[ntracks].filename,
                    tracks[ntracks].lang,
                    tracks[ntracks].codec_ctx->width,
                    tracks[ntracks].codec_ctx->height);
            }

            ntracks++;
        
        tok = strtok_r(NULL, ",", &save_srt);
        tok_lang = strtok_r(NULL, ",", &save_lang);
    }

    if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_fmt->pb, output, AVIO_FLAG_WRITE) < 0) {
            fprintf(stderr, "Error: could not open output file %s\n", output);
            return -1;
        }
    }

    #define PCR_BIAS_MS     700
    #define PCR_BIAS_TICKS  (PCR_BIAS_MS * 90)

    AVDictionary *mux_opts = NULL;
    av_dict_set(&mux_opts, "max_delay", "800000", 0);  // ~700 ms PCR lead
    av_dict_set(&mux_opts, "copyts", "1", 0);
    av_dict_set(&mux_opts, "start_at_zero", "1", 0);


    if (avformat_write_header(out_fmt, &mux_opts) < 0) {
        fprintf(stderr, "Error: could not write header for output file\n");
        return -1;
    }
    av_dict_free(&mux_opts);

    AVPacket *pkt = av_packet_alloc();
    
    while (av_read_frame(in_fmt, pkt) >= 0) {

        if (pkt->pts == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE) {
            pkt->pts = pkt->dts;
        }

        int64_t cur90 = (pkt->pts == AV_NOPTS_VALUE) ? AV_NOPTS_VALUE :
                        av_rescale_q(pkt->pts,
                                     in_fmt->streams[pkt->stream_index]->time_base,
                                     (AVRational){1,90000});

        for (int t = 0; t < ntracks; t++) {
            while (tracks[t].cur_sub < tracks[t].count &&
                  (((tracks[t].entries[tracks[t].cur_sub].start_ms +
                      tracks[t].effective_delay_ms) * 90)) <= cur90) {

                Bitmap bm={0};
                if (!use_ass) {
                    char *markup = srt_to_pango_markup(tracks[t].entries[tracks[t].cur_sub].text);
                    int64_t t1 = bench_now();
                    bm = render_text_pango(markup,
                                           video_w, video_h,
                                           cli_fontsize, cli_font,
                                           cli_fgcolor, cli_outlinecolor, cli_shadowcolor,
                                           tracks[t].entries[tracks[t].cur_sub].alignment,
                                           palette_mode);
                    if (bench_mode) { bench.t_render_us += bench_now() - t1; bench.cues_rendered++; }
                    free(markup);
                } else {
                    int64_t now_ms = tracks[t].entries[tracks[t].cur_sub].start_ms;
                    bm = render_ass_frame(ass_renderer, tracks[t].ass_track,
                                          now_ms, palette_mode);
                }

                // Pass raw times into make_subtitle; delay applied via packet PTS
                int track_delay_ms = tracks[t].effective_delay_ms;
                AVSubtitle *sub = make_subtitle(bm,
                                                tracks[t].entries[tracks[t].cur_sub].start_ms,
                                                tracks[t].entries[tracks[t].cur_sub].end_ms);
                if (sub) {
                    // Use duration so FFmpeg clears automatically
                    sub->start_display_time = 0;
                    sub->end_display_time =
                        (tracks[t].entries[tracks[t].cur_sub].end_ms -
                         tracks[t].entries[tracks[t].cur_sub].start_ms);

                    int64_t pts90 = ((tracks[t].entries[tracks[t].cur_sub].start_ms +
                                      track_delay_ms) * 90);

                    encode_and_write_subtitle(tracks[t].codec_ctx,
                                              out_fmt,
                                              &tracks[t],
                                              sub,
                                              pts90,
                                              bench_mode);

                    printf("[subs] Cue %d on %s: PTS=%lld ms, dur=%d ms, delay=%d ms\n",
                           tracks[t].cur_sub,
                           tracks[t].filename,
                           (long long)(pts90 / 90),
                           sub->end_display_time,
                           track_delay_ms);

                    avsubtitle_free(sub);
                }

                // Explicitly clear at end_ms
                AVSubtitle *clr = av_mallocz(sizeof(*clr));
                if (clr) {
                    clr->format = 0;
                    clr->start_display_time = 0;
                    clr->end_display_time   = 1;   // minimal duration
                    clr->num_rects = 0;

                    int64_t clr_pts90 = ((tracks[t].entries[tracks[t].cur_sub].end_ms +
                                        track_delay_ms) * 90);

                    encode_and_write_subtitle(tracks[t].codec_ctx,
                                            out_fmt,
                                            &tracks[t],
                                            clr,
                                            clr_pts90,
                                            bench_mode);

                    if (debug_level > 0) {
                        fprintf(stderr,
                            "[subs] CLEAR cue %d on %s @ %lld ms\n",
                            tracks[t].cur_sub,
                            tracks[t].filename,
                            (long long)(clr_pts90/90));
                    }

                    avsubtitle_free(clr);
                }

                tracks[t].cur_sub++;
            }
        }

        // Only remap and write packets for original A/V streams
        if (pkt->stream_index >= 0 && pkt->stream_index < (int)in_fmt->nb_streams) {
            AVStream *out_st = out_fmt->streams[pkt->stream_index];
            pkt->stream_index = out_st->index;

            int64_t t5 = bench_now();
            av_interleaved_write_frame(out_fmt, pkt);
            if (bench_mode) {
                bench.t_mux_us += bench_now() - t5;
                bench.packets_muxed++;
            }
        }
        av_packet_unref(pkt);
    }

    av_write_trailer(out_fmt);

    for (int t=0; t<ntracks; t++) {
        if (tracks[t].codec_ctx)
            avcodec_free_context(&tracks[t].codec_ctx);
    }

    avformat_free_context(out_fmt);
    avformat_close_input(&in_fmt);
    fclose(qc);
    av_packet_free(&pkt);

    if(bench_mode) bench_report();
    return 0;
}
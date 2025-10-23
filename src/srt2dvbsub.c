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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <getopt.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <dlfcn.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include "cpu_count.h"
#include "srt_parser.h"
#include "render_pango.h"
#include "render_ass.h"
#include "render_pool.h"
#include "dvb_sub.h"
#include "qc.h"
#include "bench.h"
#include "debug_png.h"
#include "runtime_opts.h"
#include "muxsub.h"
#include "subtrack.h"

#ifdef HAVE_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif


/* DVB language table: code, English name, native name */
struct dvb_lang_entry
{
    const char *code;
    const char *ename;
    const char *native;
};

static struct dvb_lang_entry dvb_langs[] = {
    {"eng", "English", "English"},
    {"deu", "German", "Deutsch"},
    {"fra", "French", "Français"},
    {"spa", "Spanish", "Español"},
    {"ita", "Italian", "Italiano"},
    {"por", "Portuguese", "Português"},
    {"rus", "Russian", "Русский"},
    {"jpn", "Japanese", "日本語"},
    {"zho", "Chinese", "中文"},
    {"kor", "Korean", "한국어"},
    {"nld", "Dutch", "Nederlands"},
    {"swe", "Swedish", "Svenska"},
    {"dan", "Danish", "Dansk"},
    {"nor", "Norwegian", "Norsk"},
    {"fin", "Finnish", "Suomi"},
    {"pol", "Polish", "Polski"},
    {"ces", "Czech", "Čeština"},
    {"slk", "Slovak", "Slovenčina"},
    {"slv", "Slovenian", "Slovenščina"},
    {"hrv", "Croatian", "Hrvatski"},
    {"ron", "Romanian", "Română"},
    {"bul", "Bulgarian", "Български"},
    {"ukr", "Ukrainian", "Українська"},
    {"bel", "Belarusian", "Беларуская"},
    {"est", "Estonian", "Eesti"},
    {"lav", "Latvian", "Latviešu"},
    {"lit", "Lithuanian", "Lietuvių"},
    {"hun", "Hungarian", "Magyar"},
    {"heb", "Hebrew", "עברית"},
    {"ara", "Arabic", "العربية"},
    {"tur", "Turkish", "Türkçe"},
    {"ell", "Greek", "Ελληνικά"},
    {"cat", "Catalan", "Català"},
    {"gle", "Irish", "Gaeilge"},
    {"eus", "Basque", "Euskara"},
    {"glg", "Galician", "Galego"},
    {"srp", "Serbian", "Српски"},
    {"mkd", "Macedonian", "Македонски"},
    {"alb", "Albanian", "Shqip"},
    {"hin", "Hindi", "हिन्दी"},
    {"tam", "Tamil", "தமிழ்"},
    {"tel", "Telugu", "తెలుగు"},
    {"pan", "Punjabi", "ਪੰਜਾਬੀ"},
    {"urd", "Urdu", "اردو"},
    {"vie", "Vietnamese", "Tiếng Việt"},
    {"tha", "Thai", "ไทย"},
    {"ind", "Indonesian", "Bahasa Indonesia"},
    {"msa", "Malay", "Bahasa Melayu"},
    {"sin", "Sinhala", "සිංහල"},
    {"khm", "Khmer", "ភាសាខ្មែរ"},
    {"lao", "Lao", "ລາວ"},
    {"mon", "Mongolian", "Монгол"},
    {"fas", "Persian", "فارسی"},
    {NULL, NULL, NULL}};

static int is_valid_dvb_lang(const char *code)
{
    if (!code)
        return 0;
    size_t len = strlen(code);
    if (len != 3)
        return 0;
    char low[4];
    for (int i = 0; i < 3; i++)
    {
        if (!isalpha((unsigned char)code[i]))
            return 0;
        low[i] = tolower((unsigned char)code[i]);
    }
    low[3] = '\0';
    for (struct dvb_lang_entry *p = dvb_langs; p->code; ++p)
    {
        if (strcmp(low, p->code) == 0)
            return 1;
    }
    return 0;
}

static void print_help(void)
{
    printf("Usage: mux_srt_dvb --input in.mp4 --output out.ts --srt subs.srt[,subs2.srt] --languages eng[,deu] [options]\n\n");
    printf("Options:\n");
    printf("  -I, --input FILE          Input media file\n");
    printf("  -o, --output FILE         Output TS file\n");
    printf("  -s, --srt FILES           Comma-separated SRT files\n");
    printf("  -l, --languages CODES     Comma-separated 3-letter DVB language codes\n");
    printf("      --font FONTNAME       Set font family\n");
    printf("      --fontsize N         Set font size in px (overrides dynamic sizing)\n");
    printf("      --fgcolor #RRGGBB    Text color\n");
    printf("      --outlinecolor #RRGGBB Outline color\n");
    printf("      --shadowcolor #AARRGGBB Shadow color (alpha optional)\n");
#ifdef HAVE_LIBASS
    printf("      --ass                Enable libass rendering\n");
#endif
    printf("      --enc-threads N      Set encoder thread count (0=auto)\n");
    printf("      --render-threads N   Parallel render workers (0=single-thread)\n");
    printf("      --ssaa N             Force supersample factor (1..6) (default 6)\n");
    printf("      --no-unsharp         Disable the final unsharp pass to speed rendering\n");
    printf("      --help, -h, -?       Show this help and exit\n\n");
    printf("Accepted DVB language codes:\n");
    printf("  Code  English / Native\n");
    printf("  ----  ----------------\n");
    for (struct dvb_lang_entry *p = dvb_langs; p->code; ++p)
    {
        printf("  %-4s  %s / %s\n", p->code, p->ename, p->native);
    }
    printf("\n");
}


int debug_level = 0; // Global debug level
int use_ass = 0;
int video_w = 720, video_h = 480; // Set the min video width and height...this will be overridden by libav probe

/* Performance / quality knobs (defined in runtime_opts.c) */
/* enc_threads, render_threads, ssaa_override, no_unsharp are declared in runtime_opts.h */

static int __srt_png_seq = 0;

/* signal handling: set this flag in the handler and check in main loops */
static volatile sig_atomic_t stop_requested = 0;
static void handle_signal(int sig)
{
    (void)sig;
    stop_requested = 1;
}

// Wrapper for encoding and muxing a DVB subtitle

int main(int argc, char **argv)
{
    const char *input = NULL, *output = NULL;
    char *srt_list = NULL, *lang_list = NULL;
    int forced = 0, hi = 0, qc_only = 0, bench_mode = 0;
    const char *palette_mode = "broadcast"; // default palette
    int subtitle_delay_ms = 0;              // user-supplied fine-tune

    // Subtitle style config (shared for ASS and Pango)
    const char *cli_font = "Robooto";
    /* 0 means "not set" — let render_pango compute dynamic sizing based on resolution */
    int cli_fontsize = 0;
    const char *cli_fgcolor = "#FFFFFF"; // hex for Pango
    const char *cli_outlinecolor = "#000000";
    const char *cli_shadowcolor = "#64000000";

    ASS_Library *ass_lib = NULL;
    ASS_Renderer *ass_renderer = NULL;

    static struct option long_opts[] = {
        {"input", required_argument, 0, 'I'},
        {"output", required_argument, 0, 'o'},
        {"srt", required_argument, 0, 's'},
        {"languages", required_argument, 0, 'l'},
        {"forced", no_argument, 0, 1000},
        {"hi", no_argument, 0, 1001},
        {"debug", required_argument, 0, 1002},
        {"qc-only", no_argument, 0, 1003},
        {"bench", no_argument, 0, 1004},
        {"palette", required_argument, 0, 1005},
#ifdef HAVE_LIBASS
        {"ass", no_argument, 0, 1006},
#endif
        {"font", required_argument, 0, 1007},
        {"fontsize", required_argument, 0, 1008},
        {"fgcolor", required_argument, 0, 1009},
        {"outlinecolor", required_argument, 0, 1010},
        {"shadowcolor", required_argument, 0, 1011},
        {"delay", required_argument, 0, 1012},
        {"enc-threads", required_argument, 0, 1013},
        {"render-threads", required_argument, 0, 1014},
        {"ssaa", required_argument, 0, 1015},
        {"no-unsharp", no_argument, 0, 1016},
        {0, 0, 0, 0}};

    int opt, long_index = 0;
    while ((opt = getopt_long(argc, argv, "I:o:s:l:h?", long_opts, &long_index)) != -1)
    {
        switch (opt)
        {
        case 'I':
            input = optarg;
            break;
        case 'o':
            output = optarg;
            break;
        case 's':
            srt_list = strdup(optarg);
            break;
        case 'l':
            lang_list = strdup(optarg);
            break;
        case 'h':
        case '?':
            print_help();
            return 0;
            break;
        case 1000:
            forced = 1;
            break;
        case 1001:
            hi = 1;
            break;
        case 1002:
            debug_level = atoi(optarg);
            break;
        case 1003:
            qc_only = 1;
            break;
        case 1004:
            bench_mode = 1;
            bench.enabled = 1;
            break;
        case 1005:
            palette_mode = strdup(optarg);
            break;
#ifdef HAVE_LIBASS
        case 1006:
            use_ass = 1;
            break;
#endif
        case 1007:
            cli_font = strdup(optarg);
            break;
        case 1008:
            cli_fontsize = atoi(optarg);
            break;
        case 1009:
            cli_fgcolor = strdup(optarg);
            break;
        case 1010:
            cli_outlinecolor = strdup(optarg);
            break;
        case 1011:
            cli_shadowcolor = strdup(optarg);
            break;
        case 1012:
            subtitle_delay_ms = atoi(optarg);
            break;
        case 1013:
            enc_threads = atoi(optarg);
            break;
        case 1014:
            render_threads = atoi(optarg);
            break;
        case 1015:
            ssaa_override = atoi(optarg);
            break;
        case 1016:
            no_unsharp = 1;
            break;
        default:
            /* Use the central help printer so the text remains consistent
             * (and respects whether libass was enabled at configure-time). */
            print_help();
            return 1;
        }
    }

    /*
     * Common user mistake: calling the short option as "-srt" (no space) results
     * in optarg being "rt" and the actual SRT path left as a non-option argv.
     * Detect that case and auto-correct so the tool is more forgiving.
     */
    if (srt_list && strcmp(srt_list, "rt") == 0)
    {
        if (optind < argc && argv[optind] && argv[optind][0] != '-')
        {
            free(srt_list);
            srt_list = strdup(argv[optind]);
            if (debug_level > 0)
                fprintf(stderr, "Auto-corrected '-srt' to use '%s' as SRT path\n", srt_list);
            optind++; // consume this arg so callers expecting remaining args are correct
        }
    }

    if (!input || !output || !srt_list || !lang_list)
    {
        fprintf(stderr, "Error: --input, --output, --srt, and --languages are required\n");
        return 1;
    }

    /* Validate --languages tokens: must be 3-letter DVB language codes */
    {
        char *tmp = strdup(lang_list);
        char *save = NULL;
        char *tok = strtok_r(tmp, ",", &save);
        while (tok)
        {
            if (!is_valid_dvb_lang(tok))
            {
                fprintf(stderr, "Error: invalid language code '%s' in --languages; must be 3-letter DVB language code\n", tok);
                free(tmp);
                return 1;
            }
            tok = strtok_r(NULL, ",", &save);
        }
        free(tmp);
    }

    if (debug_level > 1)
        av_log_set_level(AV_LOG_INFO);
    else if (debug_level == 1)
        av_log_set_level(AV_LOG_ERROR);
    else
        av_log_set_level(AV_LOG_QUIET);

    bench_start();

    /* Apply runtime knobs to pango renderer */
    if (ssaa_override > 0)
        render_pango_set_ssaa_override(ssaa_override);
    if (no_unsharp)
        render_pango_set_no_unsharp(1);

    /* Initialize render pool if requested */
    if (render_threads > 0)
    {
        if (render_pool_init(render_threads) != 0)
        {
            fprintf(stderr, "Warning: failed to initialize render pool with %d threads\n", render_threads);
            render_threads = 0;
        }
        else
        {
            atexit(render_pool_shutdown);
        }
    }

    /* Install simple signal handlers so Ctrl-C triggers orderly shutdown */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    FILE *qc = NULL;
    if (qc_only)
    {
        qc = fopen("qc_log.txt", "w");
        if (!qc)
        {
            perror("qc_log.txt");
            return 1;
        }
    }

    /* Ensure output directory for PNG debug files exists only when debug_level > 0 */
    if (debug_level > 0)
    {
        if (mkdir("pngs", 0755) < 0 && errno != EEXIST)
        {
            fprintf(stderr, "Warning: could not create pngs/ directory: %s\n", strerror(errno));
        }
    }

    // ---------- QC-only ----------
    if (qc_only)
    {
        char *tok = strtok(srt_list, ",");
        char *tok_lang = strtok(lang_list, ",");
        while (tok && tok_lang)
        {
            SRTEntry *entries = NULL;
            int64_t t0 = bench_now();
            int count = parse_srt(tok, &entries, qc);
            if (bench_mode)
                bench.t_parse_us += bench_now() - t0;
            if (debug_level > 0)
                printf("QC-only: %s (%s), cues=%d forced=%d hi=%d\n",
                       tok, tok_lang, count, forced, hi);
            for (int j = 0; j < count; j++)
                free(entries[j].text);
            free(entries);
            tok = strtok(NULL, ",");
            tok_lang = strtok(NULL, ",");
        }
        if (qc)
            fclose(qc);
        if (bench_mode)
            bench_report();
        return 0;
    }

    // ---------- Normal mux ----------
    avformat_network_init();
    AVFormatContext *in_fmt = NULL;
    if (avformat_open_input(&in_fmt, input, NULL, NULL) < 0)
    {
        fprintf(stderr, "Cannot open input\n");
        return -1;
    }
    avformat_find_stream_info(in_fmt, NULL);

    int video_index = -1, first_audio_index = -1;
    int64_t input_start_pts90 = 0; // Initialize input_start_pts90 (will recompute after streams are probed)
    for (unsigned i = 0; i < in_fmt->nb_streams; i++)
    {
        AVStream *st = in_fmt->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_index < 0)
        {
            video_index = i;
            if (st->codecpar->width > 0)
                video_w = st->codecpar->width;
            if (st->codecpar->height > 0)
                video_h = st->codecpar->height;
        }
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && first_audio_index < 0)
        {
            first_audio_index = i;
        }
    }

    /* Recompute input_start_pts90 now that we've discovered the video stream index
     * so we can pick a per-stream start_time if the container-level start_time
     * wasn't set. This prevents subtitle PTS from appearing earlier than the
     * actual video when streams have non-zero start_time. */
    if (in_fmt->start_time != AV_NOPTS_VALUE)
    {
        input_start_pts90 = av_rescale_q(in_fmt->start_time, AV_TIME_BASE_Q, (AVRational){1, 90000});
    }
    else if (video_index >= 0 && in_fmt->streams[video_index]->start_time != AV_NOPTS_VALUE)
    {
        input_start_pts90 = av_rescale_q(in_fmt->streams[video_index]->start_time,
                                         in_fmt->streams[video_index]->time_base,
                                         (AVRational){1, 90000});
    }
    else
    {
        input_start_pts90 = 0;
    }
    if (debug_level > 0)
    {
        fprintf(stderr, "input_start_pts90=%lld (video_index=%d)\n", (long long)input_start_pts90, video_index);
        fprintf(stderr, "[main] Discovered video size: %dx%d\n", video_w, video_h);
    }

    AVFormatContext *out_fmt = NULL;
    if (avformat_alloc_output_context2(&out_fmt, NULL, "mpegts", output) < 0)
    {
        fprintf(stderr, "Cannot alloc out_fmt\n");
        return -1;
    }
    for (unsigned i = 0; i < in_fmt->nb_streams; i++)
    {
        AVStream *in_st = in_fmt->streams[i];
        AVStream *out_st = avformat_new_stream(out_fmt, NULL);
        avcodec_parameters_copy(out_st->codecpar, in_st->codecpar);
        av_dict_copy(&out_st->metadata, in_st->metadata, 0);
    }

    int ntracks = 0;
    SubTrack tracks[8];
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_DVB_SUBTITLE);
    if (!codec)
    {
        fprintf(stderr, "DVB subtitle encoder not found\n");
        return -1;
    }

    char *save_srt = NULL, *save_lang = NULL;
    char *tok = strtok_r(srt_list, ",", &save_srt);
    char *tok_lang = strtok_r(lang_list, ",", &save_lang);
    while (tok && tok_lang && ntracks < 8)
    {
        tracks[ntracks].entries = NULL;
        tracks[ntracks].count = 0;
        tracks[ntracks].cur_sub = 0;
#ifdef HAVE_LIBASS
        tracks[ntracks].ass_track = NULL;
#endif
        tracks[ntracks].lang = strdup(tok_lang);
        tracks[ntracks].filename = strdup(tok);
        tracks[ntracks].forced = forced;
        tracks[ntracks].hi = hi;
        tracks[ntracks].last_pts = AV_NOPTS_VALUE; // Initialize per-track last_pts

        // No auto-delay from audio start_time; use --delay for manual adjustment
        int track_delay_ms = 0;
        // store per-track effective delay (manual CLI fine-tune only)
        tracks[ntracks].effective_delay_ms = track_delay_ms + subtitle_delay_ms;
        if (debug_level > 0)
        {
            fprintf(stderr,
                    "[main] Track %s lang=%s delay=%dms (auto=%d + cli=%d)\n",
                    tok, tok_lang, tracks[ntracks].effective_delay_ms,
                    track_delay_ms, subtitle_delay_ms);
        }

        if (!use_ass)
        {
            int64_t t0 = bench_now();
            int count = parse_srt(tok, &tracks[ntracks].entries, qc);
            if (bench_mode)
                bench.t_parse_us += bench_now() - t0;
            tracks[ntracks].count = count;
            if (debug_level > 0)
            {
                fprintf(stderr, "[main] Parsed %d cues from SRT '%s' for track %d\n", count, tok, ntracks);
            }
        }
        else
        {
#ifdef HAVE_LIBASS
            if (!ass_lib)
            {
                ass_lib = render_ass_init();
                ass_renderer = render_ass_renderer(ass_lib, video_w, video_h);
            }
            tracks[ntracks].ass_track = render_ass_new_track(ass_lib);

            // Inject default ASS style (must precede any events)
            render_ass_set_style(tracks[ntracks].ass_track,
                                 cli_font,
                                 cli_fontsize,
                                 cli_fgcolor, // NOTE: convert in render_ass_set_style
                                 cli_outlinecolor,
                                 cli_shadowcolor);
            if (debug_level > 0)
            {
                render_ass_debug_styles(tracks[ntracks].ass_track);
            }

            // Parse SRT with our existing parser, inject as ASS events
            SRTEntry *entries = NULL;
            int count = parse_srt(tok, &entries, qc);
            tracks[ntracks].entries = entries; // keep for timings
            tracks[ntracks].count = count;
            for (int j = 0; j < count; j++)
            {
                char *ass_text = srt_html_to_ass(entries[j].text);
                char *plain = strip_tags(ass_text);
                printf("DEBUG SRT->ASS: [%s] → [%s]\n", entries[j].text, ass_text);
                printf("DEBUG length(text)=%zu\n", strlen(plain));
                free(plain);
                if (!ass_text)
                    ass_text = strdup(entries[j].text);
                render_ass_add_event(tracks[ntracks].ass_track,
                                     ass_text,
                                     entries[j].start_ms + tracks[ntracks].effective_delay_ms,
                                     entries[j].end_ms + tracks[ntracks].effective_delay_ms);
                free(ass_text);
            }
#endif
        }
        tracks[ntracks].stream = avformat_new_stream(out_fmt, NULL);
        tracks[ntracks].stream->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
        tracks[ntracks].stream->codecpar->codec_id = AV_CODEC_ID_DVB_SUBTITLE;
        tracks[ntracks].stream->time_base = (AVRational){1, 90000};
        av_dict_set(&tracks[ntracks].stream->metadata, "language", tok_lang, 0);
        if (forced)
            av_dict_set(&tracks[ntracks].stream->metadata, "forced", "1", 0);
        if (hi)
            av_dict_set(&tracks[ntracks].stream->metadata, "hearing_impaired", "1", 0);

        // allocate encoder context per track
        tracks[ntracks].codec_ctx = avcodec_alloc_context3(codec);
        if (!tracks[ntracks].codec_ctx)
        {
            fprintf(stderr, "Failed to alloc codec context for track %s\n", tok);
            return -1;
        }
        tracks[ntracks].codec_ctx->time_base = (AVRational){1, 90000};
        tracks[ntracks].codec_ctx->width = video_w;
        tracks[ntracks].codec_ctx->height = video_h;

        /* Configure encoder threading: allow user override (enc_threads==0 => auto) */
        if (enc_threads <= 0)
        {
            tracks[ntracks].codec_ctx->thread_count = get_cpu_count();
        }
        else
        {
            tracks[ntracks].codec_ctx->thread_count = enc_threads;
        }
#if defined(FF_THREAD_FRAME)
        tracks[ntracks].codec_ctx->thread_type = FF_THREAD_FRAME;
#elif defined(FF_THREAD_SLICE)
        tracks[ntracks].codec_ctx->thread_type = FF_THREAD_SLICE;
#endif

        if (avcodec_open2(tracks[ntracks].codec_ctx, codec, NULL) < 0)
        {
            fprintf(stderr, "Failed to open DVB subtitle encoder for track %s\n", tok);
            return -1;
        }

        if (debug_level > 0)
        {
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

    if (!(out_fmt->oformat->flags & AVFMT_NOFILE))
    {
        if (avio_open(&out_fmt->pb, output, AVIO_FLAG_WRITE) < 0)
        {
            fprintf(stderr, "Error: could not open output file %s\n", output);
            return -1;
        }
    }

#define PCR_BIAS_MS 700
#define PCR_BIAS_TICKS (PCR_BIAS_MS * 90)

    AVDictionary *mux_opts = NULL;
    av_dict_set(&mux_opts, "max_delay", "800000", 0); // ~700 ms PCR lead
    av_dict_set(&mux_opts, "copyts", "1", 0);
    av_dict_set(&mux_opts, "start_at_zero", "1", 0);

    if (avformat_write_header(out_fmt, &mux_opts) < 0)
    {
        fprintf(stderr, "Error: could not write header for output file\n");
        return -1;
    }
    av_dict_free(&mux_opts);

    AVPacket *pkt = av_packet_alloc();

    /* Track first video PTS (90k) so we can emit a tiny blank subtitle aligned
     * with the video start. This ensures the subtitle stream start_time is
     * initialized and avoids the first real subtitle being dropped by the
     * muxer. */
    int seen_first_video = 0;
    int64_t first_video_pts90 = AV_NOPTS_VALUE;

    /* Progress tracking (enabled when debug_level > 0) */
    time_t prog_start_time = time(NULL);
    long pkt_count = 0;
    long subs_emitted = 0;
    int pkt_progress_mask = 0x3f; /* print every 64 packets */
    int64_t total_duration_pts90 = AV_NOPTS_VALUE;
    time_t last_progress_time = 0;
    int64_t last_valid_cur90 = AV_NOPTS_VALUE;
    if (in_fmt->duration != AV_NOPTS_VALUE)
    {
        int64_t dur90 = av_rescale_q(in_fmt->duration, AV_TIME_BASE_Q, (AVRational){1, 90000});
        /* prefer duration relative to input_start_pts90 */
        if (dur90 > input_start_pts90)
            total_duration_pts90 = dur90 - input_start_pts90;
        else
            total_duration_pts90 = dur90;
    }

    /* No automatic initial blank subtitle is emitted. Emitting a synthetic
     * blank subtitle to initialize stream timing caused timing mismatches
     * for some inputs. Subtitle stream timing will be driven by real
     * subtitle events and packet timestamps. */

    while (av_read_frame(in_fmt, pkt) >= 0)
    {
        if (stop_requested)
        {
            if (debug_level > 0)
                fprintf(stderr, "[main] stop requested (signal), breaking demux loop\n");
            av_packet_unref(pkt);
            break;
        }
        pkt_count++;

        if (pkt->pts == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE)
        {
            pkt->pts = pkt->dts;
        }

        /* Do not emit an initial blank subtitle when the first video PTS is
         * encountered. Leaving subtitle stream initialization to real
         * subtitle events avoids surprising timing shifts. */

        int64_t cur90 = (pkt->pts == AV_NOPTS_VALUE) ? AV_NOPTS_VALUE : av_rescale_q(pkt->pts, in_fmt->streams[pkt->stream_index]->time_base, (AVRational){1, 90000});
        if (cur90 != AV_NOPTS_VALUE)
            last_valid_cur90 = cur90;
        /* Use last_valid_cur90 as a fallback when the current packet has no PTS
         * so subtitle emission logic can proceed on streams where some packets
         * don't carry explicit timestamps. */
        int64_t cmp90 = (cur90 != AV_NOPTS_VALUE) ? cur90 : last_valid_cur90;

        /* Periodic progress print (in-place) -- only when debug_level == 0 */
        if (debug_level == 0 && (pkt_count & pkt_progress_mask) == 0)
        {
            time_t now = time(NULL);
            if (now - last_progress_time >= 1)
            {
                double elapsed = difftime(now, prog_start_time);
                double pct = 0.0;
                double eta = 0.0;
                /* Use last known valid timestamp for percentage so the UI
                 * doesn't blink when a packet lacking PTS is encountered. */
                int64_t cur_for_pct = (cur90 != AV_NOPTS_VALUE) ? cur90 : last_valid_cur90;
                if (total_duration_pts90 != AV_NOPTS_VALUE && total_duration_pts90 > 0 && cur_for_pct != AV_NOPTS_VALUE)
                {
                    pct = (double)(cur_for_pct - input_start_pts90) / (double)total_duration_pts90;
                    if (pct < 0.0)
                        pct = 0.0;
                    if (pct > 1.0)
                        pct = 1.0;
                    if (pct > 0.001)
                    {
                        double total_est = elapsed / pct;
                        eta = total_est - elapsed;
                    }
                }
                int mins = (int)(elapsed / 60.0);
                int secs = (int)(elapsed) % 60;
                int eta_m = (int)(eta / 60.0);
                int eta_s = (int)(eta) % 60;
                if (total_duration_pts90 != AV_NOPTS_VALUE)
                {
                    char line[81];
                    int n = snprintf(line, sizeof(line), "Progress: %5.1f%% subs=%ld elapsed=%02d:%02d ETA=%02d:%02d", pct * 100.0, subs_emitted, mins, secs, eta_m, eta_s);
                    if (n < 0)
                        n = 0;
                    if (n >= (int)sizeof(line))
                        n = (int)sizeof(line) - 1;
                    /* pad to clear leftover characters */
                    memset(line + n, ' ', sizeof(line) - n - 1);
                    line[sizeof(line) - 1] = '\0';
                    fprintf(stdout, "\r%s\r", line);
                }
                else
                {
                    char line[81];
                    int n = snprintf(line, sizeof(line), "Progress: pkt=%ld subs=%ld elapsed=%02d:%02d", pkt_count, subs_emitted, mins, secs);
                    if (n < 0)
                        n = 0;
                    if (n >= (int)sizeof(line))
                        n = (int)sizeof(line) - 1;
                    memset(line + n, ' ', sizeof(line) - n - 1);
                    line[sizeof(line) - 1] = '\0';
                    fprintf(stdout, "\r%s\r", line);
                }
                fflush(stdout);
                last_progress_time = now;
            }
        }

        for (int t = 0; t < ntracks; t++)
        {
            /* Diagnostic: show why subtitle emission condition may not be met.
             * Print current demux PTS and next cue target PTS (both in 90k units).
             */
            if (debug_level > 1) {
                if (tracks[t].cur_sub < tracks[t].count) {
                    int64_t next_pts90 = input_start_pts90 + ((tracks[t].entries[tracks[t].cur_sub].start_ms + tracks[t].effective_delay_ms) * 90);
                    fprintf(stderr, "[diag] cur90=%lld next_cue_pts90=%lld (track=%d cur_sub=%d)\n", (long long)cur90, (long long)next_pts90, t, tracks[t].cur_sub);
                } else {
                    fprintf(stderr, "[diag] no more cues for track %d (cur_sub=%d count=%d)\n", t, tracks[t].cur_sub, tracks[t].count);
                }
            }
                while (tracks[t].cur_sub < tracks[t].count &&
                         (((tracks[t].entries[tracks[t].cur_sub].start_ms +
                             tracks[t].effective_delay_ms) *
                            90)) <= cmp90)
            {

                Bitmap bm = {0};
                if (!use_ass)
                {
                    char *markup = srt_to_pango_markup(tracks[t].entries[tracks[t].cur_sub].text);
                    int64_t t1 = bench_now();
                    int render_w = video_w > 0 ? video_w : 1920;
                    int render_h = video_h > 0 ? video_h : 1080;
                    /* Prefer per-track encoder target size if available so the
                     * rendered bitmap coordinates match the encoder/mux target
                     * coordinate system. This prevents placement mismatch where
                     * rendering uses a different resolution than the DVB
                     * encoder expects. */
                    if (tracks[t].codec_ctx)
                    {
                        if (tracks[t].codec_ctx->width > 0)
                            render_w = tracks[t].codec_ctx->width;
                        if (tracks[t].codec_ctx->height > 0)
                            render_h = tracks[t].codec_ctx->height;
                    }
                    int cue_align = tracks[t].entries[tracks[t].cur_sub].alignment;
                    /* If SRT/ASS alignment explicitly requests top (7..9), remap
                     * to the corresponding bottom alignment (1..3) for DVB
                     * rendering. Many players and DVB subtitle viewers expect
                     * subtitles at the bottom; if the source contains top-anchor
                     * markers it's often unintended for broadcast overlays. */
                    int used_align = cue_align;
                    if (!use_ass && cue_align >= 7 && cue_align <= 9)
                    {
                        used_align = cue_align - 6; /* 7->1,8->2,9->3 */
                        if (debug_level > 0)
                            fprintf(stderr,
                                    "[main-debug] remapping cue align %d -> %d for DVB render\n",
                                    cue_align, used_align);
                    }
                    if (debug_level > 0)
                    {
                        fprintf(stderr,
                                "[main-debug] about to render cue %d: render_w=%d render_h=%d codec_w=%d codec_h=%d video_w=%d video_h=%d align=%d used_align=%d\n",
                                tracks[t].cur_sub,
                                render_w, render_h,
                                tracks[t].codec_ctx ? tracks[t].codec_ctx->width : -1,
                                tracks[t].codec_ctx ? tracks[t].codec_ctx->height : -1,
                                video_w, video_h,
                                cue_align, used_align);
                    }
                    if ((video_w <= 0 || video_h <= 0) && debug_level > 0)
                    {
                        fprintf(stderr, "[main] Warning: video size unknown, using fallback %dx%d for rendering\n", render_w, render_h);
                    }
                    if (render_threads > 0)
                    {
                        /* Attempt to fetch an already-rendered result keyed by track and cue.
                         * If not present, submit a small prefetch window (including current cue)
                         * and try again. If still not ready, fall back to synchronous render. */
                        Bitmap tmpb = {0};
                        int got = render_pool_try_get(t, tracks[t].cur_sub, &tmpb);
                        if (got == 1)
                        {
                            bm = tmpb; /* use the async result */
                        }
                        else if (got == 0)
                        {
                            /* job exists but not finished yet; perform a blocking render to avoid waiting forever */
                            bm = render_pool_render_sync(markup,
                                                         render_w, render_h,
                                                         cli_fontsize, cli_font,
                                                         cli_fgcolor, cli_outlinecolor, cli_shadowcolor,
                                                         used_align,
                                                         palette_mode);
                        }
                        else
                        {
                            /* no job exists yet for this cue: submit prefetch window */
                            const int PREFETCH_WINDOW = 8;
                            for (int pi = 0; pi < PREFETCH_WINDOW; ++pi)
                            {
                                int qi = tracks[t].cur_sub + pi;
                                if (qi >= tracks[t].count)
                                    break;
                                char *pm = srt_to_pango_markup(tracks[t].entries[qi].text);
                                /* submit async job; render_pool makes its own copy */
                                render_pool_submit_async(t, qi,
                                                         pm,
                                                         render_w, render_h,
                                                         cli_fontsize, cli_font,
                                                         cli_fgcolor, cli_outlinecolor, cli_shadowcolor,
                                                         used_align,
                                                         palette_mode);
                                free(pm);
                            }
                            /* try to fetch again; if still not present, fallback to sync render */
                            if (render_pool_try_get(t, tracks[t].cur_sub, &tmpb) == 1)
                            {
                                bm = tmpb;
                            }
                            else
                            {
                                bm = render_pool_render_sync(markup,
                                                             render_w, render_h,
                                                             cli_fontsize, cli_font,
                                                             cli_fgcolor, cli_outlinecolor, cli_shadowcolor,
                                                             used_align,
                                                             palette_mode);
                            }
                        }
                    }
                    else
                    {
                        bm = render_text_pango(markup,
                                               render_w, render_h,
                                               cli_fontsize, cli_font,
                                               cli_fgcolor, cli_outlinecolor, cli_shadowcolor,
                                               used_align,
                                               palette_mode);
                    }
                    if (bench_mode)
                    {
                        bench.t_render_us += bench_now() - t1;
                        bench.cues_rendered++;
                    }
                    free(markup);
                }
#ifdef HAVE_LIBASS                
                else
                {
                    int64_t now_ms = tracks[t].entries[tracks[t].cur_sub].start_ms;
                    bm = render_ass_frame(ass_renderer, tracks[t].ass_track,
                                          now_ms, palette_mode);
                }
#endif

                // Pass raw times into make_subtitle; delay applied via packet PTS
                int track_delay_ms = tracks[t].effective_delay_ms;
                /* Save debug PNG of the rendered bitmap only at high debug
                 * verbosity (debug_level > 1). When not saving, pass NULL to
                 * the encoder helper so it won't report PNG-origin logs. */
                char pngfn[PATH_MAX] = "";
                    if (debug_level > 1)
                    {
                        snprintf(pngfn, sizeof(pngfn), "pngs/srt_%03d_t%02d_c%03d.png", __srt_png_seq++, t, tracks[t].cur_sub);
                        save_bitmap_png(&bm, pngfn);
                        fprintf(stderr, "[png] SRT bitmap saved: %s (x=%d y=%d w=%d h=%d)\n", pngfn, bm.x, bm.y, bm.w, bm.h);
                        /* Also print the cue index and the normalized text so we can
                         * verify that cue<->PNG mapping is correct. */
                        if (tracks[t].cur_sub < tracks[t].count && tracks[t].entries[tracks[t].cur_sub].text) {
                            fprintf(stderr, "[png] cue idx=%d text='%s'\n", tracks[t].cur_sub, tracks[t].entries[tracks[t].cur_sub].text);
                        }
                    }
                if (debug_level > 0) {
                    int64_t dbg_start_ms = tracks[t].entries[tracks[t].cur_sub].start_ms;
                    fprintf(stderr, "[dbg] rendered track=%d cue=%d start_ms=%d (delay=%d)\n", t, tracks[t].cur_sub, (int)dbg_start_ms, tracks[t].effective_delay_ms);
                }

                AVSubtitle *sub = make_subtitle(bm,
                                                tracks[t].entries[tracks[t].cur_sub].start_ms,
                                                tracks[t].entries[tracks[t].cur_sub].end_ms);
                if (sub)
                {
                    // Use duration so FFmpeg clears automatically
                    sub->start_display_time = 0;
                    sub->end_display_time =
                        (tracks[t].entries[tracks[t].cur_sub].end_ms -
                         tracks[t].entries[tracks[t].cur_sub].start_ms);

                    int64_t pts90 = input_start_pts90 + ((tracks[t].entries[tracks[t].cur_sub].start_ms +
                                                          track_delay_ms) *
                                                         90);
                    if (debug_level > 0) {
                        fprintf(stderr, "[dbg] encoding track=%d cue=%d pts90=%lld (ms=%lld)\n", t, tracks[t].cur_sub, (long long)pts90, (long long)(pts90/90));
                    }

                    encode_and_write_subtitle(tracks[t].codec_ctx,
                                              out_fmt,
                                              &tracks[t],
                                              sub,
                                              pts90,
                                              bench_mode,
                                              (debug_level > 1 ? pngfn : NULL));

                    /* Count emitted subtitles for progress reporting */
                    subs_emitted++;
                    if (debug_level > 1)
                    {
                        fprintf(stderr, "[png] SRT bitmap saved: %s\n", pngfn);
                        printf("[subs] Cue %d on %s: PTS=%lld ms, dur=%d ms, delay=%d ms\n",
                               tracks[t].cur_sub,
                               tracks[t].filename,
                               (long long)(pts90 / 90),
                               sub->end_display_time,
                               track_delay_ms);
                    }

                    /* Immediate progress update after writing a subtitle (in-place).
                     * Only show when debug_level == 0 (quiet/default mode). */
                    if (debug_level == 0)
                    {
                        time_t now = time(NULL);
                        if (now - last_progress_time >= 1)
                        {
                            double elapsed = difftime(now, prog_start_time);
                            int mins = (int)(elapsed / 60.0);
                            int secs = (int)(elapsed) % 60;
                            /* Try to include percentage when we have a duration */
                            char line2[81];
                            if (total_duration_pts90 != AV_NOPTS_VALUE && total_duration_pts90 > 0 && last_valid_cur90 != AV_NOPTS_VALUE)
                            {
                                double pct = (double)(last_valid_cur90 - input_start_pts90) / (double)total_duration_pts90;
                                if (pct < 0.0)
                                    pct = 0.0;
                                if (pct > 1.0)
                                    pct = 1.0;
                                int eta_m = 0, eta_s = 0;
                                if (pct > 0.001)
                                {
                                    double total_est = elapsed / pct;
                                    double eta = total_est - elapsed;
                                    eta_m = (int)(eta / 60.0);
                                    eta_s = (int)(eta) % 60;
                                }
                                int n2 = snprintf(line2, sizeof(line2), "Progress: %5.1f%% subs=%ld elapsed=%02d:%02d ETA=%02d:%02d", pct * 100.0, subs_emitted, mins, secs, eta_m, eta_s);
                                if (n2 < 0)
                                    n2 = 0;
                                if (n2 >= (int)sizeof(line2))
                                    n2 = (int)sizeof(line2) - 1;
                            }
                            else
                            {
                                int n2 = snprintf(line2, sizeof(line2), "Progress: subs=%ld elapsed=%02d:%02d", subs_emitted, mins, secs);
                                if (n2 < 0)
                                    n2 = 0;
                                if (n2 >= (int)sizeof(line2))
                                    n2 = (int)sizeof(line2) - 1;
                            }
                            /* pad to clear leftover chars */
                            int len = (int)strlen(line2);
                            if (len < (int)sizeof(line2) - 1)
                                memset(line2 + len, ' ', sizeof(line2) - len - 1);
                            line2[sizeof(line2) - 1] = '\0';
                            fprintf(stdout, "\r%s\r", line2);
                            fflush(stdout);
                            last_progress_time = now;
                        }
                    }

                    avsubtitle_free(sub);
                    av_free(sub);

                    /* free temporary bitmap buffers we allocated during render */
                    if (bm.idxbuf)
                        av_free(bm.idxbuf);
                    if (bm.palette)
                        av_free(bm.palette);
                }

                // Explicitly clear at end_ms
                AVSubtitle *clr = av_mallocz(sizeof(*clr));
                if (clr)
                {
                    clr->format = 0;
                    clr->start_display_time = 0;
                    clr->end_display_time = 1; // minimal duration
                    clr->num_rects = 0;

                    int64_t clr_pts90 = input_start_pts90 + ((tracks[t].entries[tracks[t].cur_sub].end_ms +
                                                              track_delay_ms) *
                                                             90);

                    encode_and_write_subtitle(tracks[t].codec_ctx,
                                              out_fmt,
                                              &tracks[t],
                                              clr,
                                              clr_pts90,
                                              bench_mode,
                                              NULL);

                    if (debug_level > 0)
                    {
                        fprintf(stderr,
                                "[subs] CLEAR cue %d on %s @ %lld ms\n",
                                tracks[t].cur_sub,
                                tracks[t].filename,
                                (long long)(clr_pts90 / 90));
                    }

                    avsubtitle_free(clr);
                    av_free(clr);
                }

                tracks[t].cur_sub++;
            }
        }

        // Only remap and write packets for original A/V streams
        if (pkt->stream_index >= 0 && pkt->stream_index < (int)in_fmt->nb_streams)
        {
            AVStream *out_st = out_fmt->streams[pkt->stream_index];
            pkt->stream_index = out_st->index;

            int64_t t5 = bench_now();
            av_interleaved_write_frame(out_fmt, pkt);
            if (bench_mode)
            {
                bench.t_mux_us += bench_now() - t5;
                bench.packets_muxed++;
            }
        }
        av_packet_unref(pkt);
    }

    av_write_trailer(out_fmt);

    for (int t = 0; t < ntracks; t++)
    {
        /* Ensure render workers are stopped before releasing Pango/fontmap */
        render_pool_shutdown();
        /* Ensure Pango/fontmap resources are released before FcFini */
        render_pango_cleanup();

        if (tracks[t].codec_ctx)
            avcodec_free_context(&tracks[t].codec_ctx);
    }

    /* free per-track resources */
    for (int t = 0; t < ntracks; t++)
    {
        if (tracks[t].lang)
            free((void *)tracks[t].lang);
        if (tracks[t].filename)
            free((void *)tracks[t].filename);
        if (tracks[t].entries)
        {
            for (int j = 0; j < tracks[t].count; j++)
            {
                if (tracks[t].entries[j].text)
                    free(tracks[t].entries[j].text);
            }
            free(tracks[t].entries);
            tracks[t].entries = NULL;
        }
#ifdef HAVE_LIBASS        
        if (tracks[t].ass_track)
        {
            // free libass track if any
            render_ass_free_track(tracks[t].ass_track);
            tracks[t].ass_track = NULL;
        }
#endif
    }

    if (ass_renderer)
        render_ass_free_renderer(ass_renderer);
    if (ass_lib)
        render_ass_free_lib(ass_lib);
    /* free CLI strdup'd strings */
    if (srt_list)
        free(srt_list);
    if (lang_list)
        free(lang_list);
    if (palette_mode && strcmp(palette_mode, "broadcast") != 0)
        free((void *)palette_mode);
    if (cli_font && strcmp(cli_font, "Robooto") != 0)
        free((void *)cli_font);
    if (cli_fgcolor && strcmp(cli_fgcolor, "#FFFFFF") != 0)
        free((void *)cli_fgcolor);
    if (cli_outlinecolor && strcmp(cli_outlinecolor, "#000000") != 0)
        free((void *)cli_outlinecolor);
    if (cli_shadowcolor && strcmp(cli_shadowcolor, "#64000000") != 0)
        free((void *)cli_shadowcolor);

    /* Close output IO if opened, free contexts and network resources to
     * allow libav to release internal allocations detected by ASan. */
    if (out_fmt && out_fmt->pb)
        avio_closep(&out_fmt->pb);
    avformat_free_context(out_fmt);
    avformat_close_input(&in_fmt);
    avformat_network_deinit();
    if (qc)
        fclose(qc);
    av_packet_free(&pkt);

#if 1
    /* Additional runtime cleanup: try to unref Pango default fontmap and
     * reset any Cairo static caches. This should run before FcFini so that
     * Pango/GObject-owned references are released prior to fontconfig
     * finalization. */
    {
        /* Try to unref Pango's default font map via dlsym (libpango + libgobject) */
        void *pango = dlopen("libpango-1.0.so.0", RTLD_LAZY | RTLD_LOCAL);
        void *gobj = dlopen("libgobject-2.0.so.0", RTLD_LAZY | RTLD_LOCAL);
        if (pango && gobj)
        {
            /* pango_font_map_get_default returns a PangoFontMap* */
            void *(*pango_font_map_get_default_f)(void) = dlsym(pango, "pango_font_map_get_default");
            void (*g_object_unref_f)(void *) = dlsym(gobj, "g_object_unref");
            if (pango_font_map_get_default_f && g_object_unref_f)
            {
                void *map = pango_font_map_get_default_f();
                if (map)
                {
                    if (debug_level > 1)
                        fprintf(stderr, "[main] unref() pango default font map\n");
                    g_object_unref_f(map);
                }
            }
        }
        if (pango)
            dlclose(pango);
        if (gobj)
            dlclose(gobj);

        /* We intentionally do NOT call cairo_debug_reset_static_data() here.
         * On some cairo versions this can trigger internal assertions such
         * as "_cairo_hash_table_destroy: live_entries != 0" if static
         * cairo objects still exist. Calling FcFini() and unref'ing the
         * Pango fontmap is safer and sufficient to reduce ASan-reported
         * fontconfig/pango allocations. */
    }

    /* Try to call FcFini() to allow fontconfig to release small internal
     * allocations (observed as tiny strdup() leaks under ASan). If fontconfig
     * was found at configure time we link it and call FcFini() directly; fall
     * back to the dlopen/dlsym approach otherwise. */
    {
#ifdef HAVE_FONTCONFIG
        if (debug_level > 1)
            fprintf(stderr, "[main] calling FcFini() (linked) to cleanup fontconfig\n");
        FcFini();
#else
        void *fc = dlopen("libfontconfig.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (!fc)
            fc = dlopen("libfontconfig.so", RTLD_LAZY | RTLD_LOCAL);
        if (fc)
        {
            void (*FcFini_f)(void) = (void (*)(void))dlsym(fc, "FcFini");
            if (FcFini_f)
            {
                if (debug_level > 1)
                    fprintf(stderr, "[main] calling FcFini() to cleanup fontconfig\n");
                FcFini_f();
            }
            dlclose(fc);
        }
#endif
    }

#endif

    if (bench_mode)
        bench_report();
    /* Ensure we end with a newline so the CLI prompt appears on the next line */
    fprintf(stdout, "\n");
    fflush(stdout);
    return 0;
}
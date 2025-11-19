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
#define DEBUG_MODULE "main"
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <getopt.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <wchar.h>
#include <wctype.h>
#include <locale.h>
#include <ctype.h>

#ifdef HAVE_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>

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
#include "mux_write.h"
#include "dvb_lang.h"
#include "utils.h"
#include "fontlist.h"
#include "version.h"
#include "debug.h"
#include "progress.h"
#include "delay_parse.h"
#include "lang_parse.h"
#include "render_params.h"
#include "png_path.h"

/*
 * srt2dvbsub.c
 * --------------
 * Top-level CLI and muxing logic for converting SRT (or ASS) subtitle
 * files into DVB subtitle tracks multiplexed into MPEG-TS. This file
 * contains argument parsing, stream probing, per-track state management
 * and the main demux/mux loop which emits subtitle packets aligned to
 * input timestamps.
 *
 * High-level responsibilities:
 *  - validate user-provided DVB language codes
 *  - parse SRT files (via srt_parser) or inject ASS events (via render_ass)
 *  - render subtitle bitmaps (Pango or libass) and convert to DVB bitmaps
 *  - encode DVB subtitle packets and write them into the output container
 *
 * Resource ownership summary:
 *  - SubTrack.entries: allocated by parse_srt(); main() frees entries[i].text
 *    and the entries array on shutdown.
 *  - AVFormatContext/AVCodecContext/A VStreams: managed by libavformat/libavcodec;
 *    main closes and frees these at the end of the run.
 */

 /*
 * PCR_BIAS_MS:
 *   Defines the bias in milliseconds to be applied to the Program Clock Reference (PCR).
 *   This value is typically used to adjust timing for synchronization purposes.
 *
 * PCR_BIAS_TICKS:
 *   Converts the PCR bias from milliseconds to MPEG ticks (where 1 ms = 90 ticks).
 *   Used for precise timing adjustments in MPEG transport streams.
 */
#define PCR_BIAS_MS 700
#define PCR_BIAS_TICKS (PCR_BIAS_MS * 90)

/**
 * struct MainCtx
 * brief Main context structure for subtitle processing and rendering.
 *
 * This structure holds all relevant data and configuration for handling
 * subtitle tracks, rendering options, input/output formats, and other
 * runtime parameters.
 *
 * Members:
 *   tracks              Pointer to an array of subtitle tracks.
 *   ntracks             Number of subtitle tracks.
 *   ass_lib             (Optional) libass library handle for ASS/SSA rendering.
 *   ass_renderer        (Optional) libass renderer instance.
 *   srt_list            List of SRT subtitle files.
 *   lang_list           List of language codes for subtitles.
 *   palette_mode        Palette mode for subtitle rendering.
 *   cli_font            Font name for CLI rendering.
 *   cli_fgcolor         Foreground color for CLI rendering.
 *   cli_outlinecolor    Outline color for CLI rendering.
 *   cli_shadowcolor     Shadow color for CLI rendering.
 *   cli_bgcolor         Background color for CLI rendering.
 *   cli_forced_list     Comma-separated forced flags per track.
 *   cli_hi_list         Comma-separated hearing-impaired flags per track.
 *   subtitle_delay_list List of subtitle delay values.
 *   delay_vals          Array of delay values for each track.
 *   out_fmt             Output format context (FFmpeg).
 *   in_fmt              Input format context (FFmpeg).
 *   qc                  File pointer for quality control output.
 *   pkt                 Packet structure for subtitle data.
 *   bench_mode          Benchmark mode flag.
 *   debug_level         Debug verbosity level.
 *   render_threads      Number of rendering threads.
 */
struct MainCtx {
    SubTrack *tracks;    /* pointer to tracks array */
    int ntracks;
#ifdef HAVE_LIBASS
    ASS_Library *ass_lib;
    ASS_Renderer *ass_renderer;
#endif
    char *srt_list;
    char *lang_list;
    const char *palette_mode;
    const char *cli_font;
    const char *cli_font_style;
    const char *cli_fgcolor;
    const char *cli_outlinecolor;
    const char *cli_shadowcolor;
    const char *cli_bgcolor;
    char *cli_forced_list;
    char *cli_hi_list;
    char *subtitle_delay_list;
    int *delay_vals;
    AVFormatContext *out_fmt;
    AVFormatContext *in_fmt;
    FILE *qc;
    AVPacket *pkt;
    int bench_mode;
    int debug_level;
    int render_threads;
    int64_t mux_rate;
    char *service_name;
    char *service_provider;
};

static void ctx_cleanup(struct MainCtx *ctx);

/* signal handling: set this flag in the handler and check in main loops */
static volatile sig_atomic_t stop_requested = 0;

/* Monotonic sequence used when writing debug PNG filenames. */
static int __srt_png_seq = 0;

/* Trampoline to adapt the shared utils signal helper to sigaction(2). */
static void ctx_signal_request_stop(int sig)
{
    handle_signal(sig, &stop_requested);
}

/*
 * brief Finalizes the main context and returns a status code.
 *
 * This function checks if the context has already been cleaned up.
 * If not, it performs cleanup by calling ctx_cleanup(). It then
 * returns the provided status code.
 *
 * param ctx Pointer to the main context structure.
 * param ctx_cleaned Boolean flag indicating if the context has already been cleaned up.
 * param ret Status code to return after finalization.
 * return The provided status code.
 */
static int finalize_main(struct MainCtx *ctx, bool ctx_cleaned, int ret)
{
    if (!ctx_cleaned)
        ctx_cleanup(ctx);
    return ret;
}

/**
 * @brief Parses command-line arguments for the srt2dvbsub application.
 *
 * This function processes the provided command-line arguments, sets the corresponding
 * configuration options in the given context, and validates required parameters.
 * It supports both short and long options, including input/output file paths,
 * SRT file list, language codes, subtitle rendering options, and various flags.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line argument strings.
 * @param ctx Pointer to the main context structure to be configured.
 * @param input Pointer to store the input file path.
 * @param output Pointer to store the output file path.
 * @param srt_list Pointer to store the SRT file list.
 * @param lang_list Pointer to store the language code list.
 * @param forced Pointer to store the forced subtitle flag.
 * @param hi Pointer to store the hearing-impaired subtitle flag.
 * @param qc_only Pointer to store the QC-only flag.
 * @param bench_mode Pointer to store the benchmarking mode flag.
 * @param palette_mode Pointer to store the palette mode string.
 * @param subtitle_delay_ms Pointer to store the subtitle delay in milliseconds.
 * @param subtitle_delay_list Pointer to store the subtitle delay list string.
 * @param cli_fontsize Pointer to store the CLI font size.
 * @param cli_font Pointer to store the CLI font name string.
 * @param cli_fgcolor Pointer to store the CLI foreground color string.
 * @param cli_outlinecolor Pointer to store the CLI outline color string.
 * @param cli_shadowcolor Pointer to store the CLI shadow color string.
 * @param cli_bgcolor Pointer to store the CLI background color string.
 * @param debug_level Pointer to store the debug level.
 * @param use_ass_flag Pointer to store the ASS subtitle rendering flag (if available).
 *
 * @return Returns 0 on help/license request, 1 on error, and -1 on successful parsing.
 *
 * @note The function prints usage information and error messages to stderr.
 *       It also validates that required options are provided and that language codes
 *       conform to the DVB 3-letter standard.
 */
static int cli_parse(int argc, char **argv,
                          struct MainCtx *ctx,
                          const char **input,
                          const char **output,
                          char **srt_list,
                          char **lang_list,
                          char **forced_list,
                          char **hi_list,
                          int *qc_only,
                          int *bench_mode,
                          const char **palette_mode,
                          int *subtitle_delay_ms,
                          char **subtitle_delay_list,
                          int *cli_fontsize,
                          const char **cli_font,
                          const char **cli_font_style,
                          const char **cli_fgcolor,
                          const char **cli_outlinecolor,
                          const char **cli_shadowcolor,
                          const char **cli_bgcolor,
                          int *debug_level,
                          int *use_ass_flag)
{
    int opt, long_index = 0;

    static const struct option long_opts[] = {
        {"input", required_argument, 0, 'I'},
        {"output", required_argument, 0, 'o'},
        {"srt", required_argument, 0, 's'},
        {"languages", required_argument, 0, 'l'},
        {"forced", required_argument, 0, 1000},
        {"hi", required_argument, 0, 1001},
        {"debug", required_argument, 0, 1002},
        {"qc-only", no_argument, 0, 1003},
        {"bench", no_argument, 0, 1004},
        {"palette", required_argument, 0, 1005},
#ifdef HAVE_LIBASS
        {"ass", no_argument, 0, 1006},
#endif
        {"list-fonts", no_argument, 0, 1019},
        {"font", required_argument, 0, 1007},
        {"fontsize", required_argument, 0, 1008},
        {"fgcolor", required_argument, 0, 1009},
        {"outlinecolor", required_argument, 0, 1010},
        {"shadowcolor", required_argument, 0, 1011},
        {"bg-color", required_argument, 0, 1021},
        {"delay", required_argument, 0, 1012},
        {"enc-threads", required_argument, 0, 1013},
        {"render-threads", required_argument, 0, 1014},
        {"ssaa", required_argument, 0, 1015},
        {"no-unsharp", no_argument, 0, 1016},
        {"font-style", required_argument, 0, 1018},
        {"png-dir", required_argument, 0, 1020},
        {"license", no_argument, 0, 1017},
        {"help", no_argument, 0, 'h'},
        {"?", no_argument, 0, '?'},
        {0, 0, 0, 0}};

    if (cli_font_style && ctx)
        ctx->cli_font_style = *cli_font_style;

    while ((opt = getopt_long(argc, argv, "I:o:s:l:h?", long_opts, &long_index)) != -1)
    {
        switch (opt)
        {
        case 'I':
            if (validate_path_length(optarg, "--input") != 0)
            {
                return 1;
            }
            *input = optarg;
            break;
        case 'o':
            if (validate_path_length(optarg, "--output") != 0)
            {
                return 1;
            }
            *output = optarg;
            break;
        case 's':
            if (validate_path_length(optarg, "--srt") != 0)
            {
                return 1;
            }
            *srt_list = strdup(optarg);
            if (!*srt_list)
            {
                LOG(1, "Out of memory duplicating --srt value\n");
                return 1;
            }
            ctx->srt_list = *srt_list;
            break;
        case 'l':
            *lang_list = strdup(optarg);
            if (!*lang_list)
            {
                LOG(1, "Out of memory duplicating --languages value\n");
                return 1;
            }
            ctx->lang_list = *lang_list;
            break;
        case 'h':
        case '?':
            print_help();
            return 0;
        case 1000:
            if (replace_strdup((const char **)forced_list, optarg) != 0) {
                LOG(0, "Out of memory while setting forced flags\n");
                return 1;
            }
            ctx->cli_forced_list = *forced_list;
            break;
        case 1001:
            if (replace_strdup((const char **)hi_list, optarg) != 0) {
                LOG(0, "Out of memory while setting hi flags\n");
                return 1;
            }
            ctx->cli_hi_list = *hi_list;
            break;
        case 1002:
            *debug_level = atoi(optarg);
            break;
        case 1003:
            *qc_only = 1;
            break;
        case 1004:
            *bench_mode = 1;
            break;
        case 1005:
        {
            char *dup = strdup(optarg);
            if (!dup) {
                LOG(1, "Out of memory duplicating palette mode\n");
                return 1;
            }
            *palette_mode = dup;
            ctx->palette_mode = *palette_mode;
            break;
        }
#ifdef HAVE_LIBASS
        case 1006:
            if (use_ass_flag)
                *use_ass_flag = 1;
            break;
#endif
        case 1019:
        {
#ifdef HAVE_FONTCONFIG
            int rc = fontlist_print_all();
            return (rc == 0) ? 0 : 1;
#else
            fprintf(stderr, "Font listing requires Fontconfig support at build time.\n");
            return 1;
#endif
        }
        case 1007:
            if (replace_strdup(cli_font, optarg) != 0) {
                LOG(0, "Out of memory while setting font name\n");
                return 1;
            }
            ctx->cli_font = *cli_font;
            break;
        case 1008:
            {
                char fontsize_err[256] = {0};
                int ret = validate_fontsize(optarg, cli_fontsize, fontsize_err);
                if (ret != 0) {
                    LOG(0, "Font size validation error: %s\n", fontsize_err);
                    LOG(0, "Valid range: %s\n", get_fontsize_usage());
                    return 1;
                }
            }
            break;
        case 1018:
            if (replace_strdup(cli_font_style, optarg) != 0) {
                LOG(0, "Out of memory while setting font style\n");
                return 1;
            }
            ctx->cli_font_style = *cli_font_style;
            break;
        case 1009:
            {
                char color_err[256] = {0};
                int ret = validate_color(optarg, color_err);
                if (ret != 0) {
                    LOG(0, "Foreground color validation error: %s\n", color_err);
                    LOG(0, "Valid format: %s\n", get_color_usage());
                    return 1;
                }
            }
            if (replace_strdup(cli_fgcolor, optarg) != 0) {
                LOG(0, "Out of memory while setting fgcolor\n");
                return 1;
            }
            ctx->cli_fgcolor = *cli_fgcolor;
            break;
        case 1010:
            {
                char color_err[256] = {0};
                int ret = validate_color(optarg, color_err);
                if (ret != 0) {
                    LOG(0, "Outline color validation error: %s\n", color_err);
                    LOG(0, "Valid format: %s\n", get_color_usage());
                    return 1;
                }
            }
            if (replace_strdup(cli_outlinecolor, optarg) != 0) {
                LOG(0, "Out of memory while setting outline color\n");
                return 1;
            }
            ctx->cli_outlinecolor = *cli_outlinecolor;
            break;
        case 1011:
            {
                char color_err[256] = {0};
                int ret = validate_color(optarg, color_err);
                if (ret != 0) {
                    LOG(0, "Shadow color validation error: %s\n", color_err);
                    LOG(0, "Valid format: %s\n", get_color_usage());
                    return 1;
                }
            }
            if (replace_strdup(cli_shadowcolor, optarg) != 0) {
                LOG(0, "Out of memory while setting shadow color\n");
                return 1;
            }
            ctx->cli_shadowcolor = *cli_shadowcolor;
            break;
        case 1021:
            {
                char color_err[256] = {0};
                int ret = validate_color(optarg, color_err);
                if (ret != 0) {
                    LOG(0, "Background color validation error: %s\n", color_err);
                    LOG(0, "Valid format: %s\n", get_color_usage());
                    return 1;
                }
            }
            if (replace_strdup(cli_bgcolor, optarg) != 0) {
                LOG(0, "Out of memory while setting background color\n");
                return 1;
            }
            ctx->cli_bgcolor = *cli_bgcolor;
            LOG(0, "DEBUG: Set background color to: %s\n", *cli_bgcolor);
            break;
        case 1012:
            /* Parse and validate subtitle delay with better error messages */
            {
                int parsed_delay = 0;
                char delay_err[256] = {0};
                int ret = parse_single_delay(optarg, &parsed_delay, delay_err);
                if (ret != 0) {
                    LOG(0, "Subtitle delay parsing error: %s\n", delay_err);
                    return 1;
                }
                *subtitle_delay_ms = parsed_delay;
                
                char *dup_delay = strdup(optarg);
                if (!dup_delay) {
                    LOG(0, "Out of memory while setting subtitle delay list\n");
                    return 1;
                }
                free(*subtitle_delay_list);
                *subtitle_delay_list = dup_delay;
                ctx->subtitle_delay_list = *subtitle_delay_list;
                
                if (ctx->debug_level > 0) {
                    LOG(1, "Subtitle delay set to %d ms\n", parsed_delay);
                }
            }
            break;
        case 1013:
            enc_threads = atoi(optarg);
            /* Bounds-check enc_threads similar to render_threads */
            {
                int max_threads = get_cpu_count();
                if (max_threads <= 0) max_threads = 1; /* fallback */
                int reasonable_max = (max_threads > 1) ? (max_threads * 2) : 4;
                
                if (enc_threads < 0) {
                    LOG(1, "Warning: enc-threads=%d is negative; using 0 (auto CPU count)\n", enc_threads);
                    enc_threads = 0;
                } else if (enc_threads > reasonable_max) {
                    LOG(1, "Warning: enc-threads=%d exceeds recommended max (%d based on %d CPUs); capping to %d\n",
                        enc_threads, reasonable_max, max_threads, reasonable_max);
                    enc_threads = reasonable_max;
                }
            }
            break;
        case 1014:
            render_threads = atoi(optarg);
            /* Bounds-check render_threads: warn if excessive, cap at reasonable limit */
            {
                int max_threads = get_cpu_count();
                if (max_threads <= 0) max_threads = 16; /* fallback if detection fails */
                int reasonable_max = (max_threads > 1) ? (max_threads * 2) : 4;
                
                if (render_threads < 0) {
                    /* atoi returns -N for negative input; reject explicitly */
                    LOG(1, "Warning: render-threads=%d is negative; using 0 (sync-only mode)\n", render_threads);
                    render_threads = 0;
                } else if (render_threads > reasonable_max) {
                    LOG(1, "Warning: render-threads=%d exceeds recommended max (%d based on %d CPUs); capping to %d\n",
                        render_threads, reasonable_max, max_threads, reasonable_max);
                    render_threads = reasonable_max;
                }
            }
            break;
        case 1015:
            ssaa_override = atoi(optarg);
            break;
        case 1016:
            no_unsharp = 1;
            break;
        case 1020:
            {
                char png_err[256] = {0};
                int ret = init_png_path(optarg, png_err);
                if (ret != 0) {
                    LOG(0, "PNG directory initialization error: %s\n", png_err);
                    return 1;
                }
                LOG(1, "PNG output directory: %s\n", get_png_output_dir());
            }
            break;
        case 1017:
            print_license();
            return 0;
        default:
            print_help();
            return 1;
        }
    }

    if (*srt_list && strcmp(*srt_list, "rt") == 0)
    {
        if (optind < argc && argv[optind] && argv[optind][0] != '-')
        {
            if (validate_path_length(argv[optind], "SRT path") != 0)
            {
                return 1;
            }
            free(*srt_list);
            char *dup = strdup(argv[optind]);
            if (!dup)
            {
                LOG(1, "Out of memory duplicating SRT path\n");
                return 1;
            }
            *srt_list = dup;
            ctx->srt_list = *srt_list;
            if (*debug_level > 0)
                LOG(1, "Auto-corrected '-srt' to use '%s' as SRT path\n", *srt_list);
            optind++;
        }
    }

    if (!*input || !*output || !*srt_list || !*lang_list)
    {
        print_usage();
        return 1;
    }

    /* Validate language list with detailed error reporting */
    {
        char lang_err[512] = {0};
        int ret = validate_language_list(*lang_list, lang_err);
        if (ret != 0) {
            LOG(0, "Language list validation error: %s\n", lang_err);
            return 1;
        }
    }

    return -1;
}


/**
 * Initializes the MainCtx structure and prepares input/output formats.
 *
 * This function sets up the main context for subtitle processing by:
 * - Initializing network components.
 * - Opening the input format and retrieving stream information.
 * - Identifying video and audio streams, and determining video dimensions.
 * - Setting up the output format for MPEG-TS.
 * - Preparing subtitle tracks and locating the DVB subtitle encoder.
 * - Tokenizing SRT and language lists for per-track processing.
 * - Parsing per-track delay values from the provided delay list.
 *
 * @param ctx Pointer to the MainCtx structure to be initialized.
 * @param input Path to the input media file.
 * @param output Path to the output media file.
 * @param srt_list Comma-separated list of SRT subtitle files.
 * @param lang_list Comma-separated list of language codes for subtitles.
 * @param tracks Array of SubTrack structures for subtitle processing.
 * @param ntracks Pointer to the number of subtitle tracks.
 * @param video_w Pointer to store discovered video width.
 * @param video_h Pointer to store discovered video height.
 * @param tok_out Pointer to store tokenized SRT list state.
 * @param tok_lang_out Pointer to store tokenized language list state.
 * @param save_srt_out Pointer to store state for SRT tokenization.
 * @param save_lang_out Pointer to store state for language tokenization.
 * @param delay_count_out Pointer to store the count of parsed delay values.
 * @param subtitle_delay_ms Global subtitle delay in milliseconds.
 * @param subtitle_delay_list Comma-separated list of per-track delays.
 * @param debug_level Debug verbosity level.
 * @param input_start_pts90_out Pointer to store input start PTS in 90kHz units.
 * @param codec_out Pointer to store the located DVB subtitle encoder.
 *
 * @return 0 on success, negative value on failure.
 */
static int ctx_init(struct MainCtx *ctx,
                         const char *input, const char *output,
                         char *srt_list, char *lang_list,
                         SubTrack tracks[], int *ntracks,
                         int *video_w, int *video_h,
                         char **tok_out, char **tok_lang_out,
                         char **save_srt_out, char **save_lang_out,
                         int *delay_count_out,
                         int subtitle_delay_ms,
                         char *subtitle_delay_list,
                         int debug_level,
                         int64_t *input_start_pts90_out,
                         const AVCodec **codec_out)
{
    (void)subtitle_delay_ms;

    avformat_network_init();

    AVFormatContext *in_fmt = NULL;
    AVDictionary *fmt_opts = NULL;
    av_dict_set(&fmt_opts, "buffer_size", "10485760", 0); /* 10 MiB read buffer */
    if (avformat_open_input(&in_fmt, input, NULL, &fmt_opts) < 0) {
        av_dict_free(&fmt_opts);
        LOG(0, "Cannot open input file '%s': file not found or unsupported format\n", input);
        return -1;
    }
    av_dict_free(&fmt_opts);
    ctx->in_fmt = in_fmt;

    avformat_find_stream_info(in_fmt, NULL);
    
    /* Validate input is MPEG-TS format */
    if (!in_fmt->iformat || !in_fmt->iformat->name || 
        (strcmp(in_fmt->iformat->name, "mpegts") != 0 && 
         strcmp(in_fmt->iformat->name, "mpeg2ts") != 0)) {
        if (debug_level > 0 || 1) {  /* Always warn, not just debug mode */
            LOG(0, "Warning: Input file '%s' is not MPEG-TS format (detected: %s)\n", 
                input, in_fmt->iformat ? in_fmt->iformat->name : "unknown");
            LOG(0, "This program is designed for MPEG-TS inputs. Other formats may produce unexpected results.\n");
        }
    }

    int video_index = -1, first_audio_index = -1;
    for (unsigned i = 0; i < in_fmt->nb_streams; i++) {
        AVStream *st = in_fmt->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_index < 0) {
            video_index = i;
            if (st->codecpar->width > 0)
                *video_w = st->codecpar->width;
            if (st->codecpar->height > 0)
                *video_h = st->codecpar->height;
        }
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && first_audio_index < 0)
            first_audio_index = i;
    }

    if (in_fmt->start_time != AV_NOPTS_VALUE) {
        *input_start_pts90_out = av_rescale_q(in_fmt->start_time, AV_TIME_BASE_Q, (AVRational){1, 90000});
    } else if (video_index >= 0 && in_fmt->streams[video_index]->start_time != AV_NOPTS_VALUE) {
        *input_start_pts90_out = av_rescale_q(in_fmt->streams[video_index]->start_time,
                                              in_fmt->streams[video_index]->time_base,
                                              (AVRational){1, 90000});
    } else {
        *input_start_pts90_out = 0;
    }
    if (debug_level > 0) {
        LOG(1, "input_start_pts90=%lld (video_index=%d)\n", (long long)*input_start_pts90_out, video_index);
        LOG(1, "Discovered video size: %dx%d\n", *video_w, *video_h);
    }

    AVFormatContext *out_fmt = NULL;
    int64_t detected_mux_rate = 0;
    if (avformat_alloc_output_context2(&out_fmt, NULL, "mpegts", output) < 0) {
        LOG(0, "Cannot alloc out_fmt\n");
        return -1;
    }
    ctx->out_fmt = out_fmt;
    for (unsigned i = 0; i < in_fmt->nb_streams; i++) {
        AVStream *in_st = in_fmt->streams[i];
        AVStream *out_st = avformat_new_stream(out_fmt, NULL);
        if (!out_st) {
            LOG(0, "Failed to allocate output stream %u while mirroring input stream\n", i);
            return -1;
        }
        if (avcodec_parameters_copy(out_st->codecpar, in_st->codecpar) < 0) {
            LOG(0, "Failed to copy codec parameters for output stream %u\n", i);
            return -1;
        }
        if (av_dict_copy(&out_st->metadata, in_st->metadata, 0) < 0) {
            LOG(1, "Warning: unable to copy metadata for output stream %u\n", i);
        }
    }

    ctx->tracks = tracks;
    ctx->ntracks = *ntracks;

    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_DVB_SUBTITLE);
    if (!codec) {
        LOG(1, "DVB subtitle encoder not found\n");
        return -1;
    }
    *codec_out = codec;

    /* Tokenize SRT and language lists for the per-track loop */
    *save_srt_out = NULL;
    *save_lang_out = NULL;
    *tok_out = strtok_r(srt_list, ",", save_srt_out);
    *tok_lang_out = strtok_r(lang_list, ",", save_lang_out);

    /* Parse per-track delay list with robust error handling */
    *delay_count_out = 0;
    if (subtitle_delay_list) {
        int *delay_vals = NULL;
        int delay_count = 0;
        char delay_list_err[256] = {0};
        
        int ret = parse_delay_list(subtitle_delay_list, &delay_vals, &delay_count, delay_list_err);
        if (ret != 0) {
            LOG(0, "Subtitle delay list parsing error: %s\n", delay_list_err);
            free(delay_vals);
            return -1;
        }
        
        ctx->delay_vals = delay_vals;
        *delay_count_out = delay_count;
        
        if (debug_level > 0) {
            LOG(1, "Parsed %d subtitle delay values: ", delay_count);
            for (int i = 0; i < delay_count; i++) {
                fprintf(stderr, "%s%d ms", (i > 0 ? ", " : ""), delay_vals[i]);
            }
            fprintf(stderr, "\n");
        }
    }

    /* Try to capture mux rate from the input so we can mirror it on output. */
    if (in_fmt->bit_rate > 0)
        detected_mux_rate = in_fmt->bit_rate;
    if (!detected_mux_rate)
    {
        AVDictionaryEntry *tag = av_dict_get(in_fmt->metadata, "muxrate", NULL, 0);
        if (!tag)
            tag = av_dict_get(in_fmt->metadata, "bit_rate", NULL, 0);
        if (tag)
            detected_mux_rate = strtoll(tag->value, NULL, 10);
    }
    if (!detected_mux_rate)
    {
        for (unsigned i = 0; i < in_fmt->nb_programs && !detected_mux_rate; i++)
        {
            AVProgram *prog = in_fmt->programs[i];
            if (!prog)
                continue;
            AVDictionaryEntry *tag = av_dict_get(prog->metadata, "muxrate", NULL, 0);
            if (!tag)
                tag = av_dict_get(prog->metadata, "bit_rate", NULL, 0);
            if (tag)
                detected_mux_rate = strtoll(tag->value, NULL, 10);
        }
    }
    ctx->mux_rate = detected_mux_rate;
    if (debug_level > 0 && detected_mux_rate > 0)
        LOG(1, "Detected input muxrate=%" PRId64 " bps\n", detected_mux_rate);

    const char *src_service_name = NULL;
    const char *src_service_provider = NULL;
    for (unsigned i = 0; i < in_fmt->nb_programs && (!src_service_name || !src_service_provider); ++i)
    {
        AVProgram *prog = in_fmt->programs[i];
        if (!prog)
            continue;
        if (!src_service_name)
        {
            AVDictionaryEntry *tag = av_dict_get(prog->metadata, "service_name", NULL, 0);
            if (tag && tag->value && tag->value[0])
                src_service_name = tag->value;
        }
        if (!src_service_provider)
        {
            AVDictionaryEntry *tag = av_dict_get(prog->metadata, "service_provider", NULL, 0);
            if (tag && tag->value && tag->value[0])
                src_service_provider = tag->value;
        }
    }
    if (!src_service_name)
    {
        AVDictionaryEntry *tag = av_dict_get(in_fmt->metadata, "service_name", NULL, 0);
        if (tag && tag->value && tag->value[0])
            src_service_name = tag->value;
    }
    if (!src_service_provider)
    {
        AVDictionaryEntry *tag = av_dict_get(in_fmt->metadata, "service_provider", NULL, 0);
        if (tag && tag->value && tag->value[0])
            src_service_provider = tag->value;
    }
    if (src_service_name)
    {
        ctx->service_name = strdup(src_service_name);
        if (!ctx->service_name)
        {
            LOG(1, "Out of memory duplicating service_name metadata\n");
            return -1;
        }
        av_dict_set(&out_fmt->metadata, "service_name", src_service_name, 0);
    }
    if (src_service_provider)
    {
        ctx->service_provider = strdup(src_service_provider);
        if (!ctx->service_provider)
        {
            LOG(1, "Out of memory duplicating service_provider metadata\n");
            return -1;
        }
        av_dict_set(&out_fmt->metadata, "service_provider", src_service_provider, 0);
    }

    return 0;
}

/**
 * parse_flag_list: Parse a comma-separated list of flags (0/1) into an array
 * @param flag_str Comma-separated flags (e.g., "0,1,0" or "1")
 * @param track_count Number of expected tracks
 * @param flags Output array to fill with parsed flags (must be pre-allocated)
 * @param max_tracks Maximum size of flags array
 * @return 0 on success, -1 on error
 * 
 * If flag_str is NULL, all flags default to 0.
 * If flag_str has fewer entries than track_count, remaining entries default to 0.
 */
static int parse_flag_list(const char *flag_str, int track_count, int *flags, int max_tracks)
{
    if (!flags || track_count <= 0 || track_count > max_tracks)
        return -1;
    
    /* Initialize all flags to 0 */
    for (int i = 0; i < track_count; i++)
        flags[i] = 0;
    
    if (!flag_str || *flag_str == '\0')
        return 0;  /* All flags remain 0 */
    
    char *copy = strdup(flag_str);
    if (!copy)
        return -1;
    
    int idx = 0;
    char *saveptr = NULL;
    char *token = strtok_r(copy, ",", &saveptr);
    
    while (token && idx < track_count) {
        char *trimmed = trim_string_inplace(token);
        if (*trimmed == '\0') {
            idx++;
            token = strtok_r(NULL, ",", &saveptr);
            continue;
        }
        
        int val = atoi(trimmed);
        flags[idx] = (val != 0) ? 1 : 0;
        idx++;
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    free(copy);
    return 0;
}

/*
 * ctx_parse_tracks: process tokenized SRT/lang lists, parse SRT files or
 * inject ASS events (when enabled), create output streams and open encoders.
 * On allocation or runtime failures the helper calls ctx_cleanup(ctx)
 * and returns non-zero.
 */
static int ctx_parse_tracks(struct MainCtx *ctx,
                                 SubTrack tracks[],
                                 int *ntracks,
                                 char *tok, char *tok_lang,
                                 char **save_srt, char **save_lang,
                                 int delay_count,
                                 int subtitle_delay_ms,
                                 int debug_level,
                                 int video_w, int video_h,
                                 const AVCodec *codec,
                                 const int *forced_flags, const int *hi_flags,
                                 int use_ass,
                                 int bench_mode,
                                 FILE *qc,
                                 int cli_fontsize,
                                 const char *cli_font,
                                 const char *cli_font_style,
                                 const char *cli_fgcolor,
                                 const char *cli_outlinecolor,
                                 const char *cli_shadowcolor,
                                 int enc_threads)
{
    int ret = 0;
    (void)cli_font_style; /* current parsing path forwards style later during render */
    while (tok && tok_lang && *ntracks < 8) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
        /* Trim and validate language code token */
        char *tok_lang_ptr = trim_string_inplace(tok_lang);
        
        if (tok_lang_ptr[0] == '\0' || strlen(tok_lang_ptr) != 3) {
            LOG(0, "Invalid or empty language code at track %d: '%s'\n", *ntracks, tok_lang);
            ctx_cleanup(ctx);
            return -1;
        }
        
        tracks[*ntracks].entries = NULL;
        tracks[*ntracks].count = 0;
        tracks[*ntracks].cur_sub = 0;
#ifdef HAVE_LIBASS
        tracks[*ntracks].ass_track = NULL;
#endif
        if (replace_strdup_owned(&tracks[*ntracks].lang, tok_lang_ptr) != 0) {
            LOG(0, "Out of memory allocating track language\n");
            ret = 1;
            ctx_cleanup(ctx);
            return ret;
        }
        /* Include this partially-initialized track in ctx so cleanup will free it on failure */
        ctx->ntracks = *ntracks + 1;
        if (validate_path_length(tok, "SRT track") != 0) {
            ctx_cleanup(ctx);
            return 1;
        }
        if (replace_strdup_owned(&tracks[*ntracks].filename, tok) != 0) {
            LOG(0, "Out of memory allocating track filename\n");
            ret = 1;
            ctx_cleanup(ctx);
            return ret;
        }
        /* Get per-track forced/hi flags from arrays (defaultto 0 if arrays NULL or index out of bounds) */
        int track_forced = (forced_flags && *ntracks < 8) ? forced_flags[*ntracks] : 0;
        int track_hi = (hi_flags && *ntracks < 8) ? hi_flags[*ntracks] : 0;
        
        tracks[*ntracks].forced = track_forced;
        tracks[*ntracks].hi = track_hi;
        tracks[*ntracks].last_pts = AV_NOPTS_VALUE;

        int track_delay_ms = 0;
        int cli_track_delay = subtitle_delay_ms;
        if (ctx->delay_vals && *ntracks < delay_count)
            cli_track_delay = ctx->delay_vals[*ntracks];
        tracks[*ntracks].effective_delay_ms = track_delay_ms + cli_track_delay;

        if (debug_level > 0) {
            LOG(1,
                "Track %s lang=%s delay=%dms (auto=%d + cli=%d)\n",
                tok, tok_lang, tracks[*ntracks].effective_delay_ms,
                track_delay_ms, subtitle_delay_ms);
        }

        if (!use_ass) {
            /* Configure parser with robustness settings */
            SRTParserConfig cfg = {
                .use_ass = 0,
                .video_w = video_w,
                .video_h = video_h,
                .validation_level = SRT_VALIDATE_AUTO_FIX,
                .max_line_length = 200,
                .max_line_count = 5,
                .auto_fix_duplicates = 1,
                .auto_fix_encoding = 1,
                .warn_on_short_duration = 1,
                .warn_on_long_duration = 1
            };
            
            int64_t t0 = bench_now();
            int count = parse_srt_cfg(tok, &tracks[*ntracks].entries, qc, &cfg);
            if (bench_mode) {
                int64_t delta_parse = bench_now() - t0;
                bench_add_parse_us(delta_parse);
            }
            if (count < 0) {
                LOG(0, "Failed to parse SRT file '%s': invalid SRT format or file not found\n", tok);
                ctx_cleanup(ctx);
                return 1;
            }
            tracks[*ntracks].count = count;
            if (debug_level > 0) {
                LOG(1, "Parsed %d cues from SRT '%s' for track %d\n", count, tok, *ntracks);
            }
        }
#ifdef HAVE_LIBASS
        else {
            /* Initialize libass library and renderer if not already done */
            if (!ctx->ass_lib) {
                /* Validate and apply fallback dimensions if video_w/video_h are invalid */
                int ass_render_w = video_w > 0 ? video_w : 1920;
                int ass_render_h = video_h > 0 ? video_h : 1080;
                
                ctx->ass_lib = render_ass_init();
                if (!ctx->ass_lib) {
                    LOG(0, "Failed to initialize libass library for ASS rendering\n");
                    ctx_cleanup(ctx);
                    return -1;
                }
                ctx->ass_renderer = render_ass_renderer(ctx->ass_lib, ass_render_w, ass_render_h);
                if (!ctx->ass_renderer) {
                    LOG(0, "Failed to create ASS renderer with dimensions %dx%d\n", ass_render_w, ass_render_h);
                    ctx_cleanup(ctx);
                    return -1;
                }
                if (video_w <= 0 || video_h <= 0) {
                    LOG(1, "Warning: video dimensions unknown, using fallback %dx%d for ASS rendering\n", 
                        ass_render_w, ass_render_h);
                }
            }
            
            /* Create new track for this subtitle stream */
            tracks[*ntracks].ass_track = render_ass_new_track(ctx->ass_lib);
            if (!tracks[*ntracks].ass_track) {
                LOG(0, "Failed to create ASS track for subtitle file '%s'\n", tok);
                ctx_cleanup(ctx);
                return -1;
            }
            
            /* Apply rendering style (font, colors, etc.) */
            render_ass_set_style(tracks[*ntracks].ass_track,
                                 cli_font,
                                 cli_fontsize,
                                 cli_fgcolor,
                                 cli_outlinecolor,
                                 cli_shadowcolor);
            if (debug_level > 0)
                render_ass_debug_styles(tracks[*ntracks].ass_track);

            /* Configure parser with robustness settings */
            SRTParserConfig cfg = {
                .use_ass = 1,
                .video_w = video_w,
                .video_h = video_h,
                .validation_level = SRT_VALIDATE_AUTO_FIX,
                .max_line_length = 200,
                .max_line_count = 5,
                .auto_fix_duplicates = 1,
                .auto_fix_encoding = 1,
                .warn_on_short_duration = 1,
                .warn_on_long_duration = 1
            };
            
            SRTEntry *entries = NULL;
            int64_t t0 = bench_now();
            int count = parse_srt_cfg(tok, &entries, qc, &cfg);
            if (bench_mode) {
                int64_t delta_parse = bench_now() - t0;
                bench_add_parse_us(delta_parse);
            }
            tracks[*ntracks].entries = entries;
            tracks[*ntracks].count = count;
            for (int j = 0; j < count; j++) {
                char *ass_text = srt_html_to_ass(entries[j].text);
                char *plain = strip_tags(ass_text);
                if (plain) free(plain);
                if (!ass_text && entries[j].text)
                    ass_text = strdup(entries[j].text);
                if (!ass_text)
                {
                    LOG(1, "Warning: unable to allocate ASS text for track %s cue %d\n",
                        tracks[*ntracks].filename, j);
                    continue;
                }
                render_ass_add_event(tracks[*ntracks].ass_track,
                                     ass_text,
                                     entries[j].start_ms + tracks[*ntracks].effective_delay_ms,
                                     entries[j].end_ms + tracks[*ntracks].effective_delay_ms);
                free(ass_text);
            }
        }
#endif
        /* Create output stream and configure codec/encoder */
        tracks[*ntracks].stream = avformat_new_stream(ctx->out_fmt, NULL);
        if (!tracks[*ntracks].stream) {
            LOG(0, "Failed to create output stream for track %s\n", tok);
            ctx_cleanup(ctx);
            return -1;
        }
        tracks[*ntracks].stream->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
        tracks[*ntracks].stream->codecpar->codec_id = AV_CODEC_ID_DVB_SUBTITLE;
        tracks[*ntracks].stream->time_base = (AVRational){1, 90000};
        av_dict_set(&tracks[*ntracks].stream->metadata, "language", tok_lang_ptr, 0);
        if (tracks[*ntracks].forced)
            av_dict_set(&tracks[*ntracks].stream->metadata, "forced", "1", 0);
        if (tracks[*ntracks].hi)
            av_dict_set(&tracks[*ntracks].stream->metadata, "hearing_impaired", "1", 0);

        tracks[*ntracks].codec_ctx = avcodec_alloc_context3(codec);
        if (!tracks[*ntracks].codec_ctx) {
            LOG(0, "Failed to alloc codec context for track %s\n", tok);
            ctx_cleanup(ctx);
            return -1;
        }
        tracks[*ntracks].codec_ctx->time_base = (AVRational){1, 90000};
        tracks[*ntracks].codec_ctx->width = video_w;
        tracks[*ntracks].codec_ctx->height = video_h;

        if (enc_threads <= 0)
            tracks[*ntracks].codec_ctx->thread_count = get_cpu_count();
        else
            tracks[*ntracks].codec_ctx->thread_count = enc_threads;
#if defined(FF_THREAD_FRAME)
        tracks[*ntracks].codec_ctx->thread_type = FF_THREAD_FRAME;
#elif defined(FF_THREAD_SLICE)
        tracks[*ntracks].codec_ctx->thread_type = FF_THREAD_SLICE;
#endif

        if (avcodec_open2(tracks[*ntracks].codec_ctx, codec, NULL) < 0) {
            LOG(0, "Failed to open DVB subtitle encoder for track %s\n", tok);
            ctx_cleanup(ctx);
            return -1;
        }

        if (debug_level > 0) {
            LOG(1, "Opened DVB encoder for track %d (%s, lang=%s): w=%d h=%d\n",
                *ntracks,
                tracks[*ntracks].filename,
                tracks[*ntracks].lang,
                tracks[*ntracks].codec_ctx->width,
                tracks[*ntracks].codec_ctx->height);
        }

        (*ntracks)++;
        ctx->ntracks = *ntracks;

        tok = strtok_r(NULL, ",", save_srt);
        tok_lang = strtok_r(NULL, ",", save_lang);
    }
#pragma GCC diagnostic pop
    return 0;
}




/*
 * ctx_demux_mux_loop
 *
 * Extracted demux/mux loop. This helper owns its internal progress
 * tracking variables and uses values from the provided MainCtx and
 * passed parameters. It returns 0 on success or a negative error code
 * on failure. Keeping this defined before main() avoids needing a
 * separate prototype declaration and minimizes risk when refactoring.
 */
static int ctx_demux_mux_loop(struct MainCtx *ctx, SubTrack tracks[], int ntracks,
                         int video_w, int video_h, int use_ass,
                         int cli_fontsize, int64_t input_start_pts90)
{
    AVFormatContext *in_fmt = ctx->in_fmt;
    AVFormatContext *out_fmt = ctx->out_fmt;
    AVPacket *pkt = ctx->pkt;
    int debug_level = ctx->debug_level;
    int bench_mode = ctx->bench_mode;
    int render_threads = ctx->render_threads;
    const char *cli_font = ctx->cli_font;
    const char *cli_font_style = ctx->cli_font_style;
    const char *cli_fgcolor = ctx->cli_fgcolor;
    const char *cli_outlinecolor = ctx->cli_outlinecolor;
    const char *cli_shadowcolor = ctx->cli_shadowcolor;
    const char *cli_bgcolor = ctx->cli_bgcolor;
    const char *palette_mode = ctx->palette_mode;

    /* Track first video PTS (90k) so we can emit a tiny blank subtitle when
     * the first video packet is observed. Keep them marked as used so
     * builds with -Werror (unused-variable) succeed even if the current
     * demux path doesn't use them. */
    int seen_first_video = 0;
    int64_t first_video_pts90 = AV_NOPTS_VALUE;
    (void)seen_first_video;
    (void)first_video_pts90;

    /* Progress tracking state used to present user-facing progress lines. */
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

    while (av_read_frame(in_fmt, pkt) >= 0)
    {
        if (stop_requested)
        {
            if (debug_level > 0)
                LOG(1, "stop requested (signal), breaking demux loop\n");
            av_packet_unref(pkt);
            break;
        }
        pkt_count++;

        if (pkt->pts == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE)
        {
            pkt->pts = pkt->dts;
        }

        /* Bounds-check stream index before dereferencing streams array; skip
         * malformed packets that reference invalid stream indices. */
        if (pkt->stream_index < 0 || pkt->stream_index >= (int)in_fmt->nb_streams) {
            LOG(2, "skipping packet with invalid stream_index=%d (nb_streams=%u)\n",
                pkt->stream_index, in_fmt->nb_streams);
            av_packet_unref(pkt);
            continue;
        }

        int64_t cur90 = (pkt->pts == AV_NOPTS_VALUE) ? AV_NOPTS_VALUE : av_rescale_q(pkt->pts, in_fmt->streams[pkt->stream_index]->time_base, (AVRational){1, 90000});
        if (cur90 != AV_NOPTS_VALUE)
            last_valid_cur90 = cur90;
        int64_t cmp90 = (cur90 != AV_NOPTS_VALUE) ? cur90 : last_valid_cur90;

        if ((pkt_count & pkt_progress_mask) == 0)
        {
            time_t now = time(NULL);
            emit_progress(debug_level, now, prog_start_time, &last_progress_time,
                         pkt_count, subs_emitted, total_duration_pts90,
                         input_start_pts90, last_valid_cur90, 1 /* use_pkt_count */);
        }

        for (int t = 0; t < ntracks; t++)
        {
            if (debug_level > 2) {
                if (tracks[t].cur_sub < tracks[t].count) {
                    int64_t next_pts90 = input_start_pts90 + ((tracks[t].entries[tracks[t].cur_sub].start_ms + tracks[t].effective_delay_ms) * 90);
                    LOG(3, "[diag] cur90=%lld next_cue_pts90=%lld (track=%d cur_sub=%d)\n", (long long)cur90, (long long)next_pts90, t, tracks[t].cur_sub);
                } else {
                    LOG(3, "[diag] no more cues for track %d (cur_sub=%d count=%d)\n", t, tracks[t].cur_sub, tracks[t].count);
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
                    if (tracks[t].codec_ctx)
                    {
                        if (tracks[t].codec_ctx->width > 0)
                            render_w = tracks[t].codec_ctx->width;
                        if (tracks[t].codec_ctx->height > 0)
                            render_h = tracks[t].codec_ctx->height;
                    }
                    int cue_align = tracks[t].entries[tracks[t].cur_sub].alignment;
                    int used_align = cue_align;
                    if (!use_ass && cue_align >= 7 && cue_align <= 9)
                    {
                        used_align = cue_align - 6; /* 7->1,8->2,9->3 */
                        if (debug_level > 0)
                            LOG(1, "[main-debug] remapping cue align %d -> %d for DVB render\n",
                                cue_align, used_align);
                    }
                    if (debug_level > 0)
                    {
                        LOG(1,
                            "about to render cue %d: render_w=%d render_h=%d codec_w=%d codec_h=%d video_w=%d video_h=%d align=%d used_align=%d\n",
                            tracks[t].cur_sub,
                            render_w, render_h,
                            tracks[t].codec_ctx ? tracks[t].codec_ctx->width : -1,
                            tracks[t].codec_ctx ? tracks[t].codec_ctx->height : -1,
                            video_w, video_h,
                            cue_align, used_align);
                    }
                    if ((video_w <= 0 || video_h <= 0) && debug_level > 0)
                    {
                        LOG(1, "Warning: video size unknown, using fallback %dx%d for rendering\n", render_w, render_h);
                    }
                    if (render_threads > 0)
                    {
                        Bitmap tmpb = {0};
                        int got = render_pool_try_get(t, tracks[t].cur_sub, &tmpb);
                        if (got == 1)
                        {
                            bm = tmpb; /* use the async result */
                        }
                        else if (got == 0)
                        {
                            bm = render_pool_render_sync(markup,
                                                         render_w, render_h,
                                                         cli_fontsize, cli_font,
                                                         cli_font_style,
                                                         cli_fgcolor, cli_outlinecolor, cli_shadowcolor, cli_bgcolor,
                                                         used_align,
                                                         palette_mode);
                        }
                        else
                        {
                            const int PREFETCH_WINDOW = 8;
                            for (int pi = 0; pi < PREFETCH_WINDOW; ++pi)
                            {
                                int qi = tracks[t].cur_sub + pi;
                                if (qi >= tracks[t].count)
                                    break;
                                char *pm = srt_to_pango_markup(tracks[t].entries[qi].text);
                                render_pool_submit_async(t, qi,
                                                         pm,
                                                         render_w, render_h,
                                                         cli_fontsize, cli_font,
                                                         cli_font_style,
                                                         cli_fgcolor, cli_outlinecolor, cli_shadowcolor, cli_bgcolor,
                                                         used_align,
                                                         palette_mode);
                                free(pm);
                            }
                            if (render_pool_try_get(t, tracks[t].cur_sub, &tmpb) == 1)
                            {
                                bm = tmpb;
                            }
                            else
                            {
                                bm = render_pool_render_sync(markup,
                                                             render_w, render_h,
                                                             cli_fontsize, cli_font,
                                                             cli_font_style,
                                                             cli_fgcolor, cli_outlinecolor, cli_shadowcolor, cli_bgcolor,
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
                                               cli_font_style,
                                               cli_fgcolor, cli_outlinecolor, cli_shadowcolor, cli_bgcolor,
                                               used_align,
                                               palette_mode);
                    }
                    if (bench_mode)
                    {
                        int64_t delta = bench_now() - t1;
                        bench_add_render_us(delta);
                        bench_inc_cues_rendered();
                    }
                    free(markup);
                }
#ifdef HAVE_LIBASS
                else
                {
                    int64_t t1 = bench_now();
                    int64_t now_ms = tracks[t].entries[tracks[t].cur_sub].start_ms;
                    bm = render_ass_frame(ctx->ass_renderer, tracks[t].ass_track,
                                          now_ms, palette_mode);
                    if (bench_mode)
                    {
                        int64_t delta = bench_now() - t1;
                        bench_add_render_us(delta);
                        bench_inc_cues_rendered();
                    }
                }
#endif

                int track_delay_ms = tracks[t].effective_delay_ms;
                char pngfn[PATH_MAX] = "";
                if (debug_level > 1)
                {
                    if (make_png_filename(pngfn, sizeof(pngfn), __srt_png_seq++, t, tracks[t].cur_sub) == 0) {
                        save_bitmap_png(&bm, pngfn);
                        LOG(2, "[png] SRT bitmap saved: %s (x=%d y=%d w=%d h=%d)\n", pngfn, bm.x, bm.y, bm.w, bm.h);
                    }
                    if (tracks[t].cur_sub < tracks[t].count && tracks[t].entries[tracks[t].cur_sub].text) {
                        LOG(2, "[png] cue idx=%d text='%s'\n", tracks[t].cur_sub, tracks[t].entries[tracks[t].cur_sub].text);
                    }
                }
                if (debug_level > 0) {
                    int64_t dbg_start_ms = tracks[t].entries[tracks[t].cur_sub].start_ms;
                    LOG(1, "rendered track=%d cue=%d start_ms=%d (delay=%d)\n", t, tracks[t].cur_sub, (int)dbg_start_ms, tracks[t].effective_delay_ms);
                }

                AVSubtitle *sub = make_subtitle(bm,
                                                tracks[t].entries[tracks[t].cur_sub].start_ms,
                                                tracks[t].entries[tracks[t].cur_sub].end_ms);
                if (sub)
                {
                    sub->start_display_time = 0;
                    sub->end_display_time =
                        (tracks[t].entries[tracks[t].cur_sub].end_ms -
                         tracks[t].entries[tracks[t].cur_sub].start_ms);

                    int64_t pts90 = input_start_pts90 + ((tracks[t].entries[tracks[t].cur_sub].start_ms +
                                                          track_delay_ms) *
                                                         90);
                    if (debug_level > 0) {
                        LOG(1, "[dbg] encoding track=%d cue=%d pts90=%lld (ms=%lld)\n", t, tracks[t].cur_sub, (long long)pts90, (long long)(pts90/90));
                    }

                    encode_and_write_subtitle(tracks[t].codec_ctx,
                                              out_fmt,
                                              &tracks[t],
                                              sub,
                                              pts90,
                                              bench_mode,
                                              (debug_level > 1 ? pngfn : NULL));

                    subs_emitted++;
                    if (debug_level > 1)
                    {
                        LOG(2, "[png] SRT bitmap saved: %s\n", pngfn);
                        LOG(2, "[subs] Cue %d on %s: PTS=%lld ms, dur=%d ms, delay=%d ms\n",
                               tracks[t].cur_sub,
                               tracks[t].filename,
                               (long long)(pts90 / 90),
                               sub->end_display_time,
                               track_delay_ms);
                    }

                    /* Emit progress after each subtitle emission using common helper */
                    time_t now = time(NULL);
                    emit_progress(debug_level, now, prog_start_time, &last_progress_time,
                                 pkt_count, subs_emitted, total_duration_pts90,
                                 input_start_pts90, last_valid_cur90, 0 /* no pkt_count */);


                    avsubtitle_free(sub);
                    av_free(sub);

                    if (bm.idxbuf)
                        av_free(bm.idxbuf);
                    if (bm.palette)
                        av_free(bm.palette);
                }

                AVSubtitle *clr = av_mallocz(sizeof(*clr));
                if (clr)
                {
                    clr->format = 0;
                    clr->start_display_time = 0;
                    clr->end_display_time = 1; /* minimal duration */
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
                        LOG(1, "[subs] CLEAR cue %d on %s @ %lld ms\n",
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

        if (pkt->stream_index >= 0 && pkt->stream_index < (int)in_fmt->nb_streams)
        {
            AVStream *out_st = out_fmt->streams[pkt->stream_index];
            if (!out_st) {
                LOG(2, "output stream %d is NULL, skipping packet\n", pkt->stream_index);
                av_packet_unref(pkt);
                continue;
            }
            pkt->stream_index = out_st->index;

            int64_t t5 = bench_now();
            int mux_ret = safe_av_interleaved_write_frame(out_fmt, pkt);
            if (bench_mode)
            {
                int64_t delta_mux = bench_now() - t5;
                bench_add_mux_us(delta_mux);
                if (mux_ret >= 0)
                    bench_inc_packets_muxed();
            }
        }
        av_packet_unref(pkt);
    }

    /* Force final progress update at 100% completion before exiting demux loop */
    if (debug_level == 0)
    {
        time_t now = time(NULL);
        /* Temporarily clear last_progress_time to force output regardless of throttle */
        time_t saved_time = last_progress_time;
        last_progress_time = now - 2; /* Ensure now - last_progress_time >= 1 */
        emit_progress(debug_level, now, prog_start_time, &last_progress_time,
                     pkt_count, subs_emitted, total_duration_pts90,
                     input_start_pts90, 
                     (total_duration_pts90 != AV_NOPTS_VALUE) ? (input_start_pts90 + total_duration_pts90) : last_valid_cur90,
                     0 /* no pkt_count */);
        fprintf(stdout, "\n"); /* Newline to separate progress from subsequent output */
        (void)saved_time; /* Mark as used to avoid compiler warnings */
    }

    return 0;
}

/**
 * ctx_run_qc_only - Perform a quick-check (QC) on a list of SRT subtitle files.
 *
 * This function parses and validates one or more SRT files, reporting summary statistics
 * such as the number of cues and errors per file. It does not produce output media, but
 * instead logs QC results to both stdout and a file ("qc_log.txt"). The function supports
 * benchmarking and debug output, and handles resource management for all allocated memory.
 *
 * Parameters:
 *   ctx         - Pointer to the MainCtx structure (must not be NULL).
 *   srt_list    - Comma-separated list of SRT file paths.
 *   lang_list   - Comma-separated list of language codes corresponding to each SRT file.
 *   forced      - Flag indicating whether to mark cues as forced.
 *   hi          - Flag indicating whether to mark cues as hearing-impaired.
 *   bench_mode  - Flag to enable benchmarking of parsing operations.
 *   debug_level - Debug verbosity level.
 *
 * Returns:
 *   0 on success, 1 on error (e.g., invalid arguments, allocation failure, file open error).
 *
 * Behavior:
 *   - Opens a QC log file for writing.
 *   - Splits input lists into arrays of filenames and language codes.
 *   - For each SRT file:
 *       - Parses the file and counts cues and errors.
 *       - Logs per-file statistics.
 *       - Frees all allocated subtitle entry text.
 *   - Prints and logs a summary of all tracks and totals.
 *   - Cleans up all allocated resources.
 *   - Reports benchmark results if enabled.
 */
static int ctx_run_qc_only(struct MainCtx *ctx,
                       const char *srt_list,
                       const char *lang_list,
                       int forced,
                       int hi,
                       int bench_mode,
                       int debug_level)
{
    /*
     * Checks if any of the required pointers (ctx, srt_list, lang_list) are NULL.
     * Returns 1 if any pointer is NULL, indicating an error or invalid input.
     */
    if (!ctx || !srt_list || !lang_list)
        return 1;

    /**
     * Opens a file named "qc_log.txt" in write mode and assigns the file pointer to 'qc'.
     * This file is intended for logging quality control information.
     * If the file cannot be opened, 'qc' will be set to NULL.
     */
    FILE *qc = fopen("qc_log.txt", "w");

    /**
     * Checks if the 'qc' pointer is NULL. If it is, logs an error message to "qc_log.txt"
     * using perror and returns 1 to indicate failure.
     */
    if (!qc)
    {
        perror("qc_log.txt");
        return 1;
    }
    /*
     * Assigns the value of 'qc' to the 'qc' member of the 'ctx' structure.
     * This sets up the context with the provided 'qc' value for further processing.
     */
    ctx->qc = qc;

#define RETURN_QC(code) do { \
        if (qc) { fclose(qc); qc = NULL; ctx->qc = NULL; } \
        return (code); \
    } while (0)

    char *srt_copy = strdup(srt_list);
    char *lang_copy = strdup(lang_list);
    if (!srt_copy || !lang_copy)
    {
        LOG(1, "Out of memory duplicating QC file lists\n");
        free(srt_copy);
        free(lang_copy);
        RETURN_QC(1);
    }

    char *save_s = NULL, *save_l = NULL;
    char *p = strtok_r(srt_copy, ",", &save_s);
    char *q = strtok_r(lang_copy, ",", &save_l);

    char **fnames = NULL;
    char **langs = NULL;
    size_t nfiles = 0;

    bool alloc_failure = false;
    while (p)
    {
        char **nf = realloc(fnames, sizeof(*fnames) * (nfiles + 1));
        if (!nf) { alloc_failure = true; break; }
        fnames = nf;

        char **nl = realloc(langs, sizeof(*langs) * (nfiles + 1));
        if (!nl) { alloc_failure = true; break; }
        langs = nl;

        char *fname_dup = strdup(p);
        if (!fname_dup) { alloc_failure = true; break; }

        const char *lang_src = q ? q : "";
        char *lang_dup = strdup(lang_src);
        if (!lang_dup) {
            free(fname_dup);
            alloc_failure = true;
            break;
        }

        fnames[nfiles] = fname_dup;
        langs[nfiles] = lang_dup;
        nfiles++;
        p = strtok_r(NULL, ",", &save_s);
        q = strtok_r(NULL, ",", &save_l);
    }

    free(srt_copy);
    free(lang_copy);

    if (alloc_failure)
    {
        LOG(1, "Out of memory while collecting QC filenames\n");
        for (size_t i = 0; i < nfiles; ++i) {
            free(fnames[i]);
            free(langs[i]);
        }
        free(fnames);
        free(langs);
        RETURN_QC(1);
    }

    int total_cues = 0;
    int total_errors = 0;

    struct qc_summary
    {
        char *filename;
        int cues;
        int errors;
    };
    struct qc_summary *summaries = calloc((size_t)nfiles, sizeof(*summaries));
    if (!summaries)
    {
        LOG(1, "Out of memory allocating QC summary table\n");
        for (size_t i = 0; i < nfiles; ++i) {
            free(fnames[i]);
            free(langs[i]);
        }
        free(fnames);
        free(langs);
        RETURN_QC(1);
    }

    for (size_t i = 0; i < nfiles; ++i)
    {
        qc_reset_counts();
        SRTEntry *entries = NULL;
        
        /* Configure parser with robustness settings */
        SRTParserConfig cfg = {
            .use_ass = 0,
            .video_w = 1920,
            .video_h = 1080,
            .validation_level = SRT_VALIDATE_AUTO_FIX,
            .max_line_length = 200,
            .max_line_count = 5,
            .auto_fix_duplicates = 1,
            .auto_fix_encoding = 1,
            .warn_on_short_duration = 1,
            .warn_on_long_duration = 1
        };
        
        SRTParserStats stats = {0};
        
        int64_t t0 = bench_now();
        int count = parse_srt_with_stats(fnames[i], &entries, qc, &cfg, &stats);
        if (bench_mode) {
            int64_t delta_parse = bench_now() - t0;
            bench_add_parse_us(delta_parse);
        }
        int file_errors = qc_error_count;
        if (count < 0) {
            LOG(1, "QC: Failed to parse '%s': invalid SRT format or file not found\n", fnames[i]);
            count = 0;
            file_errors++;
        }

        summaries[i].filename = fnames[i];
        summaries[i].cues = count;
        summaries[i].errors = file_errors;

        total_cues += count;
        total_errors += file_errors;

        if (debug_level > 0)
            printf("QC-only: %s (%s), cues=%d forced=%d hi=%d errors=%d\n",
                   fnames[i], langs[i], count, forced, hi, file_errors);

        /* Output parser diagnostics for this file */
        printf("\n=== Parser Diagnostics for '%s' ===\n", fnames[i]);
        srt_report_stats(&stats, stdout);
        if (count > 0) {
            srt_analyze_gaps(entries, count, stdout);
            srt_print_timing_summary(entries, count, stdout, 10);
        }
        printf("\n");

        for (int j = 0; j < count; j++)
            free(entries[j].text);
        free(entries);
        free(langs[i]);
    }

    free(fnames);
    free(langs);

    int max_name_len = 0;
    for (size_t i = 0; i < nfiles; ++i)
    {
        size_t raw_len = summaries[i].filename ? strlen(summaries[i].filename) : 0;
        int l = (raw_len > (size_t)INT_MAX) ? INT_MAX : (int)raw_len;
        if (l > max_name_len)
            max_name_len = l;
    }

    printf("SRT Quick-Check Summary:\n");
    for (size_t i = 0; i < nfiles; ++i)
    {
        printf("  Track %zu: %-*s  cues=%6d  errors=%4d\n",
               i, max_name_len, summaries[i].filename ? summaries[i].filename : "",
               summaries[i].cues, summaries[i].errors);
    }
    printf("  TOTAL: %-*s  cues=%6d  errors=%4d\n",
           max_name_len, "", total_cues, total_errors);

    if (qc)
    {
        fprintf(qc, "SRT Quick-Check Summary:\n");
        for (size_t i = 0; i < nfiles; ++i)
        {
            fprintf(qc, "Track %zu: %-*s cues=%d errors=%d\n",
                    i, max_name_len, summaries[i].filename ? summaries[i].filename : "",
                    summaries[i].cues, summaries[i].errors);
        }
        fprintf(qc, "TOTAL: cues=%d errors=%d\n", total_cues, total_errors);
    }

    for (size_t i = 0; i < nfiles; ++i)
        free(summaries[i].filename);
    free(summaries);

    if (bench_mode)
        bench_report();

    RETURN_QC(0);
#undef RETURN_QC
}

/**
 * ctx_cleanup - Cleans up and releases all resources associated with the given MainCtx structure.
 *
 * This function performs a comprehensive cleanup of the application's main context, including:
 * - Stopping worker threads and freeing per-track codec contexts.
 * - Releasing memory for language strings, filenames, subtitle entries, and associated buffers.
 * - Cleaning up ASS/LibASS renderer and track resources if enabled.
 * - Freeing subtitle delay lists and palette/color/font configuration strings, unless they are default values.
 * - Closing output and input formats, network deinitialization, and freeing AVPacket.
 * - Closing QC file if open.
 * - Unreferencing Pango default font map and cleaning up fontconfig resources, either statically or dynamically.
 * - Reporting benchmark results if enabled.
 * - Flushing stdout.
 *
 * param ctx Pointer to the MainCtx structure to be cleaned up. If NULL, no action is taken.
 */
static void ctx_cleanup(struct MainCtx *ctx)
{
    if (!ctx) return;
    /* Always stop render infrastructure so worker threads and TLS state are released. */
    render_pool_shutdown();
    render_pango_cleanup();

    /* Stop workers and free codec contexts per-track */
    if (ctx->tracks) {
        for (int t = 0; t < ctx->ntracks; t++) {
            if (ctx->tracks[t].codec_ctx)
                avcodec_free_context(&ctx->tracks[t].codec_ctx);
        }

        for (int t = 0; t < ctx->ntracks; t++) {
            if (ctx->tracks[t].lang) {
                free((void *)ctx->tracks[t].lang);
                ctx->tracks[t].lang = NULL;
            }
            if (ctx->tracks[t].filename) {
                free((void *)ctx->tracks[t].filename);
                ctx->tracks[t].filename = NULL;
            }
            if (ctx->tracks[t].entries) {
                for (int j = 0; j < ctx->tracks[t].count; j++) {
                    if (ctx->tracks[t].entries[j].text) {
                        free(ctx->tracks[t].entries[j].text);
                        ctx->tracks[t].entries[j].text = NULL;
                    }
                }
                free(ctx->tracks[t].entries);
                ctx->tracks[t].entries = NULL;
            }
#ifdef HAVE_LIBASS
            if (ctx->tracks[t].ass_track) {
                render_ass_free_track(ctx->tracks[t].ass_track);
                ctx->tracks[t].ass_track = NULL;
            }
#endif
            if (ctx->tracks[t].enc_tmpbuf) {
                av_free(ctx->tracks[t].enc_tmpbuf);
                ctx->tracks[t].enc_tmpbuf = NULL;
                ctx->tracks[t].enc_tmpbuf_size = 0;
                ctx->tracks[t].enc_tmpbuf_full_count = 0;
            }
        }
    }
    ctx->ntracks = 0;
    ctx->tracks = NULL;

#ifdef HAVE_LIBASS
    if (ctx->ass_renderer) {
        render_ass_free_renderer(ctx->ass_renderer);
        ctx->ass_renderer = NULL;
    }
    if (ctx->ass_lib) {
        render_ass_free_lib(ctx->ass_lib);
        ctx->ass_lib = NULL;
    }
#endif

    if (ctx->srt_list) {
        free(ctx->srt_list);
        ctx->srt_list = NULL;
    }
    if (ctx->lang_list) {
        free(ctx->lang_list);
        ctx->lang_list = NULL;
    }
    if (ctx->palette_mode && strcmp(ctx->palette_mode, "broadcast") != 0) {
        free((void *)ctx->palette_mode);
    }
    ctx->palette_mode = NULL;
    if (ctx->cli_font && strcmp(ctx->cli_font, "Open Sans") != 0) {
        free((void *)ctx->cli_font);
    }
    ctx->cli_font = NULL;
    if (ctx->cli_font_style) {
        free((void *)ctx->cli_font_style);
        ctx->cli_font_style = NULL;
    }
    if (ctx->cli_fgcolor && strcmp(ctx->cli_fgcolor, "#FFFFFF") != 0) {
        free((void *)ctx->cli_fgcolor);
    }
    ctx->cli_fgcolor = NULL;
    if (ctx->cli_outlinecolor && strcmp(ctx->cli_outlinecolor, "#000000") != 0) {
        free((void *)ctx->cli_outlinecolor);
    }
    ctx->cli_outlinecolor = NULL;
    if (ctx->cli_shadowcolor && strcmp(ctx->cli_shadowcolor, "#64000000") != 0) {
        free((void *)ctx->cli_shadowcolor);
    }
    ctx->cli_shadowcolor = NULL;
    if (ctx->cli_bgcolor) {
        free((void *)ctx->cli_bgcolor);
    }
    ctx->cli_bgcolor = NULL;
    if (ctx->cli_forced_list) {
        free(ctx->cli_forced_list);
        ctx->cli_forced_list = NULL;
    }
    if (ctx->cli_hi_list) {
        free(ctx->cli_hi_list);
        ctx->cli_hi_list = NULL;
    }
    if (ctx->subtitle_delay_list) {
        free(ctx->subtitle_delay_list);
        ctx->subtitle_delay_list = NULL;
    }
    if (ctx->delay_vals) {
        free(ctx->delay_vals);
        ctx->delay_vals = NULL;
    }
    if (ctx->service_name) {
        free(ctx->service_name);
        ctx->service_name = NULL;
    }
    if (ctx->service_provider) {
        free(ctx->service_provider);
        ctx->service_provider = NULL;
    }

    if (ctx->out_fmt) {
        if (ctx->out_fmt->pb)
            avio_closep(&ctx->out_fmt->pb);
        avformat_free_context(ctx->out_fmt);
        ctx->out_fmt = NULL;
    }
    avformat_close_input(&ctx->in_fmt);
    avformat_network_deinit();

    if (ctx->qc) {
        fclose(ctx->qc);
        ctx->qc = NULL;
    }
    av_packet_free(&ctx->pkt);

    /* Try to unref Pango default fontmap and cleanup fontconfig as before */
    {
        void *pango = dlopen("libpango-1.0.so.0", RTLD_LAZY | RTLD_LOCAL);
        void *gobj = dlopen("libgobject-2.0.so.0", RTLD_LAZY | RTLD_LOCAL);
        if (pango && gobj) {
            void *(*pango_font_map_get_default_f)(void) = dlsym(pango, "pango_font_map_get_default");
            void (*g_object_unref_f)(void *) = dlsym(gobj, "g_object_unref");
            if (pango_font_map_get_default_f && g_object_unref_f) {
                void *map = pango_font_map_get_default_f();
                if (map) {
                    if (ctx->debug_level > 1)
                        LOG(2, "unref() pango default font map\n");
                    g_object_unref_f(map);
                }
            }
        }
        if (pango) dlclose(pango);
        if (gobj) dlclose(gobj);
    }

    {
#ifdef HAVE_FONTCONFIG
        if (ctx->debug_level > 1)
            LOG(2, "calling FcFini() (linked) to cleanup fontconfig\n");
        FcFini();
#else
        void *fc = dlopen("libfontconfig.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (!fc) fc = dlopen("libfontconfig.so", RTLD_LAZY | RTLD_LOCAL);
        if (fc) {
            void (*FcFini_f)(void) = (void (*)(void))dlsym(fc, "FcFini");
            if (FcFini_f) {
                if (ctx->debug_level > 1)
                    LOG(2, "calling FcFini() to cleanup fontconfig\n");
                FcFini_f();
            }
            dlclose(fc);
        }
#endif
    }

    if (ctx->bench_mode) {
        bench_report();
        ctx->bench_mode = 0;
    }

    fprintf(stdout, "\n");
    fflush(stdout);
}

/*
 * main
 *
 * Orchestrate end-to-end conversion of one or more SRT inputs into DVB subtitle
 * streams muxed alongside an existing transport stream.
 *
 * Description:
 *   Implements the CLI interface for `srt2dvbsub`. Parses command-line options,
 *   validates inputs, configures rendering/encoding pipelines (optionally
 *   leveraging libass and asynchronous render workers), and muxes the resulting
 *   DVB subtitle streams into an MPEG-TS container. Provides both normal mux
 *   operation and a QC-only mode that runs parser/heuristic checks without
 *   producing output media.
 *
 * Behavior:
 *   - Uses `getopt_long` to parse flags such as `--input`, `--srt`, `--languages`,
 *     rendering controls, debugging toggles, and benchmarking options.
 *   - Validates required options, normalizes common user mistakes (e.g., `-srt`),
 *     and verifies language codes via `is_valid_dvb_lang`.
 *   - Adjusts libav logging verbosity to match the selected debug level.
 *   - Initializes render pools, signal handlers, and optional QC logging.
 *   - Executes two primary flows:
 *       • QC-only: parses each SRT file, performs quality validation, writes
 *         summaries (stdout and optional qc_log.txt), and exits without muxing.
 *       • Normal mux: opens the input container via libavformat, mirrors A/V
 *         streams in the output, parses SRT tracks (Pango or libass rendering),
 *         encodes DVB subtitles, and interleaves them with pass-through packets.
 *   - Provides progress feedback (percentage/ETA or packet counts) in quiet mode
 *     and detailed diagnostics when verbose debugging is enabled.
 *   - Performs exhaustive cleanup: renderer teardown, codec context free,
 *     libav context closure, optional fontconfig finalization, and benchmark
 *     reporting when requested.
 *   - Returns 0 on success or a non-zero status for user errors or runtime
 *     failures.
 *
 * Side effects:
 *   - Allocates and frees numerous heap objects (option strings, SRT entries,
 *     libav/libass structures, render buffers).
 *   - Performs file I/O on the input TS, output TS, optional qc_log.txt, and
 *     debug PNG directories.
 *   - Emits stdout/stderr messages for progress, diagnostics, warnings, and
 *     errors; may overwrite the current console line for progress updates.
 *   - Registers `render_pool_shutdown` with `atexit`, installs SIGINT/SIGTERM
 *     handlers, and manipulates global state (`debug_level`, `bench`, etc.).
 *
 * Thread safety and reentrancy:
 *   - Not reentrant; relies on global/static state and process-wide libraries.
 *   - Spawns worker threads when `--render-threads > 0`; synchronization is
 *     handled within render_pool helpers but concurrent invocation of `main` is
 *     undefined.
 *
 * Error handling:
 *   - Validates user input early, printing descriptive errors and exiting with
 *     status 1 on misuse.
 *   - Checks return codes from libav, rendering, and file operations; on
 *     failure, reports the issue and exits with non-zero status (commonly -1).
 *   - In QC-only mode, errors in individual files are tallied and reported, but
 *     the program continues processing remaining files when possible.
 *
 * Intended use:
 *   - Invoked as a standalone CLI tool by users or scripts to merge subtitles
 *     into broadcast TS outputs or to batch-validate SRT assets. Modify this
 *     function when adding new command-line options or altering the overall
 *     processing pipeline.
 */
int main(int argc, char **argv)
{
    int ret = 0; /* return value: 0=ok, non-zero on error */
    bool ctx_cleaned = false;
    
    print_version();

    /* Move important cleanup-sensitive declarations here so early returns
     * don't land on uninitialized automatic variables. */
    FILE *qc = NULL; /* QC log file pointer (opened in QC-only mode) */
    int ntracks = 0; /* number of subtitle tracks (initialized as we parse) */
    int *delay_vals = NULL; /* parsed per-track delay values */
    /*
     * Pointers to input and output file paths.
     * 
     * input  Pointer to the input file path string. Initialized to NULL.
     * output Pointer to the output file path string. Initialized to NULL.
     */
    const char *input = NULL, *output = NULL;

    /*
     * srt_list: Pointer to a string containing a list of SRT subtitle files.
     * lang_list: Pointer to a string containing a list of language codes corresponding to the SRT files.
     */
    char *srt_list = NULL, *lang_list = NULL;
    
    /*
     * Variables for subtitle processing options:
     * - forced_list: Comma-separated forced flags per track (e.g., "0,1,0" or "1")
     * - hi_list: Comma-separated hearing-impaired flags per track (e.g., "0,0,1")
     * - qc_only: Indicates if only quality control subtitles should be processed.
     * - bench_mode: Enables benchmarking mode for performance testing.
     */
    char *forced_list = NULL, *hi_list = NULL;
    int qc_only = 0, bench_mode = 0;
    
    /*
     * Specifies the palette mode to be used for subtitle rendering.
     * This setting determines how colors are mapped for DVB subtitles.
     */
    const char *palette_mode = "broadcast";

    /*
     * The delay in milliseconds to apply to subtitles before displaying them.
     * This can be used to synchronize subtitles with video playback.
     */
    int subtitle_delay_ms = 0;

    /*
     * Pointer to a string containing a list of subtitle delays.
     * This variable is initialized to NULL and is intended to store
     * delay values for subtitles, possibly as a comma-separated list.
     */
    char *subtitle_delay_list = NULL;

    /* Subtitle style config (shared for ASS and Pango) */
    const char *cli_font = "Open Sans";
    const char *cli_font_style = NULL;

    /* 0 means "not set" — let render_pango compute dynamic sizing based on resolution */
    int cli_fontsize = 0;

    /*
     * cli_fgcolor: Foreground color for subtitle text, specified as a hex string compatible with Pango.
     * cli_outlinecolor: Outline color for subtitle text, specified as a hex string.
     * cli_shadowcolor: Shadow color for subtitle text, specified as a hex string with alpha channel.
     * cli_bgcolor: Background color for subtitle box, specified as #RRGGBB (6 hex digits, always opaque).
     */
    const char *cli_fgcolor = "#FFFFFF"; /* hex for Pango */
    const char *cli_outlinecolor = "#000000";
    const char *cli_shadowcolor = "#64000000";
    const char *cli_bgcolor = NULL;  /* Background color: #RRGGBB format (optional, default transparent) */

    /* Context to own top-level allocations as they're created. Populate
     * fields as CLI options are parsed so early failures can safely call
     * ctx_cleanup(&ctx) without leaking previously allocated strings.
     */
    struct MainCtx ctx = {0};
    ctx.palette_mode = palette_mode;
    ctx.cli_font = cli_font;
    ctx.cli_font_style = cli_font_style;
    ctx.cli_fgcolor = cli_fgcolor;
    ctx.cli_outlinecolor = cli_outlinecolor;
    ctx.cli_shadowcolor = cli_shadowcolor;
    ctx.cli_bgcolor = cli_bgcolor;

    /*
     * ass_lib: Pointer to the ASS_Library instance, used for managing libass library resources.
     * ass_renderer: Pointer to the ASS_Renderer instance, responsible for rendering ASS subtitles.
     */
#ifdef HAVE_LIBASS    
    /* libass state is owned via ctx.ass_lib / ctx.ass_renderer */
#endif

    int parse_status = cli_parse(argc, argv,
                                      &ctx,
                                      &input,
                                      &output,
                                      &srt_list,
                                      &lang_list,
                                      &forced_list,
                                      &hi_list,
                                      &qc_only,
                                      &bench_mode,
                                      &palette_mode,
                                      &subtitle_delay_ms,
                                      &subtitle_delay_list,
                                      &cli_fontsize,
                                      &cli_font,
                                      &cli_font_style,
                                      &cli_fgcolor,
                                      &cli_outlinecolor,
                                      &cli_shadowcolor,
                                      &cli_bgcolor,
                                      &debug_level,
                                      &use_ass);
    if (parse_status >= 0)
        return finalize_main(&ctx, ctx_cleaned, parse_status);

    /*
     * Configure libav logging verbosity to match our --debug level. We map
     * our conservative 0/1/2 levels to libav's levels to avoid noisy output
     * except when the user explicitly requests diagnostics. This controls
     * messages produced by libavformat/avcodec during probing/encoding. 
     */
    if (debug_level > 1)
        av_log_set_level(AV_LOG_INFO);
    else if (debug_level == 1)
        av_log_set_level(AV_LOG_ERROR);
    else
        av_log_set_level(AV_LOG_QUIET);

    /* Validate and resolve font and style */
    char *resolved_font = NULL;
    char *resolved_style = NULL;
    if (validate_and_resolve_font(cli_font, cli_font_style, &resolved_font, &resolved_style) != 0) {
        ret = 1;
        ctx_cleanup(&ctx);
        return finalize_main(&ctx, ctx_cleaned, ret);
    }
    ctx.cli_font = resolved_font;
    ctx.cli_font_style = resolved_style;
    
    /* Output encoding status with font information */
    printf("Encoding the subtitles with font: %s", resolved_font);
    if (resolved_style) {
        printf(" and style: %s\n\n", resolved_style);
    } else {
        printf(" and style: (default)\n\n");
    }

    /*
     * Starts the benchmarking timer to measure the execution time of subsequent code.
     */
    bench_start();
    bench_set_enabled(bench_mode);    /* 
     * Apply user-specified render tuning before any renderers are created.
     * - ssaa_override: forces the supersampling multiplier used by the
     *   Pango renderer. Larger values improve edge quality at the cost of CPU.
     * - no_unsharp: disable the final unsharp pass which may eat into
     *   small glyphs on low-resolution videos; exposed for debugging.
     */
    if (ssaa_override > 0)
        render_pango_set_ssaa_override(ssaa_override);
    if (no_unsharp)
        render_pango_set_no_unsharp(1);

    /* Initialize the asynchronous render pool when the user requests
     * multiple render workers. The render pool provides two modes:
     *  - async: submit jobs up to a prefetch window and later fetch finished
     *    bitmaps (good throughput for long runs)
     *  - sync fallback: if an async job was submitted but not ready we can
     *    render synchronously to avoid missing emission deadlines.
     *
     * Defensive atexit() registration:
     * - render_pool_shutdown() is registered with atexit() ONLY on successful
     *   initialization (render_pool_init returns 0).
     * - On normal exit (success or error via finalize_main), ctx_cleanup()
     *   explicitly calls render_pool_shutdown(), so the atexit handler is
     *   usually redundant.
     * - The atexit registration serves as a safety net for abnormal exits
     *   (signal, early return, uncaught exception, etc.) where ctx_cleanup()
     *   may not run. Since render_pool_shutdown() is idempotent (safe to call
     *   multiple times), duplicate calls are harmless.
     * - On initialization failure, render_pool_shutdown() is NOT registered,
     *   and render_threads is set to 0 (sync-only mode).
     */
    if (render_threads > 0)
    {
        if (render_pool_init(render_threads) != 0)
        {
            LOG(1, "Warning: failed to initialize render pool with %d threads\n", render_threads);
            render_threads = 0;
        }
        else
        {
            atexit(render_pool_shutdown);
        }
    }

    /* 
     * Install simple signal handlers so Ctrl-C triggers orderly shutdown.
     * The handler only sets a sig_atomic_t flag; the main loop polls this
     * flag and exits gracefully when set. 
     */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = ctx_signal_request_stop;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    ctx.bench_mode = bench_mode;
    ctx.debug_level = debug_level;
    ctx.render_threads = render_threads;

    /* Create a dedicated directory for PNG debug dumps when debug mode is
     * enabled. We tolerate EEXIST for idempotency across multiple runs. On
     * restricted filesystems we warn but continue (PNG emission is optional).
     */
    if (debug_level > 0)
    {
        char png_err[256] = {0};
        /* Initialize PNG output directory (defaults to "pngs/" if not set via --png-dir) */
        int ret = init_png_path(NULL, png_err);
        if (ret != 0) {
            LOG(1, "Warning: PNG directory initialization: %s\n", png_err);
        } else {
            LOG(1, "PNG output directory: %s\n", get_png_output_dir());
        }
    }

    if (qc_only)
    {
        int qc_ret = ctx_run_qc_only(&ctx, srt_list, lang_list, 0, 0, bench_mode, debug_level);
        return finalize_main(&ctx, ctx_cleaned, qc_ret);
    }

    /* ---------- Normal mux flow ----------
     * Perform early initialization (open input, probe streams, create out_fmt,
     * tokenize SRT/LANG lists and parse per-track delay list). The heavy
     * per-track work remains in the loop below. */
    SubTrack tracks[8] = {0};
    ctx.tracks = tracks;
    ctx.ntracks = ntracks;
    int delay_count = 0;
    char *save_srt = NULL, *save_lang = NULL;
    char *tok = NULL, *tok_lang = NULL;
    int64_t input_start_pts90 = 0;
    const AVCodec *codec = NULL;
    AVFormatContext *out_fmt = NULL;

    ret = ctx_init(&ctx, input, output,
                        srt_list, lang_list,
                        tracks, &ntracks,
                        &video_w, &video_h,
                        &tok, &tok_lang,
                        &save_srt, &save_lang,
                        &delay_count,
                        subtitle_delay_ms, subtitle_delay_list,
                        debug_level,
                        &input_start_pts90,
                        &codec);
    if (ret != 0) {
        ctx_cleanup(&ctx);
        return ret;
    }

    /* mirror ctx-owned format contexts and parsed delay_vals into local vars for the rest of main */
    out_fmt = ctx.out_fmt;
    delay_vals = ctx.delay_vals;

    /* Initialize empty flag arrays - will be populated after we know track count */
    int forced_flags[8] = {0};
    int hi_flags[8] = {0};

    ret = ctx_parse_tracks(&ctx, tracks, &ntracks, tok, tok_lang, &save_srt, &save_lang,
                                delay_count, subtitle_delay_ms, debug_level,
                                video_w, video_h, codec, forced_flags, hi_flags, use_ass,
                                bench_mode, qc, cli_fontsize, cli_font,
                                cli_font_style,
                                cli_fgcolor, cli_outlinecolor, cli_shadowcolor,
                                enc_threads);
    if (ret != 0) {
        /* ctx_parse_tracks already cleaned up ctx on failure */
        ctx_cleaned = true;
        return ret;
    }

    /* Parse per-track forced/hi flags from CLI context now that we know ntracks */
    LOG(2, "DEBUG: After ctx_parse_tracks: ntracks=%d, ctx.cli_forced_list=%s, ctx.cli_hi_list=%s\n", 
        ntracks, ctx.cli_forced_list ? ctx.cli_forced_list : "(null)", 
        ctx.cli_hi_list ? ctx.cli_hi_list : "(null)");
    
    if (parse_flag_list(ctx.cli_forced_list, ntracks, forced_flags, 8) != 0) {
        LOG(0, "Failed to parse forced flags (ntracks=%d, ctx.cli_forced_list=%s)\n", 
            ntracks, ctx.cli_forced_list ? ctx.cli_forced_list : "(null)");
        ctx_cleanup(&ctx);
        return 1;
    }
    if (parse_flag_list(ctx.cli_hi_list, ntracks, hi_flags, 8) != 0) {
        LOG(0, "Failed to parse hearing-impaired flags (ntracks=%d, ctx.cli_hi_list=%s)\n",
            ntracks, ctx.cli_hi_list ? ctx.cli_hi_list : "(null)");
        ctx_cleanup(&ctx);
        return 1;
    }

    /* Apply parsed flags to tracks */
    for (int i = 0; i < ntracks; i++) {
        tracks[i].forced = forced_flags[i];
        tracks[i].hi = hi_flags[i];
    }

    /* Validate that duplicate language codes have different flags */
    for (int i = 0; i < ntracks; i++) {
        for (int j = i + 1; j < ntracks; j++) {
            if (strcmp(tracks[i].lang, tracks[j].lang) == 0) {
                /* Same language code - check if flags are different */
                int flags_same = (tracks[i].forced == tracks[j].forced) && 
                               (tracks[i].hi == tracks[j].hi);
                if (flags_same) {
                    LOG(0, "Error: Tracks %d and %d both have language '%s' with identical flags\n",
                        i, j, tracks[i].lang);
                    LOG(0, "       Duplicate language codes require different --forced or --hi flags\n");
                    ctx_cleanup(&ctx);
                    return 1;
                }
            }
        }
    }

    /* Open the output file (unless the muxer uses I/O callbacks). This
     * step creates the output AVIO context used by avformat to write
     * packets. We fail early if the file cannot be created. */
    if (!(out_fmt->oformat->flags & AVFMT_NOFILE))
    {
        if (avio_open(&out_fmt->pb, output, AVIO_FLAG_WRITE) < 0)
        {
            LOG(1, "Error: could not open output file %s\n", output);
            return finalize_main(&ctx, ctx_cleaned, -1);
        }
    }

    /*
     * Dictionary for storing options to be passed to the muxer.
     * Used to configure muxing parameters in libav operations.
     */
    AVDictionary *mux_opts = NULL;

    /*
     * Set muxer options for output stream:
     * - "max_delay": Sets the maximum delay in microseconds (here, 800000 µs ≈ 700 ms PCR lead).
     * - "copyts": Enables copying input timestamps to output.
     * - "start_at_zero": Forces output timestamps to start at zero.
     */
    av_dict_set(&mux_opts, "max_delay", "800000", 0); /* ~700 ms PCR lead */
    av_dict_set(&mux_opts, "copyts", "1", 0);
    av_dict_set(&mux_opts, "start_at_zero", "1", 0);

    if (ctx.mux_rate > 0)
    {
        char muxrate_buf[32];
        snprintf(muxrate_buf, sizeof(muxrate_buf), "%lld", (long long)ctx.mux_rate);
        av_dict_set(&mux_opts, "muxrate", muxrate_buf, 0);
    }
    if (ctx.service_name)
        av_dict_set(&mux_opts, "service_name", ctx.service_name, 0);
    if (ctx.service_provider)
        av_dict_set(&mux_opts, "service_provider", ctx.service_provider, 0);

    /* Write the container header. This emits stream headers for the output
     * format and must succeed before we attempt to write interleaved packets.
     */
    if (avformat_write_header(out_fmt, &mux_opts) < 0)
    {
        LOG(1, "Error: could not write header for output file\n");
        av_dict_free(&mux_opts);
        return finalize_main(&ctx, ctx_cleaned, -1);
    }

    /*
     * Frees all entries in the AVDictionary pointed to by mux_opts and sets the pointer to NULL.
     * This is typically used to release memory allocated for dictionary options after they are no longer needed.
     *
     * @param mux_opts Pointer to an AVDictionary structure to be freed.
     */
    av_dict_free(&mux_opts);

    /*
     * Allocates an AVPacket structure and returns a pointer to it.
     * The AVPacket is used to store compressed data (such as audio or video frames)
     * and related metadata for processing in libav libraries.
     * 
     * Returns:
     *   A pointer to an AVPacket structure, or NULL if allocation fails.
     */
    AVPacket *pkt = av_packet_alloc();
    ctx.pkt = pkt;

    ctx.delay_vals = delay_vals;
    ctx.qc = qc;
    ctx.bench_mode = bench_mode;
    ctx.debug_level = debug_level;
    ctx.render_threads = render_threads;

    ret = ctx_demux_mux_loop(&ctx, tracks, ntracks,
                         video_w, video_h, use_ass,
                         cli_fontsize, input_start_pts90);
    if (ret != 0)
        return finalize_main(&ctx, ctx_cleaned, ret);

    /* Finalize the output stream(s) and write trailer metadata.
     * av_write_trailer() flushes delayed packets and writes any format
     * specific trailer information (indexes, timestamps) required by the
     * container. After this call the output file is logically complete and
     * no further packets should be written.
     */
    av_write_trailer(out_fmt);

    return finalize_main(&ctx, ctx_cleaned, ret);
}

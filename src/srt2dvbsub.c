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
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <dlfcn.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <wchar.h>
#include <wctype.h>
#include <locale.h>



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
#include "version.h"

#ifdef HAVE_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif
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
 * struct dvb_lang_entry
 *
 * Represents a DVB language entry containing:
 * - code: ISO language code as a string (e.g., "eng" for English).
 * - ename: English name of the language.
 * - native: Native name of the language.
 */
struct dvb_lang_entry
{
    const char *code;
    const char *ename;
    const char *native;
};

/*
 * dvb_langs
 *
 * Lookup table mapping DVB three-letter language codes to their English and
 * native-language names.
 *
 * Description:
 *   Static array of `struct dvb_lang_entry` instances used to validate incoming
 *   language identifiers and to present friendly names in help output. Each
 *   element supplies the canonical DVB code, an English label, and a native
 *   script/localized label. The sequence is terminated by a sentinel entry with
 *   all fields set to NULL.
 *
 * Behavior:
 *   - Resides in read-only storage for the lifetime of the program.
 *   - Intended for iteration until the sentinel is encountered.
 *   - Consumers treat the `code` field as case-sensitive three-letter ASCII
 *     strings.
 *
 * Side effects:
 *   - None by itself; serves purely as static data.
 *
 * Thread safety and reentrancy:
 *   - Immutable table; safe for concurrent readers.
 *
 * Error handling:
 *   - Sentinel entry prevents overruns during iteration when code checks rely on
 *     null pointers.
 *
 * Intended use:
 *   - Support language validation, option parsing, and user-facing displays.
 */
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

/*
 * is_valid_dvb_lang
 *
 * Validate a three-letter DVB language code against the compiled-in lookup
 * table.
 *
 * Description:
 *   Confirm that the provided string is non-null, exactly three characters in
 *   length, and composed solely of alphabetic characters. If it passes those
 *   structural checks, a lowercase copy is matched against the entries in
 *   `dvb_langs`, returning success on the first hit.
 *
 * Behavior:
 *   - Rejects null pointers and strings that are not precisely three characters.
 *   - Converts each character to lowercase using the C locale and stores it in
 *     a temporary buffer.
 *   - Performs a linear search through `dvb_langs`, comparing with `strcmp`.
 *   - Returns 1 for a match, 0 otherwise.
 *
 * Side effects:
 *   - None beyond reading `code` and the `dvb_langs` table; no I/O or heap use.
 *
 * Thread safety and reentrancy:
 *   - Read-only operation; safe for concurrent calls when `dvb_langs` remains
 *     immutable.
 *
 * Error handling:
 *   - Treats null inputs, non-alphabetic characters, or incorrect length as
 *     invalid and returns 0.
 *
 * Intended use:
 *   - Validate user-supplied language codes prior to muxing metadata or subtitle
 *     streams.
 */
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

/* Some platforms may not expose wcwidth prototype without extra feature macros.
 * Provide a fallback declaration to avoid implicit declaration warnings. */
extern int wcwidth(wchar_t wc);

/* Return the display width (columns) of a UTF-8 string using wcwidth().
 * Falls back to strlen() for bytes when multibyte handling fails. */
static int utf8_display_width(const char *s)
{
    if (!s) return 0;
    /* Ensure locale is set for wcwidth */
    static int locale_set = 0;
    if (!locale_set) { setlocale(LC_CTYPE, ""); locale_set = 1; }

    mbstate_t st;
    memset(&st, 0, sizeof(st));
    const unsigned char *p = (const unsigned char*)s;
    int width = 0;
    while (*p) {
        wchar_t wc;
        size_t mblen = mbrtowc(&wc, (const char*)p, MB_CUR_MAX, &st);
        if (mblen == (size_t)-1 || mblen == (size_t)-2 || mblen == 0) {
            /* invalid sequence or null; treat byte as width 1 and advance */
            width += 1;
            p++;
            memset(&st, 0, sizeof(st));
            continue;
        }
        int w = wcwidth(wc);
        if (w < 0) w = 1;
        width += w;
        p += mblen;
    }
    return width;
}

/*
 * Prints the build version information to standard output.
 *
 * This function outputs the build version, commit hash, and build date
 * using the predefined macros GIT_VERSION, GIT_COMMIT, and GIT_DATE.
 */
static void print_version(void)
{
    printf("\nsrt2dvbsub Version: %s (%s, %s)\n\n", GIT_VERSION, GIT_COMMIT, GIT_DATE);
}

/*
 * print_help
 *
 * Print the command-line usage guide, available switches, and recognized DVB
 * language codes to standard output.
 *
 * Description:
 *   Emit a multi-section help message covering invocation syntax, option
 *   descriptions (including conditional libass support), and the language
 *   table constructed from `dvb_langs`. Intended to orient users on how to
 *   invoke the tool and configure subtitle rendering.
 *
 * Behavior:
 *   - Writes formatted text and newline-terminated lines to stdout with printf.
 *   - Enumerates each known command-line flag along with expected arguments.
 *   - Iterates through `dvb_langs` until a sentinel entry to list language
 *     codes and their English/native names.
 *   - Does not return a value and accepts no parameters.
 *
 * Side effects:
 *   - Produces console output on stdout; content may interleave with other
 *     output if called concurrently.
 *   - Does not allocate heap memory or mutate global program state beyond I/O.
 *
 * Thread safety and reentrancy:
 *   - Not safe for signal handlers because `printf` is not async-signal-safe.
 *   - Concurrent invocations from multiple threads may interleave; serialize if
 *     atomic presentation is required.
 *
 * Error handling:
 *   - Does not inspect or propagate errors from the underlying stdio calls.
 *
 * Intended use:
 *   - Respond to `--help`, `-h`, or `-?` requests, or accompany argument
 *     validation failures where guidance should be displayed.
 */
static void print_help(void)
{
    printf("Usage: srt2dvbsub --input in.ts --output out.ts --srt subs.srt[,subs2.srt] --languages eng[,deu] [options]\n\n");
    printf("Options:\n");
    printf("  -I, --input FILE            Input media file\n");
    printf("  -o, --output FILE           Output TS file\n");
    printf("  -s, --srt FILES             Comma-separated SRT files\n");
    printf("  -l, --languages CODES       Comma-separated 3-letter DVB language codes\n");
#ifdef HAVE_LIBASS
    printf("      --ass                   Enable libass rendering\n");
#endif
    printf("      --forced                Mark subtitle streams as forced\n");
    printf("      --hi                    Mark subtitle streams as hearing-impaired\n");
    printf("      --qc-only               Run srt file quality checks only (no mux)\n");
    printf("      --palette MODE          Palette mode (broadcast|web|legacy)\n");
    printf("      --font FONTNAME         Set font family (default is Robo if installed)\n");
    printf("      --fontsize N            Set font size in px (overrides dynamic sizing)\n");
    printf("      --fgcolor #RRGGBB       Text color\n");
    printf("      --outlinecolor #RRGGBB  Outline color\n");
    printf("      --shadowcolor #AARRGGBB Shadow color (alpha optional)\n");
    printf("      --ssaa N                Force supersample factor (1..24) (default 6)\n");        
    printf("      --delay MS[,MS2,...]    Global or per-track subtitle delay in milliseconds (comma-separated list)\n");
    printf("      --enc-threads N         Encoder thread count (0=auto)\n");
    printf("      --render-threads N      Parallel render workers (0=single-thread)\n");
    printf("      --no-unsharp            Disable the final unsharp pass to speed rendering\n");
    printf("      --bench                 Enable micro-bench timing output\n");    
    printf("      --debug N               Set debug verbosity (0=quiet,1=errors,2=verbose)\n");
    printf("      --license               Show license information and exit\n");
    printf("  -h, --help, -?              Show this help and exit\n\n");
    printf("Accepted DVB language codes:\n");
    printf("  Code  English / Native\n");
    printf("  ----  ----------------\n");
    /* Print language table in dynamic columns based on terminal width. */
    int lang_count = 0;
    while (dvb_langs[lang_count].code) lang_count++;
    if (lang_count == 0) {
        printf("\n");
    } else {
        /* Build formatted entry strings and compute max length. */
        char **entries = calloc((size_t)lang_count, sizeof(char*));
        size_t *elens = calloc((size_t)lang_count, sizeof(size_t));
        size_t maxlen = 0; /* measured in display columns */
        for (int i = 0; i < lang_count; i++) {
            struct dvb_lang_entry *p = &dvb_langs[i];
            /* Format: "code  English / Native" */
            size_t need = snprintf(NULL, 0, "%s  %s / %s", p->code, p->ename, p->native) + 1;
            entries[i] = malloc(need);
            snprintf(entries[i], need, "%s  %s / %s", p->code, p->ename, p->native);
            /* compute visual display width (handles UTF-8 and wide chars) */
            elens[i] = (size_t)utf8_display_width(entries[i]);
            if (elens[i] > maxlen) maxlen = elens[i];
        }

        /* Determine terminal width */
        int term_w = 80;
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) term_w = ws.ws_col;
        else {
            char *cols_env = getenv("COLUMNS");
            if (cols_env) term_w = atoi(cols_env);
        }

        const int gap = 4; /* spaces between columns */
        int cols = term_w / (int)(maxlen + gap);
        if (cols < 1) cols = 1;
        if (cols > lang_count) cols = lang_count;

        /* Try to fit by reducing columns until the computed column widths fit. */
        int rows = 0;
        size_t *col_widths = NULL;
        while (cols >= 1) {
            rows = (lang_count + cols - 1) / cols;
            /* compute per-column width */
            free(col_widths);
            col_widths = calloc((size_t)cols, sizeof(size_t));
            for (int c = 0; c < cols; c++) {
                size_t w = 0;
                for (int r = 0; r < rows; r++) {
                    int idx = c * rows + r;
                    if (idx >= lang_count) break;
                    if (elens[idx] > w) w = elens[idx];
                }
                col_widths[c] = w;
            }
            /* total width */
            size_t total = 0;
            for (int c = 0; c < cols; c++) total += col_widths[c];
            total += (size_t)gap * (size_t)(cols - 1);
            if ((int)total <= term_w) break; /* fits */
            cols--; /* try fewer columns */
            if (cols == 0) cols = 1;
        }

        /* Print rows */
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                int idx = c * rows + r;
                if (idx >= lang_count) break;
                /* left-align in column width using display widths */
                printf("  %s", entries[idx]);
                /* pad with spaces based on display width, not byte length */
                int vis = utf8_display_width(entries[idx]);
                int pad = (int)col_widths[c] - vis;
                if (pad > 0) {
                    for (int s = 0; s < pad; s++) putchar(' ');
                }
                if (c < cols - 1) {
                    for (int s = 0; s < gap; s++) putchar(' ');
                }
            }
            printf("\n");
        }

        for (int i = 0; i < lang_count; i++) free(entries[i]);
        free(entries); free(elens); free(col_widths);
    }
    printf("\n");
}

/*
 * print_license
 *
 * Print the software license and related contact information to standard output.
 *
 * Description:
 *   Emit a multi-line, human-readable license notice to stdout. The notice
 *   describes permitted personal, educational, and non-commercial use, states
 *   that commercial use requires a separate commercial license, and includes
 *   a warranty disclaimer and contact information for obtaining a commercial
 *   license.
 *
 * Behavior:
 *   - Writes formatted text directly to stdout using the C standard library.
 *   - Produces multiple newline-terminated lines; exact visual formatting is
 *     implementation-defined but intended for console display.
 *   - Does not return a value and does not accept parameters.
 *
 * Side effects:
 *   - Performs I/O on stdout; may interleave with other output from the
 *     application if called concurrently.
 *   - Does not allocate heap memory or modify program state other than writing
 *     to standard output.
 *
 * Thread safety and reentrancy:
 *   - Not safe for use in signal handlers (printf is not async-signal-safe).
 *   - Concurrent calls from multiple threads may interleave; synchronize if
 *     atomic output is required.
 *
 * Error handling:
 *   - Does not check or propagate errors from the underlying I/O operations.
 *
 * Intended use:
 *   - Displaying license information to the user (for example via a --license
 *     or help command). Update the text if license terms or contact details
 *   change.
 */
static void print_license(void)
{
    printf("\n");
    printf("Copyright (c) 2025 Mark E. Rosche, Chili IPTV Systems\n");
    printf("All rights reserved.\n");
    printf("\n");
    printf("This software is licensed under the \" Personal Use License \" described below.\n");
    printf("\n");
    printf("────────────────────────────────────────────────────────────────\n");
    printf("PERSONAL USE LICENSE\n");
    printf("────────────────────────────────────────────────────────────────\n");
    printf("Permission is hereby granted, free of charge, to any individual person\n");
    printf("using this software for personal, educational, or non-commercial purposes,\n");
    printf("to use, copy, modify, merge, publish, and/or build upon this software,\n");
    printf("provided that this copyright and license notice appears in all copies\n");
    printf("or substantial portions of the Software.\n");
    printf("\n");
    printf("────────────────────────────────────────────────────────────────\n");
    printf("COMMERCIAL USE\n");
    printf("────────────────────────────────────────────────────────────────\n");
    printf("Commercial use of this software, including but not limited to:\n");
    printf("  • Incorporation into a product or service sold for profit,\n");
    printf("  • Use within an organization or enterprise in a revenue-generating activity,\n");
    printf("  • Modification, redistribution, or hosting as part of a commercial offering,\n");
    printf("requires a separate **Commercial License** from the copyright holder.\n");
    printf("\n");
    printf("To obtain a commercial license, please contact:\n");
    printf("  [Mark E. Rosche | Chili-IPTV Systems]\n");
    printf("  Email: [license@chili-iptv.info]\n");
    printf("  Website: [www.chili-iptv.info]\n");
    printf("\n");
    printf("────────────────────────────────────────────────────────────────\n");
    printf("DISCLAIMER\n");
    printf("────────────────────────────────────────────────────────────────\n");
    printf("THIS SOFTWARE IS PROVIDED \" AS IS \", WITHOUT WARRANTY OF ANY KIND,\n");
    printf("EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES\n");
    printf("OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.\n");
    printf("IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,\n");
    printf("DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,\n");
    printf("ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER\n");
    printf("DEALINGS IN THE SOFTWARE.\n");
    printf("\n");
    printf("────────────────────────────────────────────────────────────────\n");
    printf("Summary:\n");
    printf("  ✓ Free for personal, educational, and hobbyist use.\n");
    printf("  ✗ Commercial use requires a paid license.\n");
    printf("────────────────────────────────────────────────────────────────\n");
    printf("\n");
}



/* Global debug level (0=quiet, 1=errors, 2=verbose diagnostics) */
int debug_level = 0;
/* Toggle: use libass rendering path when non-zero */
int use_ass = 0;
/* Current video width/height used for rendering decisions. These start with
 * conservative defaults and are updated after probing the input file. */
int video_w = 720, video_h = 480;

/* Performance / quality knobs (defined in runtime_opts.c) */

/* Monotonic sequence used when writing debug PNG filenames. */
static int __srt_png_seq = 0;

/* signal handling: set this flag in the handler and check in main loops */
static volatile sig_atomic_t stop_requested = 0;
/*
 * Simple signal handler used to request an orderly shutdown. The handler
 * must be async-signal-safe and therefore only sets a sig_atomic_t flag.
 */
static void handle_signal(int sig)
{
    (void)sig;
    stop_requested = 1;
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
    print_version();
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
     * - forced: Indicates if only forced subtitles should be processed.
     * - hi: Indicates if hearing-impaired subtitles should be included.
     * - qc_only: Indicates if only quality control subtitles should be processed.
     * - bench_mode: Enables benchmarking mode for performance testing.
     */
    int forced = 0, hi = 0, qc_only = 0, bench_mode = 0;
    
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
    const char *cli_font = "Robooto";

    /* 0 means "not set" — let render_pango compute dynamic sizing based on resolution */
    int cli_fontsize = 0;

    /*
     * cli_fgcolor: Foreground color for subtitle text, specified as a hex string compatible with Pango.
     * cli_outlinecolor: Outline color for subtitle text, specified as a hex string.
     * cli_shadowcolor: Shadow color for subtitle text, specified as a hex string with alpha channel.
     */
    const char *cli_fgcolor = "#FFFFFF"; /* hex for Pango */
    const char *cli_outlinecolor = "#000000";
    const char *cli_shadowcolor = "#64000000";

    /*
     * ass_lib: Pointer to the ASS_Library instance, used for managing libass library resources.
     * ass_renderer: Pointer to the ASS_Renderer instance, responsible for rendering ASS subtitles.
     */
#ifdef HAVE_LIBASS    
    ASS_Library *ass_lib = NULL;
    ASS_Renderer *ass_renderer = NULL;
#endif

    /*
     * Array of command-line options for the srt2dvbsub program.
     *
     * This array defines the supported long options for the command-line interface,
     * including input/output files, subtitle formats, language selection, forced subtitles,
     * hearing-impaired mode, debugging, quality control, benchmarking, palette selection,
     * font settings, color customization, delay, threading options, supersampling,
     * sharpening control, and license information.
     *
     * Some options are conditionally included based on library availability (e.g., HAVE_LIBASS).
     *
     * Each option is defined with its name, argument requirement, flag, and corresponding short or unique code.
     */
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
        {"license", no_argument, 0, 1017},
        {"help", no_argument, 0, 'h'},
        {"?", no_argument, 0, '?'},
        {0, 0, 0, 0}};

    int opt, long_index = 0;

    /*
     * Parses command-line options using getopt_long and sets corresponding variables.
     *
     * Unrecognized options or errors will print the help message and return an error code.
     */
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
            /* Keep the integer parse for compatibility (first value) and
             * also save the raw string for per-track parsing later. */
            subtitle_delay_ms = atoi(optarg);
            if (subtitle_delay_list)
                free(subtitle_delay_list);
            subtitle_delay_list = strdup(optarg);
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
        case 1017:
            print_license();
            return 0;
        default:
            /* Use the central help printer so the text remains consistent
             * (and respects whether libass was enabled at configure-time). */
            print_help();
            return 1;
        }
    }

    /* Common user mistake: calling the short option as "-srt" (no space)
     * results in optarg being "rt" and the actual SRT path left as a
     * non-option argv. Detect that case and auto-correct so the tool is
     * more forgiving to interactive users and scripts that expect this
     * compatibility behavior.
     *
     * Note: we strdup() earlier when parsing -s so we must free and replace
     * the saved string here to avoid leaking the initial accidental value.
     */
    if (srt_list && strcmp(srt_list, "rt") == 0)
    {
            if (optind < argc && argv[optind] && argv[optind][0] != '-')
        {
            free(srt_list);
            srt_list = strdup(argv[optind]);
            if (debug_level > 0)
                fprintf(stderr, "Auto-corrected '-srt' to use '%s' as SRT path\n", srt_list);
            optind++; /* consume this arg so callers expecting remaining args are correct */
        }
    }

    /* 
     * Validate that the user provided all mandatory CLI options. We fail
     * fast and print a clear error so automated scripts can detect misuse.
     */
    if (!input || !output || !srt_list || !lang_list)
    {
        fprintf(stderr, "Usage: --input, --output, --srt, and --languages are required.\n");
        fprintf(stderr, "See srt2dvbsub --help for more information.\n\n");
        return 1;
    }

    /*
     * Parses a comma-separated list of language codes from `lang_list`,
     * validates each code using `is_valid_dvb_lang()`, and prints an error
     * message to stderr if any code is invalid. Returns 1 and frees memory
     * on error, otherwise continues execution. Expects 3-letter DVB language codes.
     */
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

    /*
     * Starts the benchmarking timer to measure the execution time of subsequent code.
     */
    bench_start();

    /* 
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
     * We register render_pool_shutdown with atexit() as a defensive cleanup
     * so render worker threads will be stopped even on early exit paths.
     */
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

    /* 
     * Install simple signal handlers so Ctrl-C triggers orderly shutdown.
     * The handler only sets a sig_atomic_t flag; the main loop polls this
     * flag and exits gracefully when set. 
     */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Optional QC output file for writing quality-control reports. Opened
     * when --qc-only is used. */
    FILE *qc = NULL;
    if (qc_only)
    {
        /* QC-only mode writes a machine-readable report to qc_log.txt so
         * that external tools can ingest the results. Fail early if we
         * cannot open the file rather than silently continuing in QC mode. */
        qc = fopen("qc_log.txt", "w");
        if (!qc)
        {
            perror("qc_log.txt");
            return 1;
        }
    }

    /* Create a dedicated directory for PNG debug dumps when debug mode is
     * enabled. We tolerate EEXIST for idempotency across multiple runs. On
     * restricted filesystems we warn but continue (PNG emission is optional).
     */
    if (debug_level > 0)
    {
        if (mkdir("pngs", 0755) < 0 && errno != EEXIST)
        {
            fprintf(stderr, "Warning: could not create pngs/ directory: %s\n", strerror(errno));
        }
    }

    /* ---------- QC-only flow ----------
     * When --qc-only is set we parse each SRT and run quality checks without
     * performing a full mux. This is useful for validating subtitle length,
     * line wrapping, and other heuristics across many files quickly.
     */
    if (qc_only)
    {
        /* In QC-only mode we iterate over each SRT file and run parse checks
         * without initializing the heavy render/mux pipeline. This path is
         * optimized for batch validation and must free parsed text buffers
         * immediately to avoid leaking memory in long batch runs. 
         * Parse srt_list and lang_list into arrays using strtok_r to avoid
         * interference between tokenizations. This makes the QC-only path
         * robust when multiple files are provided. */
        char *srt_copy = strdup(srt_list);
        char *lang_copy = strdup(lang_list);
        char *save_s = NULL, *save_l = NULL;
        char *p = strtok_r(srt_copy, ",", &save_s);
        char *q = strtok_r(lang_copy, ",", &save_l);

        /*
         * fnames: Pointer to an array of strings, each representing a filename.
         * langs: Pointer to an array of strings, each representing a language code corresponding to the filenames.
         * nfiles: Integer representing the number of files (and languages) in the arrays.
         */
        char **fnames = NULL;
        char **langs = NULL;
        int nfiles = 0;

        /*
         * Iterates through a list of file names and corresponding language codes,
         * dynamically reallocating arrays to store them. For each file name and language
         * pair, memory is allocated and the values are copied into the arrays. If a language
         * code is missing, an empty string is stored instead. The loop continues until all
         * pairs are processed or memory allocation fails.
         *
         * Variables:
         *   - fnames: Array of file name strings.
         *   - langs: Array of language code strings.
         *   - nfiles: Number of files processed.
         *   - p: Current file name token.
         *   - q: Current language code token.
         *   - save_s, save_l: State pointers for strtok_r.
         */
        while (p) {
            char **nf = realloc(fnames, sizeof(*fnames) * (nfiles + 1));
            char **nl = realloc(langs, sizeof(*langs) * (nfiles + 1));
            if (!nf || !nl) break;
            fnames = nf; langs = nl;
            fnames[nfiles] = strdup(p);
            if (q)
                langs[nfiles] = strdup(q);
            else
                langs[nfiles] = strdup("");
            nfiles++;
            p = strtok_r(NULL, ",", &save_s);
            q = strtok_r(NULL, ",", &save_l);
        }

        /*
         * Frees the memory allocated for the copies of the SRT data and language string.
         * - srt_copy: Pointer to the duplicated SRT subtitle data.
         * - lang_copy: Pointer to the duplicated language string.
         * This prevents memory leaks by releasing resources after use.
         */
        free(srt_copy);
        free(lang_copy);

        /**
         * total_cues - Tracks the total number of cues processed.
         * total_errors - Tracks the total number of errors encountered during processing.
         */
        int total_cues = 0;
        int total_errors = 0;


        /*
         * Defines a structure 'qc_summary' to store quality control summary information for subtitle files,
         * including the filename, number of cues, and number of errors.
         * Allocates an array of 'qc_summary' structures, one for each file, using calloc to ensure memory is zero-initialized.
         * 'nfiles' specifies the number of subtitle files to process.
         */
        struct qc_summary { char *filename; int cues; int errors; };
        struct qc_summary *summaries = calloc(nfiles, sizeof(*summaries));

        /*
         * Processes multiple subtitle files by parsing each file, collecting summary statistics,
         * and performing memory cleanup.
         *
         * For each file in the input list:
         *   - Resets quality control (QC) counters.
         *   - Parses the SRT file and retrieves subtitle entries.
         *   - Measures and accumulates parsing time if benchmarking is enabled.
         *   - Updates summary statistics including filename, cue count, and error count.
         *   - Accumulates total cues and errors across all files.
         *   - Optionally prints debug information about the file and parsing results.
         *   - Frees memory allocated for subtitle entry texts, entry array, and language string.
         *
         * Variables used:
         *   - nfiles: Number of files to process.
         *   - fnames: Array of file names.
         *   - langs: Array of language strings for each file.
         *   - summaries: Array to store summary statistics for each file.
         *   - total_cues, total_errors: Accumulators for cues and errors.
         *   - debug_level: Controls verbosity of debug output.
         *   - bench_mode, bench: Used for benchmarking parsing time.
         *   - qc, qc_error_count: Quality control context and error count.
         */
        for (int i = 0; i < nfiles; ++i) {
            qc_reset_counts();
            SRTEntry *entries = NULL;
            int64_t t0 = bench_now();
            int count = parse_srt(fnames[i], &entries, qc);
            if (bench_mode)
                bench.t_parse_us += bench_now() - t0;
            if (count < 0) count = 0;
            int file_errors = qc_error_count;
            summaries[i].filename = fnames[i];
            summaries[i].cues = count;
            summaries[i].errors = file_errors;
            total_cues += count;
            total_errors += file_errors;

            if (debug_level > 0)
                printf("QC-only: %s (%s), cues=%d forced=%d hi=%d errors=%d\n",
                       fnames[i], langs[i], count, forced, hi, file_errors);

            for (int j = 0; j < count; j++)
                free(entries[j].text);
            free(entries);
            free(langs[i]);
        }

        /*
         * Frees the memory allocated for the arrays 'fnames' and 'langs'.
         * This is necessary to prevent memory leaks after these arrays are no longer needed.
         */
        free(fnames);
        free(langs);

        /* Compute max filename width so columns align nicely regardless of
         * varying filename lengths. We then use the width with the "%-*s"
         * format specifier to pad filenames to the same column. */
        int max_name_len = 0;
        
        /*
         * Iterates through the array of summary structures to determine the maximum length
         * of the filenames. For each file, it calculates the length of the filename string,
         * and if this length is greater than the current maximum, updates the maximum value.
         */
        for (int i = 0; i < nfiles; ++i) {
            int l = (int)strlen(summaries[i].filename);
            if (l > max_name_len) max_name_len = l;
        }

        printf("SRT Quick-Check Summary:\n");

        /*
         * Iterates over all subtitle track summaries and prints formatted information for each track.
         *
         * For each track, displays:
         *   - Track index
         *   - Filename (left-aligned, padded to max_name_len)
         *   - Number of cues
         *   - Number of errors
         *
         * Parameters:
         *   nfiles        - Total number of subtitle tracks.
         *   max_name_len  - Maximum length for filename display.
         *   summaries     - Array of structures containing filename, cues, and errors for each track.
         */
        for (int i = 0; i < nfiles; ++i) {
            printf("  Track %d: %-*s  cues=%6d  errors=%4d\n",
                   i, max_name_len, summaries[i].filename, summaries[i].cues, summaries[i].errors);
        }

        printf("  TOTAL: %-*s  cues=%6d  errors=%4d\n", max_name_len, "", total_cues, total_errors);

        /*
         * If the 'qc' file pointer is valid, writes a summary of SRT quick-check results to the file.
         * The summary includes:
         *   - A header line.
         *   - For each track, its index, filename (left-aligned to max_name_len), number of cues, and number of errors.
         *   - A total line showing the sum of cues and errors across all tracks.
         * Finally, closes the summary file.
         *
         * Parameters used:
         *   qc           - FILE pointer to the summary output file.
         *   nfiles       - Number of tracks/files processed.
         *   max_name_len - Maximum length for filename display.
         *   summaries    - Array of summary structs containing filename, cues, and errors for each track.
         *   total_cues   - Total number of cues across all tracks.
         *   total_errors - Total number of errors across all tracks.
         */
        if (qc) {
            fprintf(qc, "SRT Quick-Check Summary:\n");
            for (int i = 0; i < nfiles; ++i) {
                fprintf(qc, "Track %d: %-*s cues=%d errors=%d\n",
                        i, max_name_len, summaries[i].filename, summaries[i].cues, summaries[i].errors);
            }
            fprintf(qc, "TOTAL: cues=%d errors=%d\n", total_cues, total_errors);
            fclose(qc);
        }

        /*
         * Loop through each file summary and free the memory allocated for its filename.
         * This helps prevent memory leaks by releasing resources associated with each filename.
         */
        for (int i = 0; i < nfiles; ++i) {
            free(summaries[i].filename);
        }
        
        /*
         * Frees the memory allocated for the 'summaries' pointer.
         * This prevents memory leaks by releasing resources that are no longer needed.
         */
        free(summaries);

        /*
         * If bench_mode is enabled, calls bench_report() to output benchmarking information.
         */
        if (bench_mode)
            bench_report();

        return 0;
    }

    /* ---------- Normal mux flow ----------
     * Initialize libavformat/network and open the input file for probing.
     * From here the main demux loop reads packets and triggers subtitle
     * rendering/encoding based on input timestamps. */
    avformat_network_init();

    /*
     * Pointer to an AVFormatContext structure, used to represent the format context
     * for the input media file. This context contains information about the format,
     * streams, and other metadata required for processing the input.
     */
    AVFormatContext *in_fmt = NULL;

    /* Open the input container using libavformat. This will probe the file
     * headers and prepare stream structures used later to map packet PTS
     * values into our 90kHz subtitle timeline. Failure here indicates the
     * input file cannot be read or is an unsupported format. */
    if (avformat_open_input(&in_fmt, input, NULL, NULL) < 0)
    {
        fprintf(stderr, "Cannot open input\n");
        return -1;
    }
    
    /*
     * Probes the input media file to retrieve stream information and metadata.
     *
     * This function analyzes the input container referenced by 'in_fmt' and populates
     * its stream structures with codec parameters, durations, and other relevant
     * metadata required for subsequent processing. It must be called after opening
     * the input file and before accessing stream details.
     *
     * Returns 0 on success, or a negative error code on failure.
     */
    avformat_find_stream_info(in_fmt, NULL);

    int video_index = -1, first_audio_index = -1;
    int64_t input_start_pts90 = 0; /* Initialize input_start_pts90 (will recompute after streams are probed) */
    
    /* Walk discovered streams to find the first video and audio streams.
     * We capture the video dimensions (if present) to size subtitle
     * rendering appropriately. For some container types dimensions may be
     * absent and we fall back to conservative defaults (720x480). */
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

    /* Determine the 'zero' PTS for our subtitle timeline expressed in
     * 90kHz ticks. We prefer the container-level start_time; if that isn't
     * set we fall back to the video stream's start_time so subtitle PTS
     * align with video presentation. If neither are set, zero is used. */
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

    /* Allocate an output format context for MPEG-TS and replicate input
     * stream codec parameters. Subtitles will be added as separate streams
     * (one per SRT file / language pair) later. Failure here is fatal. */
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
    /* Locate the DVB subtitle encoder in libavcodec. This encoder converts
     * our in-memory DVB-style bitmaps into encoder frames suitable for the
     * MPEG-TS container. If the encoder isn't available the program cannot
     * produce DVB subtitle streams. */
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_DVB_SUBTITLE);
    if (!codec)
    {
        fprintf(stderr, "DVB subtitle encoder not found\n");
        return -1;
    }

    char *save_srt = NULL, *save_lang = NULL;
    char *tok = strtok_r(srt_list, ",", &save_srt);
    char *tok_lang = strtok_r(lang_list, ",", &save_lang);

    /* Parse per-track delay list (if provided) into integer array so we can
     * apply a per-file delay override. Values are in milliseconds. */
    int *delay_vals = NULL;
    int delay_count = 0;

    /*
     * Parses a comma-separated list of subtitle delay values from `subtitle_delay_list`
     * and stores them as integers in the dynamically allocated array `delay_vals`.
     *
     * Each value represents a per-track delay in milliseconds. The function splits the
     * input string using `strtok_r`, converts each token to an integer, and appends it
     * to the array. Memory for the array is reallocated as needed. The total number of
     * parsed delay values is stored in `delay_count`.
     *
     * This allows the program to apply individual delay adjustments to each subtitle track.
     *
     * Memory management:
     *   - The duplicated string `dlcopy` is freed after parsing.
     *   - The caller is responsible for freeing `delay_vals` after use.
     */
    if (subtitle_delay_list) {
        char *dlcopy = strdup(subtitle_delay_list);
        char *dsave = NULL;
        char *d = strtok_r(dlcopy, ",", &dsave);
        while (d) {
            int val = atoi(d);
            int *nv = realloc(delay_vals, sizeof(*delay_vals) * (delay_count + 1));
            if (!nv) break;
            delay_vals = nv;
            delay_vals[delay_count++] = val;
            d = strtok_r(NULL, ",", &dsave);
        }
        free(dlcopy);
    }
    
    /*
     * Processes subtitle tracks from input filenames and language codes, initializing up to 8 tracks.
     *
     * For each track:
     * - Initializes track fields and metadata, including language, filename, forced/HI flags, and delay.
     * - Parses SRT subtitles and stores entries; if ASS is enabled, converts SRT to ASS events and injects styles.
     * - Creates an output stream for DVB subtitles, setting codec type, codec ID, time base, and metadata.
     * - Allocates and configures an encoder context for DVB subtitles, setting dimensions and threading options.
     * - Opens the encoder instance, handling errors if allocation or opening fails.
     * - Outputs debug information for parsing, ASS conversion, and encoder initialization when enabled.
     *
     * The loop continues until all input tracks are processed or the maximum track count is reached.
     */
    while (tok && tok_lang && ntracks < 8)
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
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
        tracks[ntracks].last_pts = AV_NOPTS_VALUE; /* Initialize per-track last_pts */

        /* No auto-delay from audio start_time; use --delay for manual adjustment */
        int track_delay_ms = 0;
        /* per-track CLI delay override when provided; default to subtitle_delay_ms */
        int cli_track_delay = subtitle_delay_ms;

        /*
         * Assigns a delay value to the current track if delay values are provided and the number of tracks
         * processed so far is less than the total number of delay values available.
         * This ensures each track can have an individual delay applied from the delay_vals array.
         */
        if (delay_vals && ntracks < delay_count)
            cli_track_delay = delay_vals[ntracks];

        /* store per-track effective delay (auto + per-track CLI fine-tune) */
        tracks[ntracks].effective_delay_ms = track_delay_ms + cli_track_delay;

        /*
         * Logs track information to stderr if debug_level is greater than 0.
         *
         * The log includes:
         *   - Track name (tok)
         *   - Language (tok_lang)
         *   - Effective delay in milliseconds (tracks[ntracks].effective_delay_ms)
         *   - Automatic delay (track_delay_ms)
         *   - Command-line specified delay (subtitle_delay_ms)
         *
         * Format:
         *   [main] Track <name> lang=<lang> delay=<ms> (auto=<auto_ms> + cli=<cli_ms>)
         */
        if (debug_level > 0)
        {
            fprintf(stderr,
                    "[main] Track %s lang=%s delay=%dms (auto=%d + cli=%d)\n",
                    tok, tok_lang, tracks[ntracks].effective_delay_ms,
                    track_delay_ms, subtitle_delay_ms);
        }

        /*
         * This code block handles the parsing and processing of subtitle tracks in either SRT or ASS format.
         *
         * If ASS support is not enabled or selected (`!use_ass`):
         *   - Measures parsing time if benchmarking is enabled.
         *   - Parses SRT subtitles using `parse_srt`, storing entries and count.
         *   - Outputs debug information about the number of cues parsed.
         *
         * If ASS support is enabled (`#ifdef HAVE_LIBASS` and `else`):
         *   - Initializes the libass library and renderer if not already done.
         *   - Creates a new ASS track for the current subtitle.
         *   - Sets default ASS style parameters (font, size, colors).
         *   - Optionally outputs debug information about ASS styles.
         *   - Parses SRT subtitles, storing entries and count for timing.
         *   - Converts SRT text to ASS format, strips tags for debugging, and outputs debug information.
         *   - Adds each subtitle event to the ASS track with adjusted timing.
         *
         * The code ensures that subtitle tracks are correctly parsed and formatted for further processing,
         * supporting both plain SRT and styled ASS subtitles, with optional benchmarking and debugging.
         */
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
#ifdef HAVE_LIBASS        
        else
        {

            if (!ass_lib)
            {
                ass_lib = render_ass_init();
                ass_renderer = render_ass_renderer(ass_lib, video_w, video_h);
            }
            tracks[ntracks].ass_track = render_ass_new_track(ass_lib);

            /* Inject default ASS style (must precede any events) */
            render_ass_set_style(tracks[ntracks].ass_track,
                                 cli_font,
                                 cli_fontsize,
                                 cli_fgcolor, /* NOTE: convert in render_ass_set_style */
                                 cli_outlinecolor,
                                 cli_shadowcolor);
            if (debug_level > 0)
            {
                render_ass_debug_styles(tracks[ntracks].ass_track);
            }

            /* Parse SRT with our existing parser, inject as ASS events */
            SRTEntry *entries = NULL;
            int count = parse_srt(tok, &entries, qc);
            tracks[ntracks].entries = entries; /* keep for timings */
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
        }
#endif
        /* Create an output stream for this subtitle track and set basic
         * metadata such as language and flags (forced/hi). We set the
         * time_base to 1/90000 so PTS values align with our internal
         * 90kHz representation used throughout the code. */
        tracks[ntracks].stream = avformat_new_stream(out_fmt, NULL);
        tracks[ntracks].stream->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
        tracks[ntracks].stream->codecpar->codec_id = AV_CODEC_ID_DVB_SUBTITLE;
        tracks[ntracks].stream->time_base = (AVRational){1, 90000};
        av_dict_set(&tracks[ntracks].stream->metadata, "language", tok_lang, 0);
        if (forced)
            av_dict_set(&tracks[ntracks].stream->metadata, "forced", "1", 0);
        if (hi)
            av_dict_set(&tracks[ntracks].stream->metadata, "hearing_impaired", "1", 0);


        /* Allocate an encoder context for DVB subtitles. We initialize the
         * time_base and encode target dimensions here so rendered bitmaps
         * can be sized consistently with the encoder's expectations. */
        tracks[ntracks].codec_ctx = avcodec_alloc_context3(codec);
        if (!tracks[ntracks].codec_ctx)
        {
            fprintf(stderr, "Failed to alloc codec context for track %s\n", tok);
            return -1;
        }
        tracks[ntracks].codec_ctx->time_base = (AVRational){1, 90000};
        tracks[ntracks].codec_ctx->width = video_w;
        tracks[ntracks].codec_ctx->height = video_h;


        /* Configure encoder threading. When user hasn't specified a value
         * we default to the platform CPU count; otherwise we use the user
         * provided thread count. Threading can speed up encoder work for
         * large inputs but may increase memory usage. */
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

        /* Open the encoder instance. avcodec_open2() may probe codec-specific
         * capabilities and allocate internal buffers. Failure here typically
         * indicates a configuration mismatch or missing runtime support. */
        if (avcodec_open2(tracks[ntracks].codec_ctx, codec, NULL) < 0)
        {
            fprintf(stderr, "Failed to open DVB subtitle encoder for track %s\n", tok);
            return -1;
        }

        /*
         * If debugging is enabled (debug_level > 0), prints information about the opened DVB encoder for the current track.
         * The printed details include:
         *   - Track index (ntracks)
         *   - Track filename (tracks[ntracks].filename)
         *   - Track language (tracks[ntracks].lang)
         *   - Video width (tracks[ntracks].codec_ctx->width)
         *   - Video height (tracks[ntracks].codec_ctx->height)
         */
        if (debug_level > 0)
        {
            printf("Opened DVB encoder for track %d (%s, lang=%s): w=%d h=%d\n",
                   ntracks,
                   tracks[ntracks].filename,
                   tracks[ntracks].lang,
                   tracks[ntracks].codec_ctx->width,
                   tracks[ntracks].codec_ctx->height);
        }

        /*
         * Increment the number of subtitle tracks processed.
         * This counter is used to keep track of how many subtitle tracks have been handled so far.
         */
        ntracks++;

        /*
         * Parses the next tokens from the input string using ',' as the delimiter.
         * - `tok` receives the next token from the SRT string.
         * - `tok_lang` receives the next token from the language string.
         * Uses `strtok_r` for thread-safe tokenization, with `save_srt` and `save_lang` as context pointers.
         */
        tok = strtok_r(NULL, ",", &save_srt);
        tok_lang = strtok_r(NULL, ",", &save_lang);
    }

    /* Open the output file (unless the muxer uses I/O callbacks). This
     * step creates the output AVIO context used by avformat to write
     * packets. We fail early if the file cannot be created. */
    if (!(out_fmt->oformat->flags & AVFMT_NOFILE))
    {
        if (avio_open(&out_fmt->pb, output, AVIO_FLAG_WRITE) < 0)
        {
            fprintf(stderr, "Error: could not open output file %s\n", output);
            return -1;
        }
    }

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

    /* Write the container header. This emits stream headers for the output
     * format and must succeed before we attempt to write interleaved packets.
     */
    if (avformat_write_header(out_fmt, &mux_opts) < 0)
    {
        fprintf(stderr, "Error: could not write header for output file\n");
        return -1;
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

    /* Track first video PTS (90k) so we can emit a tiny blank subtitle aligned
     * with the video start. This ensures the subtitle stream start_time is
     * initialized and avoids the first real subtitle being dropped by the
     * muxer. */
    int seen_first_video = 0;
    int64_t first_video_pts90 = AV_NOPTS_VALUE;

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

    /*
     * Main demux and subtitle emission loop.
     *
     * This loop reads packets from the input format context (`in_fmt`) using libav's `av_read_frame`.
     * It handles asynchronous signal-based shutdowns, progress reporting, and subtitle emission logic.
     *
     * Key responsibilities:
     * - Handles interruptible shutdown via a signal flag (`stop_requested`).
     * - Tracks packet count and progress, printing periodic progress updates to stdout.
     * - Converts packet timestamps (PTS/DTS) to a unified 90kHz timeline for synchronization.
     * - For each subtitle track:
     *   - Determines if the next subtitle cue should be emitted based on current demux timestamp.
     *   - Renders subtitle bitmaps using either Pango or libass, with support for async rendering pools.
     *   - Remaps subtitle alignment for DVB compatibility if needed.
     *   - Emits subtitles via `encode_and_write_subtitle`, passing timing and rendering info.
     *   - Emits a minimal "clear" subtitle at cue end to ensure overlays are properly cleared.
     *   - Optionally saves debug PNGs of rendered bitmaps at high verbosity.
     * - Passes through regular A/V packets to the output format context (`out_fmt`), remapping stream indices.
     * - Frees all temporary resources and buffers after use.
     *
     * Progress reporting:
     * - Shows percentage, elapsed time, ETA, and subtitle count interactively.
     * - Uses last known valid timestamp to avoid UI jitter on streams with missing PTS.
     *
     * Debugging:
     * - Prints detailed diagnostic info at higher debug levels, including cue timing, alignment, and PNG saves.
     *
     * Resource management:
     * - Ensures all allocated buffers (bitmaps, subtitles) are freed after use.
     * - Handles both synchronous and asynchronous rendering paths for subtitle bitmaps.
     *
     * This loop is central to the subtitle muxing process, ensuring accurate timing, robust progress feedback,
     * and compatibility with DVB subtitle requirements.
     */
    while (av_read_frame(in_fmt, pkt) >= 0)
    {
        /* Async signal handling: interruptible shutdown.
         * We poll the sig_atomic_t flag set by the signal handler to break
         * out of the demux loop and start an orderly teardown. Avoid doing
         * any non-async-signal-safe operations in the handler itself.
         */
        if (stop_requested)
        {
            if (debug_level > 0)
                fprintf(stderr, "[main] stop requested (signal), breaking demux loop\n");
            av_packet_unref(pkt);
            break;
        }
        pkt_count++;

        /* Some containers provide DTS but not PTS. For our timing decisions
         * we prefer PTS; if PTS is missing but DTS is present use DTS as a
         * fallback so subtitle emission can proceed on streams without PTS. */
        if (pkt->pts == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE)
        {
            pkt->pts = pkt->dts;
        }

        /* Convert the packet's PTS into our 90kHz timeline. If the packet
         * lacks a PTS we fall back to the last seen valid timestamp so
         * subtitle emission logic remains forward-progressing on mixed
         * PTS/DTS streams. */
        int64_t cur90 = (pkt->pts == AV_NOPTS_VALUE) ? AV_NOPTS_VALUE : av_rescale_q(pkt->pts, in_fmt->streams[pkt->stream_index]->time_base, (AVRational){1, 90000});
        if (cur90 != AV_NOPTS_VALUE)
            last_valid_cur90 = cur90;
        /* Use last_valid_cur90 as a fallback when the current packet has no PTS
         * so subtitle emission logic can proceed on streams where some packets
         * don't carry explicit timestamps. */
        int64_t cmp90 = (cur90 != AV_NOPTS_VALUE) ? cur90 : last_valid_cur90;


        /* Periodic in-place progress printing for interactive runs. This
         * only runs when debug_level==0 so diagnostic runs remain clean.
         * The percentage is computed from the last known valid timestamp to
         * avoid jitter on containers with missing PTS. */
        if (debug_level == 0 && (pkt_count & pkt_progress_mask) == 0)
        {
            time_t now = time(NULL);

            /*
             * Updates and displays progress information for subtitle processing.
             *
             * This block executes if at least one second has passed since the last progress update.
             * It calculates the elapsed time since the program started, the percentage of progress,
             * and the estimated time remaining (ETA) based on the current packet timestamp and total duration.
             *
             * Progress is displayed in the terminal, showing either:
             *   - Percentage complete, number of subtitles emitted, elapsed time, and ETA (if total duration is known), or
             *   - Packet count, number of subtitles emitted, and elapsed time (if total duration is unknown).
             *
             * The output line is padded to clear any leftover characters from previous updates and uses
             * carriage return to overwrite the previous line in the terminal. The output buffer is flushed
             * to ensure immediate display.
             *
             * Variables used:
             *   - now: Current time.
             *   - last_progress_time: Timestamp of the last progress update.
             *   - prog_start_time: Program start time.
             *   - cur90, last_valid_cur90: Current and last valid packet timestamps.
             *   - total_duration_pts90: Total duration in PTS units.
             *   - input_start_pts90: Start timestamp in PTS units.
             *   - subs_emitted: Number of subtitles emitted.
             *   - pkt_count: Number of packets processed.
             *
             * Preconditions:
             *   - Progress update occurs only if at least one second has passed since the last update.
             *   - Percentage and ETA are calculated only if total duration and current timestamp are valid.
             *
             * Side effects:
             *   - Writes progress information to stdout.
             *   - Updates last_progress_time to the current time.
             */
            if (now - last_progress_time >= 1)
            {
                /*
                 * Calculate the elapsed time in seconds since the program started,
                 * and initialize variables for progress percentage and estimated time remaining.
                 */
                double elapsed = difftime(now, prog_start_time);
                double pct = 0.0;
                double eta = 0.0;

                /* Use last known valid timestamp for percentage so the UI
                 * doesn't blink when a packet lacking PTS is encountered. */
                int64_t cur_for_pct = (cur90 != AV_NOPTS_VALUE) ? cur90 : last_valid_cur90;

                /*
                 * Calculates the percentage of progress (`pct`) based on the current PTS value and the total duration.
                 * Ensures `pct` is clamped between 0.0 and 1.0.
                 * If progress is greater than 0.1%, estimates the total elapsed time and the remaining time (ETA).
                 *
                 * Preconditions:
                 * - `total_duration_pts90` must be valid and greater than 0.
                 * - `cur_for_pct` must be valid.
                 *
                 * Variables:
                 * - `pct`: Progress percentage (0.0 to 1.0).
                 * - `total_est`: Estimated total time based on current progress.
                 * - `eta`: Estimated time remaining.
                 */
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

                /*
                 * Calculates the elapsed time and estimated time of arrival (ETA) in minutes and seconds.
                 * - 'mins' and 'secs' represent the elapsed time split into minutes and seconds.
                 * - 'eta_m' and 'eta_s' represent the ETA split into minutes and seconds.
                 */
                int mins = (int)(elapsed / 60.0);
                int secs = (int)(elapsed) % 60;
                int eta_m = (int)(eta / 60.0);
                int eta_s = (int)(eta) % 60;
                
                /*
                 * Displays a progress line on stdout, updating in place.
                 * If total_duration_pts90 is valid, shows percentage complete, number of subtitles emitted,
                 * elapsed time, and estimated time remaining (ETA).
                 * Otherwise, shows packet count, number of subtitles emitted, and elapsed time.
                 * The progress line is padded with spaces to clear any leftover characters from previous updates.
                 * Uses carriage return ('\r') to overwrite the previous line in the terminal.
                 */
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
                
                /*
                 * Flushes the output buffer of stdout, ensuring that all buffered data is written to the console.
                 * This is useful for making sure that any pending output is displayed immediately.
                 */

                fflush(stdout);

                /*
                 * Updates the timestamp of the last progress event.
                 * 
                 * Sets 'last_progress_time' to the current time ('now'), typically used to track
                 * when the last progress update occurred in the process.
                 */
                last_progress_time = now;
            }
        }

        /*
         * Processes subtitle tracks and emits DVB subtitles for each cue.
         *
         * For each subtitle track:
         *   - Prints diagnostic information about subtitle emission conditions if debug_level > 1.
         *   - Iterates through cues whose start time is less than or equal to the current muxing position.
         *   - Renders subtitle bitmaps using either Pango or libass, depending on configuration.
         *     - Uses per-track codec target size if available to match encoder/mux coordinate system.
         *     - Remaps top alignment (7..9) to bottom alignment (1..3) for DVB rendering.
         *     - Supports asynchronous rendering with a prefetch window for improved throughput.
         *   - Optionally saves rendered bitmaps as PNG files for debugging.
         *   - Creates AVSubtitle packets for each cue and encodes/writes them using libav.
         *     - Sets display duration so libav clears subtitles automatically.
         *     - Tracks and reports progress, including estimated completion time.
         *   - Emits a minimal "clearing" subtitle at the end of each cue to ensure overlays are removed.
         *   - Frees all allocated resources after processing each cue.
         *
         * Debugging and benchmarking information is printed based on the debug_level and bench_mode settings.
         */
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

            /*
             * Processes and renders subtitle cues for each track, emitting DVB-compatible subtitles.
             *
             * This loop iterates over subtitle entries for a given track, rendering each cue as a bitmap
             * and encoding it for output. The rendering pipeline supports both SRT and ASS subtitle formats,
             * with optional asynchronous rendering for performance. The rendered bitmap is passed to the
             * DVB encoder helper, which consumes the index buffer and palette.
             *
             * Key steps:
             * - Determines rendering resolution based on video and codec context.
             * - Handles alignment remapping for DVB compatibility (top to bottom).
             * - Supports asynchronous rendering with prefetching for improved throughput.
             * - Renders subtitle cues using either Pango (SRT) or libass (ASS).
             * - Optionally saves debug PNGs of rendered bitmaps for verification.
             * - Encodes and writes the subtitle, updating progress and emitting a clearing subtitle at cue end.
             * - Frees all allocated resources after processing each cue.
             *
             * Progress and debug information are printed to stderr and stdout as appropriate.
             * The function ensures that subtitle overlays are cleared by emitting a minimal-duration
             * subtitle packet at the end of each cue.
             */
            while (tracks[t].cur_sub < tracks[t].count &&
                   (((tracks[t].entries[tracks[t].cur_sub].start_ms +
                      tracks[t].effective_delay_ms) *
                     90)) <= cmp90)
            {

                /* Render pipeline: produce an indexed `Bitmap` for the cue.
                 * The bitmap contains an index buffer and a palette that the
                 * DVB encoder helper consumes; the render functions allocate
                 * these buffers and the caller frees them via av_free(). */
                Bitmap bm = {0};
                
                if (!use_ass)
                {
                    /*
                     * Converts the current subtitle text from SRT format to Pango markup.
                     * This allows the subtitle text to be rendered with rich text formatting
                     * supported by Pango, such as bold, italics, and color.
                     *
                     * Parameters:
                     *   tracks[t].entries[tracks[t].cur_sub].text - The subtitle text in SRT format
                     *
                     * Returns:
                     *   A pointer to a newly allocated string containing the Pango markup.
                     *   The caller is responsible for freeing this memory.
                     */
                    char *markup = srt_to_pango_markup(tracks[t].entries[tracks[t].cur_sub].text);

                    /*
                     * Records the current timestamp in microseconds for benchmarking purposes.
                     * The value of t1 can be used to measure elapsed time between events.
                     */
                    int64_t t1 = bench_now();

                    /*
                     * Determines the rendering width and height for video output.
                     * If 'video_w' or 'video_h' are greater than zero, their values are used.
                     * Otherwise, defaults to 1920 for width and 1080 for height.
                     */
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

                    /*
                     * Retrieves the alignment value for the current subtitle entry of the specified track.
                     * 'cue_align' will hold the alignment information, which determines how the subtitle
                     * text is positioned on the screen (e.g., left, center, right).
                     * - 'tracks[t]' refers to the current track being processed.
                     * - 'cur_sub' is the index of the currently active subtitle entry within the track.
                     * - 'alignment' is a property of the subtitle entry specifying its alignment.
                     */
                    int cue_align = tracks[t].entries[tracks[t].cur_sub].alignment;
                    
                    /*
                     * Stores the alignment value to be used for the current cue.
                     * The value is taken from cue_align, which determines how the subtitle text
                     * should be aligned (e.g., left, center, right) when rendered.
                     */
                    int used_align = cue_align;

                    /*
                     * Remaps cue alignment values for DVB rendering when ASS subtitles are not used.
                     *
                     * If `cue_align` is between 7 and 9 (inclusive), it is remapped to values 1 to 3
                     * by subtracting 6. This is necessary because DVB rendering expects alignment values
                     * in the range 1 to 3, whereas the input may use 7 to 9 for certain alignments.
                     *
                     * If debugging is enabled (`debug_level > 0`), the remapping is logged to stderr.
                     */
                    if (!use_ass && cue_align >= 7 && cue_align <= 9)
                    {
                        used_align = cue_align - 6; /* 7->1,8->2,9->3 */
                        if (debug_level > 0)
                            fprintf(stderr,
                                    "[main-debug] remapping cue align %d -> %d for DVB render\n",
                                    cue_align, used_align);
                    }

                    /*
                     * If debug_level is greater than 0, prints detailed information about the current subtitle cue rendering process to stderr.
                     * The debug output includes:
                     *   - The index of the current subtitle cue (tracks[t].cur_sub)
                     *   - The dimensions of the rendered subtitle (render_w, render_h)
                     *   - The codec context dimensions (width and height), or -1 if codec_ctx is NULL
                     *   - The video dimensions (video_w, video_h)
                     *   - The alignment parameters (cue_align, used_align)
                     */
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

                    /*
                     * Checks if the video width or height is invalid (less than or equal to zero).
                     * If so, and if debugging is enabled (debug_level > 0), prints a warning message
                     * to stderr indicating that the video size is unknown and fallback dimensions
                     * (render_w x render_h) will be used for rendering.
                     */
                    if ((video_w <= 0 || video_h <= 0) && debug_level > 0)
                    {
                        fprintf(stderr, "[main] Warning: video size unknown, using fallback %dx%d for rendering\n", render_w, render_h);
                    }

                    /*
                     * Handles subtitle bitmap rendering with support for asynchronous and synchronous modes.
                     *
                     * If asynchronous rendering threads are enabled (render_threads > 0), attempts to fetch a pre-rendered
                     * bitmap for the current subtitle cue. If the bitmap is available, it is used directly. If the job exists
                     * but is not finished, falls back to synchronous rendering to avoid indefinite waiting. If no job exists
                     * for the current cue, submits a prefetch window of upcoming cues to the async render pool to improve
                     * throughput, then tries to fetch the bitmap again. If still unavailable, performs synchronous rendering
                     * to ensure timely subtitle emission.
                     *
                     * This logic ensures optimal performance by leveraging async rendering when possible, while guaranteeing
                     * that subtitles are rendered and emitted on time via synchronous fallback.
                     */
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
                            /* job exists but not finished yet; perform a blocking render
                             * to avoid waiting forever — this turn-keys a synchronous
                             * render using the same rendering path so output is
                             * identical to the async case. */
                            bm = render_pool_render_sync(markup,
                                                         render_w, render_h,
                                                         cli_fontsize, cli_font,
                                                         cli_fgcolor, cli_outlinecolor, cli_shadowcolor,
                                                         used_align,
                                                         palette_mode);
                        }
                        else
                        {
                            /*
                             * Prefetches and submits subtitle rendering jobs asynchronously for a window of upcoming subtitle entries.
                             *
                             * This loop iterates over a fixed-size prefetch window (`PREFETCH_WINDOW`) starting from the current subtitle index.
                             * For each entry within the window:
                             *   - Converts the subtitle text to Pango markup using `srt_to_pango_markup`.
                             *   - Submits an asynchronous rendering job to the render pool via `render_pool_submit_async`, passing relevant rendering parameters.
                             *   - Frees the allocated markup string after submission.
                             * The loop terminates early if the end of the subtitle entries is reached.
                             *
                             * Parameters used in rendering:
                             *   - `render_w`, `render_h`: Dimensions for rendering.
                             *   - `cli_fontsize`, `cli_font`: Font size and font family.
                             *   - `cli_fgcolor`, `cli_outlinecolor`, `cli_shadowcolor`: Colors for foreground, outline, and shadow.
                             *   - `used_align`: Text alignment.
                             *   - `palette_mode`: Color palette mode.
                             */

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
                            

                            /*
                             * Attempts to retrieve a rendered subtitle bitmap from the render pool for the current track.
                             * If a cached bitmap is available (render_pool_try_get returns 1), it is used.
                             * Otherwise, a new bitmap is rendered synchronously using the provided markup, dimensions,
                             * font settings, colors, alignment, and palette mode.
                             */
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

                    /*
                     * If benchmarking mode is enabled, update the rendering time and count.
                     *
                     * - Adds the elapsed time (in microseconds) since `t1` to `bench.t_render_us`.
                     * - Increments the number of cues rendered (`bench.cues_rendered`).
                     */
                    if (bench_mode)
                    {
                        bench.t_render_us += bench_now() - t1;
                        bench.cues_rendered++;
                    }

                    /*
                     * Frees the memory allocated for the 'markup' pointer.
                     * This helps prevent memory leaks by releasing resources
                     * that are no longer needed.
                     */
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

                /* Pass raw times into make_subtitle; delay applied via packet PTS */
                int track_delay_ms = tracks[t].effective_delay_ms;

                /* Save debug PNG of the rendered bitmap only at high debug
                 * verbosity (debug_level > 1). When not saving, pass NULL to
                 * the encoder helper so it won't report PNG-origin logs. */
                char pngfn[PATH_MAX] = "";

                /*
                 * If the debug level is greater than 1, this block performs the following actions:
                 * - Generates a PNG filename using the current subtitle sequence, track index, and cue index.
                 * - Saves the current bitmap as a PNG file with the generated filename.
                 * - Logs the PNG save operation, including filename and bitmap coordinates/dimensions.
                 * - If the current subtitle index is valid and the subtitle text exists, logs the cue index and normalized subtitle text.
                 * This helps verify the mapping between subtitle cues and their corresponding PNG images for debugging purposes.
                 */
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

                /*
                 * If debugging is enabled (debug_level > 0), logs information about the current subtitle cue being rendered.
                 * The log includes:
                 *   - The track index (t)
                 *   - The current cue index (tracks[t].cur_sub)
                 *   - The start time of the cue in milliseconds (start_ms)
                 *   - The effective delay applied to the track in milliseconds (effective_delay_ms)
                 * Output is sent to stderr for debugging purposes.
                 */
                if (debug_level > 0) {
                    int64_t dbg_start_ms = tracks[t].entries[tracks[t].cur_sub].start_ms;
                    fprintf(stderr, "[dbg] rendered track=%d cue=%d start_ms=%d (delay=%d)\n", t, tracks[t].cur_sub, (int)dbg_start_ms, tracks[t].effective_delay_ms);
                }

                /*
                 * Creates an AVSubtitle object using the provided bitmap and subtitle timing information.
                 * - bm: Bitmap data representing the subtitle image.
                 * - tracks[t].entries[tracks[t].cur_sub].start_ms: Start time of the subtitle in milliseconds.
                 * - tracks[t].entries[tracks[t].cur_sub].end_ms: End time of the subtitle in milliseconds.
                 * Returns a pointer to the newly created AVSubtitle structure.
                 */
                AVSubtitle *sub = make_subtitle(bm,
                                                tracks[t].entries[tracks[t].cur_sub].start_ms,
                                                tracks[t].entries[tracks[t].cur_sub].end_ms);
                /*
                 * Processes and encodes a subtitle entry for a given track.
                 *
                 * - Sets the display duration for the subtitle so libav can clear it automatically.
                 * - Calculates the presentation timestamp (PTS) in 90kHz units, considering track delay.
                 * - Optionally prints debug information about the subtitle being encoded.
                 * - Calls `encode_and_write_subtitle()` to encode and write the subtitle data.
                 * - Increments the count of emitted subtitles for progress reporting.
                 * - If debug level is high, prints details about the saved bitmap and subtitle cue.
                 * - If debug level is quiet/default, updates progress in-place on the console, including
                 *   percentage complete and estimated time remaining if possible.
                 * - Frees allocated subtitle and bitmap resources after processing.
                 *
                 * Variables used:
                 *   - sub: Pointer to the subtitle structure.
                 *   - tracks: Array of track structures containing subtitle entries.
                 *   - t: Current track index.
                 *   - debug_level: Controls verbosity of debug and progress output.
                 *   - input_start_pts90: Start PTS in 90kHz units.
                 *   - track_delay_ms: Delay to apply to the track in milliseconds.
                 *   - out_fmt: Output format context.
                 *   - bench_mode: Benchmark mode flag.
                 *   - pngfn: Filename for PNG bitmap output (if debug).
                 *   - subs_emitted: Counter for emitted subtitles.
                 *   - last_progress_time: Timestamp of last progress update.
                 *   - prog_start_time: Timestamp when processing started.
                 *   - total_duration_pts90: Total duration in 90kHz units (for percentage calculation).
                 *   - last_valid_cur90: Last valid current PTS in 90kHz units.
                 *   - bm: Bitmap buffers used during subtitle rendering.
                 */
                if (sub)
                {
                    /* Use duration so libav clears automatically */
                    sub->start_display_time = 0;
                    sub->end_display_time =
                        (tracks[t].entries[tracks[t].cur_sub].end_ms -
                         tracks[t].entries[tracks[t].cur_sub].start_ms);

                    /*
                     * Calculates the presentation timestamp (PTS) in 90kHz clock units for the current subtitle entry.
                     * The PTS is determined by adding the start time (in milliseconds) of the current subtitle entry
                     * and any track-specific delay, then converting the result to 90kHz units and adding it to the
                     * initial input start PTS value.
                     *
                     * Variables:
                     * - input_start_pts90: The initial PTS value in 90kHz units.
                     * - tracks[t].entries[tracks[t].cur_sub].start_ms: Start time of the current subtitle entry in milliseconds.
                     * - track_delay_ms: Additional delay to apply to the track, in milliseconds.
                     */
                    int64_t pts90 = input_start_pts90 + ((tracks[t].entries[tracks[t].cur_sub].start_ms +
                                                          track_delay_ms) *
                                                         90);
                    /*
                     * If debugging is enabled (debug_level > 0), this block prints detailed information
                     * about the current subtitle encoding process to stderr. The output includes:
                     * - The track index (t)
                     * - The current subtitle cue index for the track (tracks[t].cur_sub)
                     * - The presentation timestamp in 90kHz clock units (pts90)
                     * - The presentation timestamp converted to milliseconds (pts90/90)
                     */
                    if (debug_level > 0) {
                        fprintf(stderr, "[dbg] encoding track=%d cue=%d pts90=%lld (ms=%lld)\n", t, tracks[t].cur_sub, (long long)pts90, (long long)(pts90/90));
                    }

                    /*
                     * Encodes and writes a subtitle using the specified codec context and output format.
                     *
                     * Parameters:
                     *   tracks[t].codec_ctx - The codec context for the current track.
                     *   out_fmt             - The output format for the subtitle.
                     *   &tracks[t]          - Pointer to the track structure containing subtitle information.
                     *   sub                 - The subtitle data to encode and write.
                     *   pts90               - Presentation timestamp in 90kHz units.
                     *   bench_mode          - Flag indicating whether benchmarking mode is enabled.
                     *   (debug_level > 1 ? pngfn : NULL) - Optional PNG filename for debugging output if debug level is greater than 1.
                     */
                    encode_and_write_subtitle(tracks[t].codec_ctx,
                                              out_fmt,
                                              &tracks[t],
                                              sub,
                                              pts90,
                                              bench_mode,
                                              (debug_level > 1 ? pngfn : NULL));

                    /* Count emitted subtitles for progress reporting */
                    subs_emitted++;

                    /*
                     * If the debug level is greater than 1, outputs diagnostic information to stderr and stdout.
                     * - Logs the filename of the saved SRT bitmap as a PNG.
                     * - Prints subtitle cue information including:
                     *   - Cue number
                     *   - Subtitle track filename
                     *   - Presentation timestamp (PTS) in milliseconds
                     *   - Subtitle display duration in milliseconds
                     *   - Track delay in milliseconds
                     */
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

                    /*
                     * Displays progress information to stdout if debug_level is 0 and at least one second has passed since the last update.
                     * Shows elapsed time, number of subtitles emitted, and, if available, percentage completed and estimated time remaining (ETA).
                     * Progress information is padded to clear leftover characters from previous output.
                     *
                     * Variables:
                     *   debug_level           - Controls verbosity; progress is shown only if set to 0.
                     *   now                   - Current time.
                     *   last_progress_time    - Time when progress was last displayed.
                     *   prog_start_time       - Time when processing started.
                     *   total_duration_pts90  - Total duration in PTS units (if available).
                     *   last_valid_cur90      - Last valid current PTS value.
                     *   input_start_pts90     - Start PTS value of input.
                     *   subs_emitted          - Number of subtitles emitted so far.
                     *
                     * Output:
                     *   Progress line printed to stdout, updated in-place using carriage return.
                     */
                    if (debug_level == 0)
                    {
                        /*
                         * Gets the current calendar time as a value of type time_t.
                         * The value represents the number of seconds elapsed since the Unix epoch (00:00:00 UTC, January 1, 1970).
                         * Returns -1 if the current time is not available.
                         */
                        time_t now = time(NULL);

                        /*
                         * Displays progress information to the user if at least one second has passed since the last update.
                         * Calculates elapsed time in minutes and seconds. If total duration and current position are known,
                         * computes the percentage completed and estimates the remaining time (ETA). Formats a progress line
                         * including percentage, number of subtitles emitted, elapsed time, and ETA. If duration is unknown,
                         * displays only subtitles emitted and elapsed time. Pads the output line to clear leftover characters,
                         * prints it to stdout, and flushes the output. Updates the last progress time.
                         *
                         * Variables used:
                         *   - now: current time
                         *   - last_progress_time: last time progress was displayed
                         *   - prog_start_time: time when processing started
                         *   - total_duration_pts90: total duration in PTS units (if available)
                         *   - last_valid_cur90: last valid current PTS position
                         *   - input_start_pts90: input start PTS position
                         *   - subs_emitted: number of subtitles emitted so far
                         */
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

                    /*
                     * Frees the memory allocated for an AVSubtitle structure.
                     * First, releases any internal resources held by the subtitle using avsubtitle_free(sub).
                     * Then, deallocates the memory for the subtitle structure itself using av_free(sub).
                     * This ensures proper cleanup and prevents memory leaks.
                     */
                    avsubtitle_free(sub);
                    av_free(sub);


                    /*
                     * Frees the memory allocated for bm.idxbuf if it is not NULL.
                     * Uses av_free to release the buffer, preventing memory leaks.
                     */
                    if (bm.idxbuf)
                        av_free(bm.idxbuf);

                    /*
                     * Frees the memory allocated for the bitmap palette if it exists.
                     * Checks if 'bm.palette' is not NULL before calling 'av_free' to release the memory.
                     */
                    if (bm.palette)
                        av_free(bm.palette);
                }

                /*
                 * Allocates memory for an AVSubtitle structure and initializes it to zero.
                 * 
                 * This ensures that all fields of the subtitle structure are set to their default values,
                 * preventing undefined behavior due to uninitialized memory.
                 *
                 * Returns pointer to the newly allocated and zero-initialized AVSubtitle structure.
                 */
                AVSubtitle *clr = av_mallocz(sizeof(*clr));

                /*
                 * Clears a subtitle cue by resetting its format and display times, then encodes and writes
                 * the cleared subtitle using the provided codec context and output format. The function also
                 * logs the clear operation if debugging is enabled and frees the allocated subtitle memory.
                 *
                 * Steps performed:
                 * - Resets subtitle format and display times to minimal values.
                 * - Calculates the presentation timestamp (PTS) for the clear cue.
                 * - Encodes and writes the cleared subtitle.
                 * - Logs the clear operation if debug_level > 0.
                 * - Frees the subtitle structure memory.
                 *
                 * Parameters used:
                 * - clr: Pointer to the subtitle structure to be cleared.
                 * - tracks[t]: Current track context.
                 * - input_start_pts90: Start PTS in 90kHz units.
                 * - track_delay_ms: Delay to apply to the track in milliseconds.
                 * - out_fmt: Output format context.
                 * - bench_mode: Benchmark mode flag.
                 * - debug_level: Debug verbosity level.
                 */
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

        /* Remap packet stream indices from input to output and write A/V
        * packets. Subtitle packets are emitted by encode_and_write_subtitle
        * above; regular A/V packets are passed through to the output. */
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

    /* Finalize the output stream(s) and write trailer metadata.
     * av_write_trailer() flushes delayed packets and writes any format
     * specific trailer information (indexes, timestamps) required by the
     * container. After this call the output file is logically complete and
     * no further packets should be written.
     */
    av_write_trailer(out_fmt);

    /*
     * Shutdown render workers and free codec contexts.
     *
     * Ordering requirements and rationale:
     *  - Stop the render worker pool before freeing any renderer-owned
     *    resources (Freetype, Pango fontmap, Cairo caches) because worker
     *    threads may still reference those objects. Calling render_pool_shutdown
     *    prevents race conditions where a thread touches freed memory.
     *  - After worker threads are stopped call render_pango_cleanup() to
     *    release Pango/Cairo-owned objects while still linked to their
     *    libraries. This reduces leaks reported by ASan and is safe because
     *    no render threads are active.
     *  - Finally free codec contexts (avcodec_free_context) which may hold
     *    references to codec-private buffers. Freeing codec contexts in this
     *    controlled order lowers the chance of re-entrancy or mysterious
     *    use-after-free when libraries call into callbacks during teardown.
     *
     * Note: render_pool_shutdown() is idempotent; calling it multiple times
     * is safe and simplifies per-track teardown loops (the implementation
     * will gracefully handle a no-op on subsequent calls).
     */
    for (int t = 0; t < ntracks; t++)
    {
        /* Ensure render workers are stopped before releasing Pango/fontmap.
         * If render_threads was zero this will be a cheap no-op. */
        render_pool_shutdown();

        /* Ensure Pango/fontmap resources are released while we still have
         * the process-local symbol visibility for GObject/Pango. This
         * avoids leaking objects that hold references into fontconfig. */
        render_pango_cleanup();

        /* Free the codec context for this track. avcodec_free_context()
         * will also free internal buffers and is safe to call even when the
         * encoder has buffered packets (we wrote the trailer already). */
        if (tracks[t].codec_ctx)
            avcodec_free_context(&tracks[t].codec_ctx);
    }

    /* Free per-track heap resources allocated during initialization and
     * parsing. Ownership rules:
     *  - tracks[].lang and tracks[].filename are strdup()'d copies that
     *    belong to main() and must be free()-ed here.
     *  - tracks[].entries is an array returned from parse_srt(). Each
     *    entries[i].text is separately allocated and must be freed before
     *    freeing the entries array itself.
     *  - When libass is used, tracks[].ass_track references an opaque
     *    libass object which must be released via the render_ass helper.
     */
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
            /* Release libass per-track state. The render_ass helpers wrap
             * libass APIs and centralize error handling; prefer those
             * wrapper calls rather than calling libass directly here. */
            render_ass_free_track(tracks[t].ass_track);
            tracks[t].ass_track = NULL;
        }
#endif
    }

    /*
     * Frees resources allocated during program execution.
     *
     * - Releases ASS renderer and library if initialized.
     * - Frees CLI-allocated strings and lists, ensuring only dynamically allocated memory is released.
     * - Checks for default values before freeing certain strings to avoid freeing static memory.
     * - Cleans up subtitle delay lists and associated values.
     *
     * This block helps prevent memory leaks and ensures proper cleanup before program exit.
     */
#ifdef HAVE_LIBASS  
    if (ass_renderer)
        render_ass_free_renderer(ass_renderer);
    if (ass_lib)
        render_ass_free_lib(ass_lib);
#endif
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
    if (subtitle_delay_list)
        free(subtitle_delay_list);
    if (delay_vals)
        free(delay_vals);


    /*
     * Closes the output format's AVIO context if it exists.
     *
     * This checks if the output format (`out_fmt`) and its associated AVIO context (`pb`) are not NULL.
     * If both are valid, it closes the AVIO context using `avio_closep`, releasing any resources associated with it.
     */
    if (out_fmt && out_fmt->pb)
        avio_closep(&out_fmt->pb);

    avformat_free_context(out_fmt);
    avformat_close_input(&in_fmt);
    avformat_network_deinit();

    if (qc)
        fclose(qc);
    av_packet_free(&pkt);

    /* Additional runtime cleanup: try to unref Pango default fontmap and
     * reset any Cairo static caches. This should run before FcFini so that
     * Pango/GObject-owned references are released prior to fontconfig
     * finalization. These steps reduce small leaks reported by ASan during
     * shutdown and are best-effort (they use dlopen/dlsym when necessary).
     *
     * Implementation notes:
     *  - We attempt to dlopen() the relevant libraries and call unref via
     *    dlsym() to avoid hard-linking to specific GLib/GObject symbols.
     *  - We do NOT call cairo_debug_reset_static_data() since on some
     *    Cairo versions that can trigger internal assertions when static
     *    objects still exist. The current sequence (unref Pango, then FcFini)
     *    has the best practical results in reducing small ASan-reported
     *    allocations without risking crashes.
     */
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
    }

    /* Try to call FcFini() to allow fontconfig to release small internal
     * allocations (observed as tiny strdup() leaks under ASan). If fontconfig
     * was found at configure time we link it and call FcFini() directly; fall
     * back to the dlopen/dlsym approach otherwise.
     *
     * Important: FcFini must be invoked after Pango/GObject references are
     * released, otherwise fontconfig finalization may leave dangling
     * references in Pango. The order chosen here (unref Pango, then FcFini)
     * works well across different library versions.
     */
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

    /*
     * If bench_mode is enabled, calls bench_report() to output benchmarking results.
     */
    if (bench_mode) {
        bench_report();
    }

    /* Ensure we end with a newline so the CLI prompt appears on the next line */
    fprintf(stdout, "\n");
    
    fflush(stdout);

    return 0;
}

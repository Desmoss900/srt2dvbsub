/*
* Copyright (c) 2025 Mark E. Rosche, Chili IPTV Systems
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
*   Mark E. Rosche, Chili IPTV Systems
*   Email: license@chili-iptv.de
*   Website: www.chili-iptv.de
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <wchar.h>
#include <wctype.h>
#include <locale.h>
#include <unistd.h>
#include <dlfcn.h>
#include <signal.h>
#include <limits.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "dvb_lang.h"
#include "utils.h"
#include "runtime_opts.h"
#include "version.h"

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

void print_help(void)
{
    printf("Usage: srt2dvbsub --input in.ts --output out.ts --srt subs.srt[,subs2.srt] --languages eng[,deu] [options]\n\n");
    printf("Options:\n");
    printf("  -I, --input FILE            Input TS file\n");
    printf("  -o, --output FILE           Output TS file\n");
    printf("  -s, --srt FILES             Comma-separated SRT files\n");
    printf("  -l, --languages CODES       Comma-separated 3-letter DVB language codes\n");
#ifdef HAVE_LIBASS
    printf("      --ass                   Enable libass rendering\n");
#endif
    printf("      --forced FLAGS          Comma-separated forced flags per track (e.g., \"0,1,0\")\n");
    printf("      --hi FLAGS              Comma-separated hearing-impaired flags per track (e.g., \"0,0,1\")\n");
#ifdef HAVE_FONTCONFIG
    printf("      --list-fonts            List available font families/styles and exit\n");
#else
    printf("      --list-fonts            (unavailable: rebuild with Fontconfig support)\n");
#endif
    printf("      --qc-only               Run srt file quality checks only (no mux)\n");
    printf("      --palette MODE          Palette mode (ebu-broadcast|broadcast|greyscale)\n");
    printf("      --font FONTNAME         Set font family (default is DejaVu Sans)\n");
    printf("      --font-style STYLE      Optional font style variant (e.g. Bold, Italic, Light)\n");
    printf("      --font-size N           Set font size in px (overrides dynamic sizing)\n");
    printf("      --fg-color #RRGGBB      Text color (in quotes i.e. \"#00ff00\")\n");
    printf("      --outline-color #RRGGBB Outline color (in quotes i.e. \"#808080\")\n");
    printf("      --shadow-color #AARRGGBB Shadow color (alpha optional...in quotes i.e. \"#00808080\")\n");
    printf("      --bg-color #RRGGBB      Background color (in quotes i.e. \"#000000\")\n");
    printf("      --sub-position POS      Position on canvas: top-left, top-center, top-right, center-left,\n");
    printf("                              center, center-right, bottom-left, bottom-center (default), bottom-right\n");
    printf("      --margin-top PERCENT    Top margin as %% of canvas height (0.0-50.0%%, default: 3.5%%)\n");
    printf("      --margin-left PERCENT   Left margin as %% of canvas width (0.0-50.0%%, default: 2.0%%)\n");
    printf("      --margin-bottom PERCENT Bottom margin as %% of canvas height (0.0-50.0%%, default: 3.5%%)\n");
    printf("      --margin-right PERCENT  Right margin as %% of canvas width (0.0-50.0%%, default: 2.0%%)\n");
    printf("      --ssaa N                Force supersample factor (1..24) (default 4)\n");
    printf("      --no-unsharp            Disable the final unsharp pass to speed rendering\n");
    printf("      --png-dir DIR           Custom directory for debug PNG output (default: pngs/)\n");
    printf("      --png-only              Output PNG files only (no MPEG-TS generation)\n");        
    printf("      --pid PID[,PID2,...]    Custom PIDs for subtitle tracks (single value=auto-increment)\n");
    printf("      --ts-bitrate BPSI       Override MPEG-TS bitrate (muxrate) in bits per second\n");
    printf("      --delay MS[,MS2,...]    Global or per-track subtitle delay in milliseconds (comma-separated list)\n");
    printf("      --enc-threads N         Encoder thread count (0=auto)\n");
    printf("      --render-threads N      Parallel render workers (0=single-thread)\n");
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
        int fallback = 0;
        int allocated_entries = 0;
        char **entries = calloc((size_t)lang_count, sizeof(char*));
        size_t *elens = calloc((size_t)lang_count, sizeof(size_t));
        size_t *col_widths = NULL;
        size_t maxlen = 0; /* measured in display columns */
        if (!entries || !elens) {
            fallback = 1;
            goto after_table;
        }
        for (int i = 0; i < lang_count; i++) {
            const struct dvb_lang_entry *p = &dvb_langs[i];
            /* Format: "code  English / Native" */
            size_t need = snprintf(NULL, 0, "%s  %s / %s", p->code, p->ename, p->native) + 1;
            entries[i] = malloc(need);
            if (!entries[i]) {
                fallback = 1;
                goto after_table;
            }
            allocated_entries = i + 1;
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
        while (cols >= 1) {
            rows = (lang_count + cols - 1) / cols;
            /* compute per-column width */
            size_t *new_cols = calloc((size_t)cols, sizeof(size_t));
            if (!new_cols) {
                fallback = 1;
                goto after_table;
            }
            free(col_widths);
            col_widths = new_cols;
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

after_table:
        if (fallback) {
            if (entries) {
                for (int i = 0; i < allocated_entries; i++) free(entries[i]);
            }
            free(entries);
            free(elens);
            free(col_widths);
            for (int i = 0; i < lang_count; i++) {
                const struct dvb_lang_entry *p = &dvb_langs[i];
                printf("  %s  %s / %s\n", p->code, p->ename, p->native);
            }
        } else {
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
            free(entries);
            free(elens);
            free(col_widths);
        }
    }
    printf("\n");
}

/*
 * Prints the build version information to standard output.
 *
 * This function outputs the build version, commit hash, build date,
 * CPU architecture, and operating system using predefined macros and compiler detection.
 */
void print_version(void)
{
    /* Detect operating system using compiler-defined macros */
    const char *os = "unknown";
    
    #if defined(__linux__)
        os = "linux";
    #elif defined(__APPLE__) && defined(__MACH__)
        os = "darwin";
    #elif defined(__FreeBSD__)
        os = "FreeBSD";
    #elif defined(__OpenBSD__)
        os = "OpenBSD";
    #elif defined(__NetBSD__)
        os = "NetBSD";
    #endif
    
    /* Detect CPU architecture using compiler-defined macros */
    const char *arch = "unknown";
    
    #if defined(__x86_64__) || defined(__amd64__) || defined(_M_X64)
        arch = "x86_64";
    #elif defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__) || defined(_M_IX86)
        arch = "i386";
    #elif defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
        arch = "aarch64";
    #elif defined(__arm__) || defined(__thumb__) || defined(_M_ARM)
        arch = "arm";
    #elif defined(__ppc64__) || defined(__PPC64__) || defined(_M_PPC64)
        arch = "ppc64";
    #elif defined(__ppc__) || defined(__PPC__) || defined(_M_PPC)
        arch = "ppc";
    #elif defined(__mips__) || defined(__mips64__)
        arch = "mips";
    #elif defined(__riscv)
        arch = "riscv";
    #endif
    
    printf("\nsrt2dvbsub Version: %s (%s, %s) [%s-%s]\n\n", GIT_VERSION, GIT_COMMIT, GIT_DATE, os, arch);
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
void print_license(void)
{
    printf("\n");
    printf("Copyright (c) 2025 Mark E. Rosche, Chili IPTV Systems\n");
    printf("All rights reserved.\n");
    printf("\n");
    printf("PERSONAL USE LICENSE - NON-COMMERCIAL ONLY\n");
    printf("────────────────────────────────────────────────────────────────\n");
    printf("This software is provided for personal, educational, and non-commercial\n");
    printf("use only. You are granted permission to use, copy, and modify this\n");
    printf("software for your own personal or educational purposes, provided that\n");
    printf("this copyright and license notice appears in all copies or substantial\n");
    printf("portions of the software.\n");
    printf("\n");
    printf("PERMITTED USES:\n");
    printf("  ✓ Personal projects and experimentation\n");
    printf("  ✓ Educational purposes and learning\n");
    printf("  ✓ Non-commercial testing and evaluation\n");
    printf("  ✓ Individual hobbyist use\n");
    printf("\n");
    printf("PROHIBITED USES:\n");
    printf("  ✗ Commercial use of any kind\n");
    printf("  ✗ Incorporation into products or services sold for profit\n");
    printf("  ✗ Use within organizations or enterprises for revenue-generating activities\n");
    printf("  ✗ Modification, redistribution, or hosting as part of any commercial offering\n");
    printf("  ✗ Licensing, selling, or renting this software to others\n");
    printf("  ✗ Using this software as a foundation for commercial services\n");
    printf("\n");
    printf("No commercial license is available. For inquiries regarding any use not\n");
    printf("explicitly permitted above, contact:\n");
    printf("  Mark E. Rosche, Chili IPTV Systems\n");
    printf("  Email: license@chili-iptv.de\n");
    printf("  Website: www.chili-iptv.de\n");
    printf("\n");
    printf("────────────────────────────────────────────────────────────────\n");
    printf("DISCLAIMER\n");
    printf("────────────────────────────────────────────────────────────────\n");
    printf("THIS SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND,\n");
    printf("EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES\n");
    printf("OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.\n");
    printf("IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,\n");
    printf("DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,\n");
    printf("ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER\n");
    printf("DEALINGS IN THE SOFTWARE.\n");
    printf("\n");
    printf("────────────────────────────────────────────────────────────────\n");
    printf("By using this software, you agree to these terms and conditions.\n");
    printf("────────────────────────────────────────────────────────────────\n");
    printf("\n");
}

/**
 * Prints the usage instructions for the srt2dvbsub command-line tool.
 * Displays the required and optional arguments, as well as a hint to use the help option for more details.
 */
void print_usage(void)
{
    printf("Usage: srt2dvbsub --input in.ts --output out.ts --srt subs.srt[,subs2.srt] --languages eng[,deu] [options]\n");
    printf("Try 'srt2dvbsub --help' for more information.\n");
}

/* Some platforms may not expose wcwidth prototype without extra feature macros.
 * Provide a fallback declaration to avoid implicit declaration warnings. */
extern int wcwidth(wchar_t wc);

/* Return the display width (columns) of a UTF-8 string using wcwidth().
 * Falls back to strlen() for bytes when multibyte handling fails. */
int utf8_display_width(const char *s)
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
 * replace_strdup
 *
 * Safely replace a destination pointer with a strdup'd copy of src.
 * On success the previous *dest is freed (if non-NULL) and *dest is
 * updated to point at the newly allocated string. If src is NULL the
 * destination is set to NULL (previous value freed).
 *
 * Returns 0 on success, -1 on allocation failure.
 */
int replace_strdup(const char **dest, const char *src)
{
    char *tmp = NULL;
    if (src) {
        tmp = strdup(src);
        if (!tmp)
            return -1;
    }
    /* Do not free previous *dest here. Caller is responsible for freeing
     * prior values when appropriate (e.g. when replacing previously heap
     * allocated strings). This avoids attempting to free static string
     * literals that may be used as defaults. */
    *dest = tmp;
    return 0;
}

/*
 * replace_strdup_owned
 *
 * Like replace_strdup but assumes the existing *dest, if non-NULL, was
 * allocated with strdup (or malloc) and therefore can be freed safely.
 * This is useful when the caller exclusively owns the previous string
 * and wants to avoid leaking it when replacing the value.
 */
int replace_strdup_owned(const char **dest, const char *src)
{
    if (!dest) return -1;
    if (*dest) {
        free((void *)*dest);
        *dest = NULL;
    }
    if (!src) return 0;
    char *dup = strdup(src);
    if (!dup) return -1;
    *dest = dup;
    return 0;
}

/*
 * Simple signal handler used to request an orderly shutdown. The handler
 * must be async-signal-safe and therefore only sets a sig_atomic_t flag.
 */
void handle_signal(int sig, volatile sig_atomic_t *stop_requested)
{
    (void)sig;
    *stop_requested = 1;
}

int validate_path_length(const char *path, const char *label)
{
    if (!path)
        return 0;
    size_t len = strlen(path);
    if (len == 0)
        return 0;
    if (len >= PATH_MAX)
    {
        LOG(1, "Error: %s exceeds maximum supported length (%d characters)\n",
            label ? label : "path", PATH_MAX - 1);
        return -1;
    }
    return 0;
}

/*
 * Removes leading and trailing whitespace from the input string in place.
 * The function modifies the original string by advancing the pointer past
 * leading whitespace and replacing trailing whitespace with null terminators.
 *
 * Parameters:
 *   str - Pointer to the string to be trimmed. May be modified.
 *
 * Returns:
 *   Pointer to the trimmed string. If the input is NULL, returns NULL.
 *
 * Note:
 *   The returned pointer may not be the same as the input pointer if leading
 *   whitespace is present.
 */
char* trim_string_inplace(char *str)
{
    if (!str)
        return str;

    /* Trim leading whitespace */
    while (*str && isspace((unsigned char)*str))
        str++;

    /* Trim trailing whitespace */
    char *end = str + strlen(str) - 1;
    while (end >= str && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    return str;
}

/**
 * @brief Parse and validate a PID list string.
 *
 * Parses a PID list specification into an array of integers. Supports:
 * - Single value (e.g., "150") for auto-increment
 * - Comma-separated list (e.g., "150,151,152") for explicit PIDs
 *
 * Returns 0 on success, allocating memory for pids_out.
 * Caller is responsible for freeing pids_out with free().
 */
int parse_pid_list(const char *pid_str, int **pids_out, int *count_out, char *errmsg)
{
    if (!pid_str || !pids_out || !count_out || !errmsg) {
        if (errmsg) snprintf(errmsg, 256, "Internal error: NULL parameter");
        return -1;
    }

    *pids_out = NULL;
    *count_out = 0;

    /* Make a working copy to parse */
    char *pid_copy = strdup(pid_str);
    if (!pid_copy) {
        snprintf(errmsg, 256, "Out of memory");
        return -1;
    }

    /* Count commas to estimate array size */
    int max_pids = 1;
    for (const char *p = pid_copy; *p; p++) {
        if (*p == ',') max_pids++;
    }

    /* Allocate PID array */
    int *pids = malloc(max_pids * sizeof(int));
    if (!pids) {
        snprintf(errmsg, 256, "Out of memory allocating PID array");
        free(pid_copy);
        return -1;
    }

    /* Parse comma-separated values */
    int pid_count = 0;
    char *saveptr = NULL;
    char *token = strtok_r(pid_copy, ",", &saveptr);

    while (token && pid_count < max_pids) {
        /* Trim whitespace */
        token = trim_string_inplace(token);

        /* Parse as integer */
        char *endptr;
        errno = 0;
        long val = strtol(token, &endptr, 10);

        /* Check for conversion errors */
        if (errno != 0 || endptr == token || *endptr != '\0') {
            snprintf(errmsg, 256, "Invalid PID value: '%s' is not a valid integer", token);
            free(pids);
            free(pid_copy);
            return -1;
        }

        /* Validate range: DVB PIDs must be between 32 and 8186 (reserved ranges excluded) */
        if (val < 32 || val > 8186) {
            snprintf(errmsg, 256, "PID value %ld out of valid range (32-8186). Range 0-31 are reserved for system PIDs.", val);
            free(pids);
            free(pid_copy);
            return 1;  /* Return 1 for range error (distinct from parse error) */
        }

        /* Check for duplicate PIDs within the list */
        for (int i = 0; i < pid_count; i++) {
            if (pids[i] == (int)val) {
                snprintf(errmsg, 256, "Duplicate PID value: %ld is specified multiple times", val);
                free(pids);
                free(pid_copy);
                return 1;  /* Return 1 for duplicate error */
            }
        }

        pids[pid_count++] = (int)val;
        token = strtok_r(NULL, ",", &saveptr);
    }

    free(pid_copy);

    if (pid_count == 0) {
        snprintf(errmsg, 256, "No valid PID values parsed");
        free(pids);
        return -1;
    }

    *pids_out = pids;
    *count_out = pid_count;
    return 0;
}

/**
 * parse_subtitle_positions
 *
 * Parse and validate subtitle positioning specification string.
 * Format: "position[,margin_top,margin_left,margin_bottom,margin_right];..."
 *
 * @param spec_str Input specification string (NULL means use all defaults)
 * @param configs Array of SubtitlePositionConfig[8] to populate
 * @param ntracks Number of tracks to configure
 * @param errmsg Error message buffer (at least 256 bytes)
 * @return 0 on success, -1 on parse error, 1 on value error
 */
int parse_subtitle_positions(const char *spec_str, SubtitlePositionConfig *configs,
                             int ntracks, char *errmsg)
{
    if (!configs || ntracks < 1 || ntracks > 8 || !errmsg) {
        return -1;
    }

    /* Initialize all tracks with defaults */
    for (int i = 0; i < ntracks; i++) {
        configs[i].position = SUB_POS_BOT_CENTER;
        configs[i].margin_top = 3.5;
        configs[i].margin_left = 2;
        configs[i].margin_bottom = 3.5;
        configs[i].margin_right = 2;
    }

    /* If no spec provided, use all defaults */
    if (!spec_str || spec_str[0] == '\0') {
        return 0;
    }

    char *spec_copy = strdup(spec_str);
    if (!spec_copy) {
        snprintf(errmsg, 256, "Out of memory parsing subtitle positions");
        return -1;
    }

    char *save_outer = NULL;
    char *track_spec = strtok_r(spec_copy, ";", &save_outer);
    int track_idx = 0;

    while (track_spec && track_idx < ntracks) {
        track_spec = trim_string_inplace(track_spec);

        /* Parse format: "position[,margin_top,margin_left,margin_bottom,margin_right]" */
        char *save_inner = NULL;
        char *part = strtok_r(track_spec, ",", &save_inner);

        if (!part) {
            snprintf(errmsg, 256, "Empty track position specification at index %d", track_idx);
            free(spec_copy);
            return -1;
        }

        /* Parse position name */
        part = trim_string_inplace(part);
        SubtitlePosition pos = SUB_POS_BOT_CENTER;

        if (strcasecmp(part, "top-left") == 0 || strcasecmp(part, "1") == 0) {
            pos = SUB_POS_TOP_LEFT;
        } else if (strcasecmp(part, "top-center") == 0 || strcasecmp(part, "2") == 0) {
            pos = SUB_POS_TOP_CENTER;
        } else if (strcasecmp(part, "top-right") == 0 || strcasecmp(part, "3") == 0) {
            pos = SUB_POS_TOP_RIGHT;
        } else if (strcasecmp(part, "middle-left") == 0 || strcasecmp(part, "mid-left") == 0 || strcasecmp(part, "4") == 0) {
            pos = SUB_POS_MID_LEFT;
        } else if (strcasecmp(part, "middle-center") == 0 || strcasecmp(part, "mid-center") == 0 || strcasecmp(part, "center") == 0 || strcasecmp(part, "5") == 0) {
            pos = SUB_POS_MID_CENTER;
        } else if (strcasecmp(part, "middle-right") == 0 || strcasecmp(part, "mid-right") == 0 || strcasecmp(part, "6") == 0) {
            pos = SUB_POS_MID_RIGHT;
        } else if (strcasecmp(part, "bottom-left") == 0 || strcasecmp(part, "bot-left") == 0 || strcasecmp(part, "7") == 0) {
            pos = SUB_POS_BOT_LEFT;
        } else if (strcasecmp(part, "bottom-center") == 0 || strcasecmp(part, "bot-center") == 0 || strcasecmp(part, "8") == 0) {
            pos = SUB_POS_BOT_CENTER;
        } else if (strcasecmp(part, "bottom-right") == 0 || strcasecmp(part, "bot-right") == 0 || strcasecmp(part, "9") == 0) {
            pos = SUB_POS_BOT_RIGHT;
        } else {
            snprintf(errmsg, 256, "Invalid position '%s' at track %d. Valid: top-left, top-center, top-right, middle-left, middle-center, middle-right, bottom-left, bottom-center, bottom-right", part, track_idx);
            free(spec_copy);
            return -1;
        }

        configs[track_idx].position = pos;

        /* Parse margins (optional) */
        double margins[4] = {0.0, 0.0, 0.0, 0.0};  /* top, left, bottom, right */
        int margin_idx = 0;

        while ((part = strtok_r(NULL, ",", &save_inner)) && margin_idx < 4) {
            part = trim_string_inplace(part);
            char *endptr;
            errno = 0;
            double val = strtod(part, &endptr);

            if (errno != 0 || endptr == part || *endptr != '\0') {
                snprintf(errmsg, 256, "Invalid margin value '%s' at track %d margin %d", part, track_idx, margin_idx);
                free(spec_copy);
                return -1;
            }

            if (val < 0.0 || val > 50.0) {
                snprintf(errmsg, 256, "Margin value %.1f%% at track %d margin %d out of range (0.0-50.0%%)", val, track_idx, margin_idx);
                free(spec_copy);
                return 1;
            }

            margins[margin_idx++] = val;
        }

        /* Assign parsed margins to config */
        if (margin_idx >= 1) configs[track_idx].margin_top = margins[0];
        if (margin_idx >= 2) configs[track_idx].margin_left = margins[1];
        if (margin_idx >= 3) configs[track_idx].margin_bottom = margins[2];
        if (margin_idx >= 4) configs[track_idx].margin_right = margins[3];

        track_idx++;
        track_spec = strtok_r(NULL, ";", &save_outer);
    }

    free(spec_copy);

    /* If fewer specs than tracks, remaining tracks use defaults (already initialized) */
    return 0;
}

/**
 * Extract ASS/SSA alignment number from Pango markup and remove the tag.
 *
 * Searches for {\an<digit>} tags and converts to SubtitlePosition.
 * Also removes the tag from the markup string in-place.
 * ASS alignment: keypad layout (7=TL, 8=TC, 9=TR, 4=ML, 5=MC, 6=MR, 1=BL, 2=BC, 3=BR, 0=default)
 */
SubtitlePositionConfig* extract_ass_alignment(char *markup)
{
    if (!markup) return NULL;
    
    /* Search for {\an<digit>} pattern */
    const char *pattern = "{\\an";
    char *pos = strstr(markup, pattern);
    
    if (!pos) return NULL;  /* No alignment tag found */
    
    /* Extract the digit after {\an */
    char *digit_pos = pos + strlen(pattern);
    if (!*digit_pos || *digit_pos < '0' || *digit_pos > '9') return NULL;  /* Invalid format */
    
    int ass_align = *digit_pos - '0';  /* Convert char to int */
    
    /* Find the closing brace */
    char *close_brace = digit_pos + 1;
    if (*close_brace != '}') return NULL;  /* Invalid format */
    
    if (ass_align == 0) {
        /* 0 means "use default" - still remove the tag but don't return config */
        /* Remove {\an0} from markup by shifting remaining string */
        memmove(pos, close_brace + 1, strlen(close_brace + 1) + 1);
        return NULL;
    }
    
    /* Remove {\an<digit>} from markup by shifting remaining string */
    memmove(pos, close_brace + 1, strlen(close_brace + 1) + 1);
    
    /* Allocate config and convert ASS alignment to SubtitlePosition */
    SubtitlePositionConfig *config = malloc(sizeof(SubtitlePositionConfig));
    if (!config) return NULL;
    
    /* Initialize with defaults */
    config->margin_top = 3.5;
    config->margin_left = 3.5;
    config->margin_bottom = 3.5;
    config->margin_right = 3.5;
    
    /* Convert ASS keypad number (1-9) to SubtitlePosition enum
     * ASS: 7=TL, 8=TC, 9=TR, 4=ML, 5=MC, 6=MR, 1=BL, 2=BC, 3=BR
     * Our enum: 1=TL, 2=TC, 3=TR, 4=ML, 5=MC, 6=MR, 7=BL, 8=BC, 9=BR
     * Direct mapping: SubtitlePosition = ass_align
     */
    switch (ass_align) {
        case 7: config->position = SUB_POS_TOP_LEFT; break;
        case 8: config->position = SUB_POS_TOP_CENTER; break;
        case 9: config->position = SUB_POS_TOP_RIGHT; break;
        case 4: config->position = SUB_POS_MID_LEFT; break;
        case 5: config->position = SUB_POS_MID_CENTER; break;
        case 6: config->position = SUB_POS_MID_RIGHT; break;
        case 1: config->position = SUB_POS_BOT_LEFT; break;
        case 2: config->position = SUB_POS_BOT_CENTER; break;
        case 3: config->position = SUB_POS_BOT_RIGHT; break;
        default:
            free(config);
            return NULL;  /* Invalid alignment value */
    }
    
    LOG(3, "DEBUG: Extracted ASS alignment \\an%d and removed tag from markup -> position %d\n", ass_align, config->position);
    
    return config;
}

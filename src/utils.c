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
#include "debug.h"
#include "dvb_lang.h"
#include "utils.h"
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
    printf("  -I, --input FILE            Input media file\n");
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
    printf("      --palette MODE          Palette mode (ebu-broadcast|broadcast|web|legacy)\n");
    printf("      --font FONTNAME         Set font family (default is DejaVu Sans)\n");
    printf("      --font-style STYLE      Optional font style variant (e.g. Bold, Italic, Light)\n");
    printf("      --fontsize N            Set font size in px (overrides dynamic sizing)\n");
    printf("      --fgcolor #RRGGBB       Text color (in quotes i.e. \"#00ff00)\"\n");
    printf("      --outlinecolor #RRGGBB  Outline color (in quotes i.e. \"#808080\")\n");
    printf("      --shadowcolor #AARRGGBB Shadow color (alpha optional...in quotes i.e. \"#00808080\")\n");
    printf("      --bg-color #RRGGBB      Background color (in quotes i.e. \"#000000\")\n");
    printf("      --ssaa N                Force supersample factor (1..24) (default 4)\n");        
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
 * This function outputs the build version, commit hash, and build date
 * using the predefined macros GIT_VERSION, GIT_COMMIT, and GIT_DATE.
 */
void print_version(void)
{
    printf("\nsrt2dvbsub Version: %s (%s, %s)\n\n", GIT_VERSION, GIT_COMMIT, GIT_DATE);
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


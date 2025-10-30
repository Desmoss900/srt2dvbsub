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
#include "srt_parser.h"
#include "qc.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>

#define MAX_TAG_STACK 32
/* --- Configurable limits --- */
#define MAX_LINES_SD 3
#define MAX_CHARS_SD 37
#define MAX_CHARS_HD 67

/* Imported from main.c -- these are used to decide SD/HD wrapping rules */
extern int use_ass;
extern int video_w;
extern int video_h;
extern int debug_level;

/*
 * srt_parser.c
 * --------------
 * Implements a minimal, robust SRT file parser tailored for the project's
 * subtitle normalization requirements. Key behaviors:
 *  - Strips an optional UTF-8 BOM on the first line (prevents sscanf failures)
 *  - Parses numeric cue indices and timestamp lines (HH:MM:SS,ms --> HH:MM:SS,ms)
 *  - Joins and normalizes cue text (whitespace, line wrapping) using
 *    SD/HD heuristics to avoid overly long lines on small displays.
 *  - Optionally preserves ASS markup (when `use_ass` is set) or converts
 *    simple HTML tags into ASS overrides.
 *
 * The parser favors simplicity and predictable outputs rather than full
 * compliance with every SRT/ASS edge case. Caller responsibility:
 *  - Free the returned `entries_out` array and each entry's `text` member.
 */


/*
 * Count Unicode codepoints (visible characters) in a UTF-8 string.
 *
 * This counts codepoints rather than bytes so length checks and wrapping
 * operate on human-visible characters. The function tolerates invalid
 * UTF-8 by treating invalid bytes as single-width characters.
 */
static int u8_len(const char *s) {
    int count = 0;
    const unsigned char *p = (const unsigned char *)s;

    /*
     * Walk the UTF-8 string safely. For multi-byte sequences, ensure
     * the required continuation bytes exist and have the form 0b10xxxxxx.
     * If a sequence is truncated or invalid, treat the lead byte as a
     * single character (be liberal in what we accept).
     */
    while (*p) {
        if ((*p & 0x80) == 0) {
            /* ASCII */
            p++;
        } else if (((*p & 0xE0) == 0xC0) && p[1] && ((p[1] & 0xC0) == 0x80)) {
            /* 2-byte */
            p += 2;
        } else if (((*p & 0xF0) == 0xE0) && p[1] && p[2] && ((p[1] & 0xC0) == 0x80) && ((p[2] & 0xC0) == 0x80)) {
            /* 3-byte */
            p += 3;
        } else if (((*p & 0xF8) == 0xF0) && p[1] && p[2] && p[3] && ((p[1] & 0xC0) == 0x80) && ((p[2] & 0xC0) == 0x80) && ((p[3] & 0xC0) == 0x80)) {
            /* 4-byte */
            p += 4;
        } else {
            /* Invalid or truncated sequence — treat as single byte */
            p++;
        }
        count++;
    }
    return count;
}

/*
 * Compute the display length of `s` while ignoring inline markup wrapped
 * in '<...>' (HTML) or '{...}' (ASS). This allows whitespace/line wrapping
 * logic to operate on the visible glyph count while retaining formatting
 * tags in the output.
 */
static int visible_len(const char *s) {
    if (!s) return 0;
    int count = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p == '<') {
            const unsigned char *q = (const unsigned char *)strchr((const char *)p, '>');
            if (q) { p = q + 1; continue; }
        }
        if (*p == '{') {
            const unsigned char *q = (const unsigned char *)strchr((const char *)p, '}');
            if (q) { p = q + 1; continue; }
        }
        if ((*p & 0x80) == 0) {
            p++;
        } else if (((*p & 0xE0) == 0xC0) && p[1] && ((p[1] & 0xC0) == 0x80)) {
            p += 2;
        } else if (((*p & 0xF0) == 0xE0) && p[1] && p[2] &&
                   ((p[1] & 0xC0) == 0x80) && ((p[2] & 0xC0) == 0x80)) {
            p += 3;
        } else if (((*p & 0xF8) == 0xF0) && p[1] && p[2] && p[3] &&
                   ((p[1] & 0xC0) == 0x80) && ((p[2] & 0xC0) == 0x80) && ((p[3] & 0xC0) == 0x80)) {
            p += 4;
        } else {
            p++;
        }
        count++;
    }
    return count;
}


/*
 * Normalize cue text: collapse whitespace, join lines and wrap to
 * `max_chars` per line using `max_lines`. The function returns a newly
 * allocated string which the caller must free.
 *
 * The routine uses UTF-8 codepoint counting via u8_len so wrapping is
 * character-aware for non-ASCII languages.
 */
static char* normalize_cue_text(const char *raw, int is_hd) {
    int max_lines = MAX_LINES_SD;
    int max_chars = is_hd ? MAX_CHARS_HD : MAX_CHARS_SD;

    /* Step 1: normalize whitespace and join lines into a dynamic buffer */
    size_t raw_len = raw ? strlen(raw) : 0;
    size_t buf_cap = raw_len + 1;
    char *buf = NULL;
    char *out = NULL;
    char *tmp = NULL;

    buf = malloc(buf_cap);
    if (!buf) goto oom;
    size_t buf_len = 0;
    buf[0] = '\0';

    const char *p = raw;
    int last_space = 0;
    while (p && *p) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\n' || ch == '\r' || isspace(ch)) {
            if (!last_space) {
                if (buf_len + 1 >= buf_cap) {
                    size_t nc = buf_cap * 2 + 16;
                    char *nt = realloc(buf, nc);
                    if (!nt) goto oom;
                    buf = nt; buf_cap = nc;
                }
                buf[buf_len++] = ' ';
                buf[buf_len] = '\0';
                last_space = 1;
            }
            p++;
        } else {
            if (buf_len + 1 >= buf_cap) {
                size_t nc = buf_cap * 2 + 16;
                char *nt = realloc(buf, nc);
                if (!nt) goto oom;
                buf = nt; buf_cap = nc;
            }
            buf[buf_len++] = *p++;
            buf[buf_len] = '\0';
            last_space = 0;
        }
    }

    /* Prepare output dynamic buffer */
    size_t out_cap = buf_len + 1 + (size_t)max_lines + 8;
    out = malloc(out_cap);
    if (!out) goto oom;
    size_t out_len = 0;
    out[0] = '\0';

    /* Tokenize using reentrant strtok_r */
    tmp = strdup(buf);
    if (!tmp) goto oom;
    char *saveptr = NULL;
    char *tok = strtok_r(tmp, " ", &saveptr);

    int whole_cue_is_symbol = 0;
    char *plain = strip_tags(raw);
    if (plain) {
        int len = u8_len(plain);
        if (len == 1 && ((unsigned char)plain[0] & 0x80)) whole_cue_is_symbol = 1;
        free(plain);
    }

    int line_len = 0, lines = 1;

    while (tok) {
        int wordlen = visible_len(tok);
        int sym_line = (whole_cue_is_symbol && wordlen == 1);

        if (sym_line) {
            if (line_len > 0) {
                if (out_len + 1 >= out_cap) {
                    size_t nc = out_cap * 2 + 16;
                    char *nt = realloc(out, nc);
                    if (!nt) goto oom;
                    out = nt; out_cap = nc;
                }
                out[out_len++] = '\n'; out[out_len] = '\0';
            }
            size_t tlen = strlen(tok);
            if (out_len + tlen >= out_cap) {
                size_t nc = out_len + tlen + 16;
                char *nt = realloc(out, nc);
                if (!nt) goto oom;
                out = nt; out_cap = nc;
            }
            memcpy(out + out_len, tok, tlen);
            out_len += tlen; out[out_len] = '\0';
            line_len = wordlen;
            lines++;
        }
        else if (wordlen > 0 && line_len + wordlen + 1 > max_chars && lines < max_lines) {
            /* break line */
            if (out_len + 1 >= out_cap) {
                size_t nc = out_cap * 2 + 16;
                char *nt = realloc(out, nc);
                if (!nt) goto oom;
                out = nt; out_cap = nc;
            }
            out[out_len++] = '\n'; out[out_len] = '\0';
            line_len = 0; lines++;

            size_t tlen = strlen(tok);
            if (out_len + tlen >= out_cap) {
                size_t nc = out_len + tlen + 16;
                char *nt = realloc(out, nc);
                if (!nt) goto oom;
                out = nt; out_cap = nc;
            }
            memcpy(out + out_len, tok, tlen);
            out_len += tlen; out[out_len] = '\0';
            line_len = wordlen;
        }
        else {
            if (wordlen > 0 && line_len > 0) {
                if (out_len + 1 >= out_cap) {
                    size_t nc = out_cap * 2 + 16;
                    char *nt = realloc(out, nc);
                    if (!nt) goto oom;
                    out = nt; out_cap = nc;
                }
                out[out_len++] = ' '; out[out_len] = '\0';
                line_len++;
            }
            size_t tlen = strlen(tok);
            if (out_len + tlen >= out_cap) {
                size_t nc = out_len + tlen + 16;
                char *nt = realloc(out, nc);
                if (!nt) goto oom;
                out = nt; out_cap = nc;
            }
            memcpy(out + out_len, tok, tlen);
            out_len += tlen; out[out_len] = '\0';
            line_len += wordlen;
        }

        tok = strtok_r(NULL, " ", &saveptr);
    }

    free(tmp);
    free(buf);
    return out;

oom:
    free(tmp);
    free(out);
    free(buf);
    return NULL;
}

/*
 * Trim trailing CR/LF characters from a C string in-place.
 */
static void rstrip(char *s) {
    if (!s || !*s) return;
    char *end = s + strlen(s);
    while (end > s && (*(end-1) == '\n' || *(end-1) == '\r')) {
        *(--end) = '\0';
    }
}


/*
 * Parse an ASS color override tag like "{\c&HBBGGRR&}" or "{\1c&H...&}"
 * and produce a Pango/CSS '#RRGGBB' color string in `out`.
 */
static void parse_ass_color(const char *tag, char *out, size_t outsz) {
    unsigned int b=0,g=0,r=0;
    if (sscanf(tag, "{\\c&H%02X%02X%02X&}", &b,&g,&r) == 3 ||
        sscanf(tag, "{\\1c&H%02X%02X%02X&}", &b,&g,&r) == 3) {
        snprintf(out, outsz, "#%02X%02X%02X", r,g,b);
    } else {
        /* Use snprintf to ensure NUL-termination and avoid strncpy pitfalls */
        snprintf(out, outsz, "#FFFFFF");
    }
}

/*
 * Dynamically append `s` to a resizable buffer `*bufp` with capacity `*cap` and
 * current length `*lenp`. On success returns 0, on allocation failure returns -1.
 * The buffer is NUL-terminated and may be realloc'd; caller must update stored
 * pointers. This helper provides bounds-checked, growing-appends used by
 * tag/html normalization routines.
 */
static int dyn_append(char **bufp, size_t *cap, size_t *lenp, const char *s) {
    if (!bufp || !cap || !lenp || !s) return -1;
    size_t sl = strlen(s);
    size_t cur = *lenp;
    /* Ensure capacity for cur + sl + NUL */
    if (cur + sl + 1 > *cap) {
        size_t nc = *cap ? (*cap * 2 + sl + 16) : (sl + 16);
        while (cur + sl + 1 > nc) nc = nc * 2 + 16;
        char *nt = realloc(*bufp, nc);
        if (!nt) return -1;
        *bufp = nt;
        *cap = nc;
    }
    memcpy((*bufp) + cur, s, sl);
    (*bufp)[cur + sl] = '\0';
    *lenp = cur + sl;
    return 0;
}

/* Centralized logging helper for this file. Levels: 0=always, 1=info, 2=debug.
 * The function checks the global `debug_level` and prints to stderr when
 * appropriate. Format strings should NOT include a trailing newline unless
 * intended; we pass through the format as-is.
 */
static void sp_log(int level, const char *fmt, ...) {
    if (level > 0 && debug_level < level) return;
    va_list ap;
    va_start(ap, fmt);
    /* Prefix to identify origin */
    fprintf(stderr, "[srt_parser] ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/*
 * Replace ASS non-breaking space escape ("\h") with a normal space. 
 */
static void replace_ass_h(char *text) {
    if (!text) return;
    char *p = text;
    while ((p = strstr(p, "\\h")) != NULL) {
        *p = ' ';                    // remove '\'
        memmove(p+1, p+2, strlen(p+2)+1); // shift left to drop 'h'
        p++;                         // continue after the inserted space
    }
}


/*
 * Remove ASS non-breaking space escape sequences completely. 
 */
static void remove_ass_h(char *text) {
    if (!text) return;
    char *p = text;
    while ((p = strstr(p, "\\h")) != NULL) {
        memmove(p, p + 2, strlen(p + 2) + 1); // shift left by 2 to drop "\h"
    }
}

/*
 * Translate a subset of ASS override tags to Pango markup. The returned
 * string is newly allocated and must be freed by the caller. This is a
 * lightweight translator intended for basic styling (bold/italic/underline,
 * color, font face). Complex ASS features like transforms are ignored.
 */
static char* normalize_tags(const char *in) {
    if (!in) return NULL;
    size_t cap = strlen(in)*6 + 256;
    char *tmp = malloc(cap);
    if (!tmp) return NULL;
    tmp[0] = '\0';
    size_t tmp_len = 0;

    const char *stack[MAX_TAG_STACK];
    int sp = 0;

    const char *p = in;
    while (*p) {
        if (!strncmp(p, "{\\i1}", 5)) {
            if (dyn_append(&tmp, &cap, &tmp_len, "<i>") < 0) goto err;
            stack[sp++] = "</i>";
            if (sp >= MAX_TAG_STACK) sp = MAX_TAG_STACK-1;
            p += 5;
        }
        else if (!strncmp(p, "{\\i0}", 5)) {
            for (int j=sp-1; j>=0; j--) {
                if (strcmp(stack[j], "</i>")==0) {
                    if (dyn_append(&tmp, &cap, &tmp_len, stack[j]) < 0) goto err;
                    sp = j;
                    break;
                }
            }
            p += 5;
        }
        else if (!strncmp(p, "{\\b1}", 5)) {
            if (dyn_append(&tmp, &cap, &tmp_len, "<b>") < 0) goto err;
            stack[sp++] = "</b>";
            if (sp >= MAX_TAG_STACK) sp = MAX_TAG_STACK-1;
            p += 5;
        }
        else if (!strncmp(p, "{\\b0}", 5)) {
            for (int j=sp-1; j>=0; j--) {
                if (strcmp(stack[j], "</b>")==0) {
                    if (dyn_append(&tmp, &cap, &tmp_len, stack[j]) < 0) goto err;
                    sp = j;
                    break;
                }
            }
            p += 5;
        }
        else if (!strncmp(p, "{\\u1}", 5)) {
            if (dyn_append(&tmp, &cap, &tmp_len, "<u>") < 0) goto err;
            stack[sp++] = "</u>";
            if (sp >= MAX_TAG_STACK) sp = MAX_TAG_STACK-1;
            p += 5;
        }
        else if (!strncmp(p, "{\\u0}", 5)) {
            for (int j=sp-1; j>=0; j--) {
                if (strcmp(stack[j], "</u>")==0) {
                    if (dyn_append(&tmp, &cap, &tmp_len, stack[j]) < 0) goto err;
                    sp = j;
                    break;
                }
            }
            p += 5;
        }
        else if (!strncmp(p, "{\\c&H", 5) || !strncmp(p, "{\\1c&H", 6)) {
            for (int j=sp-1; j>=0; j--) {
                if (strncmp(stack[j], "</span>", 7)==0) {
                    if (dyn_append(&tmp, &cap, &tmp_len, stack[j]) < 0) goto err;
                    sp = j;
                    break;
                }
            }
            char color[16];
            parse_ass_color(p, color, sizeof(color));
            char span[64];
            snprintf(span, sizeof(span), "<span foreground=\"%s\">", color);
            if (dyn_append(&tmp, &cap, &tmp_len, span) < 0) goto err;
            stack[sp++] = "</span>";
            if (sp >= MAX_TAG_STACK) sp = MAX_TAG_STACK-1;
            const char *q = strchr(p, '}');
            if (q) p = q+1;
            else { if (dyn_append(&tmp, &cap, &tmp_len, "{") < 0) goto err; p++; }
        }
        else if (!strncmp(p, "{\\fn", 4)) {
            for (int j=sp-1; j>=0; j--) {
                if (strncmp(stack[j], "</span>", 7)==0) {
                    if (dyn_append(&tmp, &cap, &tmp_len, stack[j]) < 0) goto err;
                    sp = j;
                    break;
                }
            }
            const char *q = strchr(p, '}');
            if (q) {
                char fontname[128];
                size_t len = (size_t)(q - (p+3));
                if (len >= sizeof(fontname)) len = sizeof(fontname)-1;
                memcpy(fontname, p+3, len);
                fontname[len] = 0;
                char span[256];
                snprintf(span, sizeof(span), "<span font=\"%s\">", fontname);
                if (dyn_append(&tmp, &cap, &tmp_len, span) < 0) goto err;
                stack[sp++] = "</span>";
                if (sp >= MAX_TAG_STACK) sp = MAX_TAG_STACK-1;
                p = q+1;
            } else {
                p++;
            }
        }
        else if (!strncmp(p, "{\\pos", 5) || !strncmp(p, "{\\move", 6) ||
                 !strncmp(p, "{\\fad", 5) || !strncmp(p, "{\\org", 5)) {
            const char *q = strchr(p, '}');
            if (q) p = q+1;
            else { if (dyn_append(&tmp, &cap, &tmp_len, "{") < 0) goto err; p++; }
        }
        else {
            char one[2] = { *p++, '\0' };
            if (dyn_append(&tmp, &cap, &tmp_len, one) < 0) goto err;
        }
    }

    for (int j=sp-1; j>=0; j--) {
        if (dyn_append(&tmp, &cap, &tmp_len, stack[j]) < 0) goto err;
    }

    return tmp;
err:
    free(tmp);
    return NULL;
}

/*
 * Parse an SRT file into an array of SRTEntry structures.
 *
 * The function opens `filename`, reads cues, normalizes text and performs
 * lightweight QC checks which are written to `qc` when provided.
 *
 * Return: number of parsed cues on success, -1 on error (file open or
 * allocation failure). Caller frees the allocated entries and their text.
 */
/* Internal implementation that accepts explicit config values instead of
 * relying on external globals. This is the core parser logic; public
 * wrappers call this with either global values or explicit config.
 */
static int parse_srt_internal(const char *filename, SRTEntry **entries_out, FILE *qc,
                              int use_ass_local, int video_w_local, int video_h_local) {
    extern int debug_level;
    FILE *f = fopen(filename, "r");
    if (!f) {
        if (debug_level > 0) {
            sp_log(1, "Failed to open SRT '%s': %s\n", filename, strerror(errno));
        }
        return -1;
    }

    char line[2048];
    size_t cap = 128;
    size_t n = 0;
    *entries_out = calloc(cap, sizeof(SRTEntry));
    if (!*entries_out) {
        if (debug_level > 0) {
            sp_log(1, "Failed to allocate entries array for '%s'\n", filename);
        }
        fclose(f); return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        rstrip(line);
        if (strlen(line) == 0) continue;

        /* Strip UTF-8 BOM if present at start of line. Some SRT files
         * are saved with a BOM which makes sscanf("%d") fail on the
         * cue index line and causes the parser to skip the first cue. */
        if ((unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF) {
            memmove(line, line + 3, strlen(line + 3) + 1);
        }

        int idx = 0;
        if (sscanf(line, "%d", &idx) == 1) {
            if (!fgets(line, sizeof(line), f)) break;
            rstrip(line);
        }

        int h1,m1,s1,ms1,h2,m2,s2,ms2;
        if (sscanf(line, "%d:%d:%d,%d --> %d:%d:%d,%d",
                   &h1,&m1,&s1,&ms1,&h2,&m2,&s2,&ms2) != 8) {
            continue;
        }
        /* Validate parsed timestamp fields to avoid accepting malformed cues */
        if (m1 < 0 || m1 > 59 || s1 < 0 || s1 > 59 || ms1 < 0 || ms1 > 999 ||
            m2 < 0 || m2 > 59 || s2 < 0 || s2 > 59 || ms2 < 0 || ms2 > 999) {
            if (debug_level > 0) {
                sp_log(1, "invalid timestamp ranges in line: '%s'\n", line);
            }
            continue;
        }
        int64_t start = ((int64_t)h1*3600 + m1*60 + s1) * 1000 + ms1;
        int64_t end   = ((int64_t)h2*3600 + m2*60 + s2) * 1000 + ms2;
        if (end <= start) {
            if (debug_level > 0) {
                sp_log(1, "invalid cue timing (end <= start) in line: '%s'\n", line);
            }
            continue;
        }

        char textbuf[8192] = {0};
        while (fgets(line, sizeof(line), f)) {
            rstrip(line);
            if (strlen(line) == 0) break;
            if (strlen(textbuf)+strlen(line)+2 < sizeof(textbuf)) {
                strcat(textbuf, line);
                strcat(textbuf, "\n");
            }
        }

        if (n >= cap) {
            cap *= 2;
            /* Use a temporary pointer to avoid losing the original on failure */
            SRTEntry *tmp = realloc(*entries_out, cap * sizeof(SRTEntry));
            if (!tmp) {
                if (debug_level > 0) {
                    sp_log(1, "realloc failed expanding to %zu entries for '%s'\n", cap, filename);
                }
                /* Clean up and return error; caller gets no partial results */
                free(*entries_out);
                *entries_out = NULL;
                fclose(f);
                return -1;
            }
            *entries_out = tmp;
        }

    /* Decide SD vs HD by provided resolution */
        int is_hd = (video_w_local > 720 || video_h_local > 576);

        char *norm = NULL;
        if (use_ass_local) {
            norm = normalize_tags(textbuf);
            if (!norm) {
                norm = strdup(textbuf);
                if (!norm) {
                    if (debug_level > 0) sp_log(1, "allocation failed creating norm for cue %d in '%s'\n", (int)n, filename);
                        /* cleanup allocated entries */
                        for (size_t i = 0; i < n; i++) free((*entries_out)[i].text);
                    free(*entries_out);
                    *entries_out = NULL;
                    fclose(f);
                    return -1;
                }
            }
            replace_ass_h(norm);
        } else {
            norm = normalize_cue_text(textbuf, is_hd);
            if (!norm) {
                if (debug_level > 0) sp_log(1, "allocation failed normalizing cue text for cue %d in '%s'\n", (int)n, filename);
                for (size_t i = 0; i < n; i++) free((*entries_out)[i].text);
                free(*entries_out);
                *entries_out = NULL;
                fclose(f);
                return -1;
            }
            remove_ass_h(norm);
    }

        (*entries_out)[n].start_ms = start;
        (*entries_out)[n].end_ms   = end;
        (*entries_out)[n].text     = norm;

        /* Force at least 50ms gap between adjacent cues */
        if (n > 0 && (*entries_out)[n].start_ms <= (*entries_out)[n-1].end_ms) {
            (*entries_out)[n-1].end_ms = (*entries_out)[n].start_ms - 50;
            if ((*entries_out)[n-1].end_ms < (*entries_out)[n-1].start_ms) {
                /* never allow negative duration */
                (*entries_out)[n-1].end_ms = (*entries_out)[n-1].start_ms + 1;
            }
            (*entries_out)[n].start_ms = (*entries_out)[n-1].end_ms + 50;

                if (debug_level > 0) {
                sp_log(1,
                    "Overlap HARD-corrected between cue %d and %d → new: end[%d]=%lld, start[%d]=%lld\n",
                    (int)(n-1), (int)n,
                    (int)(n-1), (long long)(*entries_out)[n-1].end_ms,
                    (int)n,   (long long)(*entries_out)[n].start_ms);
            }
        }

       
        if (debug_level > 1) {
            sp_log(2,
                "Cue %d: %lld → %lld ms | text='%s'\n",
                (int)n,
                (long long)(*entries_out)[n].start_ms,
                (long long)(*entries_out)[n].end_ms,
                norm);
        }

        int align = 2;
        const char *tag = strstr(textbuf, "{\\an");
        if (tag && strlen(tag) >= 5) {
            int code = tag[4]-'0';
            if (code >=1 && code <=9) align = code;
        }
        (*entries_out)[n].alignment = align;

    /* Use stripped text (no HTML/ASS tags) for QC length checks */
        char *plain = strip_tags((*entries_out)[n].text);
        if (!plain) {
            if (debug_level > 0) sp_log(1, "allocation failed stripping plain text for QC for cue %d in '%s'\n", (int)n, filename);
            /* cleanup and abort */
            for (size_t i = 0; i <= n; i++) free((*entries_out)[i].text);
            free(*entries_out);
            *entries_out = NULL;
            fclose(f);
            return -1;
        }
        SRTEntry tmp = (*entries_out)[n];
        tmp.text = plain;

        qc_check_entry(filename, (int)n, &tmp,
                    (n > 0 ? &(*entries_out)[n-1] : NULL), qc);

        if (debug_level > 1) {
            sp_log(2,
                "Cue %d: %lld → %lld ms | raw_len=%d plain_len=%d | text='%s'\n",
                (int)n,
                (long long)(*entries_out)[n].start_ms,
                (long long)(*entries_out)[n].end_ms,
                u8_len((*entries_out)[n].text),
                u8_len(plain),
                plain);
        }

        free(plain);

        n++;
    }
    fclose(f);
    return (int)n;
}

/* Backwards-compatible wrapper that reads configuration from globals. */
int parse_srt(const char *filename, SRTEntry **entries_out, FILE *qc) {
    return parse_srt_internal(filename, entries_out, qc, use_ass, video_w, video_h);
}

/* Public API accepting explicit configuration. If cfg is NULL, fallback
 * to using global variables to preserve compatibility.
 */
int parse_srt_cfg(const char *filename, SRTEntry **entries_out, FILE *qc, const SRTParserConfig *cfg) {
    if (cfg) return parse_srt_internal(filename, entries_out, qc, cfg->use_ass, cfg->video_w, cfg->video_h);
    return parse_srt(filename, entries_out, qc);
}

/* 
 * Convert minimal HTML tags (<i>, <b>, <font color>) into ASS overrides.
 * The caller frees the returned string. 
 */
char* srt_html_to_ass(const char *in) {
    if (!in) return NULL;
    size_t cap = strlen(in) * 8 + 128;
    char *out = malloc(cap);
    if (!out) return NULL;
    out[0] = '\0';
    size_t out_len = 0;
    const char *p = in;
    while (*p) {
        if (!strncasecmp(p,"<i>",3))      { if (dyn_append(&out,&cap,&out_len,"{\\i1}") < 0) goto err; p+=3; }
        else if (!strncasecmp(p,"</i>",4)){ if (dyn_append(&out,&cap,&out_len,"{\\i0}") < 0) goto err; p+=4; }
        else if (!strncasecmp(p,"<b>",3)) { if (dyn_append(&out,&cap,&out_len,"{\\b1}") < 0) goto err; p+=3; }
        else if (!strncasecmp(p,"</b>",4)){ if (dyn_append(&out,&cap,&out_len,"{\\b0}") < 0) goto err; p+=4; }
        else if (!strncasecmp(p,"<font color=",12)) {
            const char *q = strchr(p,'"'); if (!q) { p++; continue; }
            const char *r = strchr(q+1,'"'); if (!r) { p++; continue; }
            char color[16];
            size_t color_len = (size_t)(r - (q+1));
            if (color_len >= sizeof(color)) color_len = sizeof(color) - 1;
            memcpy(color, q+1, color_len);
            color[color_len] = 0;
            unsigned rr=255,gg=255,bb=255;
            if (color[0]=='#' && strlen(color)==7)
                sscanf(color+1,"%02x%02x%02x",&rr,&gg,&bb);
            char tag[64];
            snprintf(tag,sizeof(tag),"{\\c&H%02X%02X%02X&}",bb,gg,rr);
            if (dyn_append(&out,&cap,&out_len,tag) < 0) goto err;
            p = r+2; // skip ">"
        }
        else if (!strncasecmp(p,"<font face=",11)) {
            const char *q = strchr(p,'"'); if (!q) { p++; continue; }
            const char *r = strchr(q+1,'"'); if (!r) { p++; continue; }
            char face[64];
            size_t face_len = (size_t)(r - (q+1));
            if (face_len >= sizeof(face)) face_len = sizeof(face) - 1;
            memcpy(face, q+1, face_len);
            face[face_len] = 0;
            char tag[128];
            snprintf(tag,sizeof(tag),"{\\fn%s}",face);
            if (dyn_append(&out,&cap,&out_len,tag) < 0) goto err;
            p = r+2; // skip ">"
        }
        else if (!strncasecmp(p,"</font>",7)) {
            if (dyn_append(&out,&cap,&out_len,"{\\r}") < 0) goto err;
            p+=7;
        }
        else { char one[2] = { *p++, '\0' }; if (dyn_append(&out,&cap,&out_len,one) < 0) goto err; }
    }
    return out;
err:
    free(out);
    return NULL;
}

/*
 * Strip ASS/HTML tags for plain-text length/QC calculations. Returns a
 * newly allocated C string which the caller must free.
 */
char* strip_tags(const char *in) {
    if (!in) {
        char *empty = malloc(1);
        if (!empty) return NULL;
        empty[0] = '\0';
        return empty;
    }
    size_t in_len = strlen(in);
    char *out = malloc(in_len + 1);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < in_len; i++) {
        if (in[i] == '{') {
            const char *q = strchr(in + i, '}');
            if (q) {
                /* skip the tag including closing brace */
                i = (size_t)(q - in);
            } else {
                /* no closing brace — treat '{' as literal */
                out[j++] = in[i];
            }
        }
        else if (in[i] == '<') {
            const char *q = strchr(in + i, '>');
            if (q) {
                i = (size_t)(q - in);
            } else {
                out[j++] = in[i];
            }
        }
        else {
            out[j++] = in[i];
        }
    }
    out[j] = '\0';
    return out;
}

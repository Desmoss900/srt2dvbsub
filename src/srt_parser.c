/*
* Copyright (c) 2025 Mark E. Rosche, Capsaworks Project
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
*   Mark E. Rosche, Capsaworks Project
*   Email: license@capsaworks-project.de
*   Website: www.capsaworks-project.de
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
        /* Skip HTML tags: <...> */
        if (*p == '<') {
            const unsigned char *q = (const unsigned char *)strchr((const char *)p, '>');
            if (q) { p = q + 1; continue; }
        }
        /* Skip ASS/Pango tags: {...}
         * This includes ASS alignment {\an<digit>}, color tags, style tags, etc. */
        if (*p == '{') {
            const unsigned char *q = (const unsigned char *)strchr((const char *)p, '}');
            if (q) { p = q + 1; continue; }
        }
        /* Handle UTF-8 multibyte sequences */
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
 * Clean up resources allocated in normalize_cue_text on error.
 * All pointers are set to NULL after freeing.
 */
static void normalize_cue_text_cleanup(char **buf, char **out, char **tmp) {
    if (buf && *buf) { free(*buf); *buf = NULL; }
    if (out && *out) { free(*out); *out = NULL; }
    if (tmp && *tmp) { free(*tmp); *tmp = NULL; }
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
    if (!buf) return NULL;
    size_t buf_len = 0;
    buf[0] = '\0';

    const char *p = raw;
    int last_space = 0;
    while (p && *p) {
        unsigned char ch = (unsigned char)*p;
        /* Preserve explicit newlines from SRT file, but collapse other whitespace */
        if (ch == '\n') {
            /* Explicit newline: preserve it (unless preceded by space to avoid "space\n") */
            if (last_space && buf_len > 0 && buf[buf_len-1] == ' ') {
                buf[buf_len-1] = '\n';  /* Replace trailing space with newline */
            } else {
                if (buf_len + 1 >= buf_cap) {
                    size_t nc = buf_cap * 2 + 16;
                    char *nt = realloc(buf, nc);
                    if (!nt) {
                        normalize_cue_text_cleanup(&buf, &out, &tmp);
                        return NULL;
                    }
                    buf = nt; buf_cap = nc;
                }
                buf[buf_len++] = '\n';
                buf[buf_len] = '\0';
            }
            last_space = 0;
            p++;
        } else if (ch == '\r' || isspace(ch)) {
            /* Other whitespace (tabs, spaces, etc): collapse to single space */
            if (!last_space) {
                if (buf_len + 1 >= buf_cap) {
                    size_t nc = buf_cap * 2 + 16;
                    char *nt = realloc(buf, nc);
                    if (!nt) {
                        normalize_cue_text_cleanup(&buf, &out, &tmp);
                        return NULL;
                    }
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
                if (!nt) {
                    normalize_cue_text_cleanup(&buf, &out, &tmp);
                    return NULL;
                }
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
    if (!out) {
        normalize_cue_text_cleanup(&buf, &out, &tmp);
        return NULL;
    }
    size_t out_len = 0;
    out[0] = '\0';

    /* Process buffer line-by-line, preserving explicit newlines from SRT file */
    tmp = strdup(buf);
    if (!tmp) {
        normalize_cue_text_cleanup(&buf, &out, &tmp);
        return NULL;
    }

    int whole_cue_is_symbol = 0;
    char *plain = strip_tags(raw);
    if (plain) {
        int len = u8_len(plain);
        if (len == 1 && ((unsigned char)plain[0] & 0x80)) whole_cue_is_symbol = 1;
        free(plain);
    }

    int line_len = 0, lines = 1;

    /* Process line by line, respecting embedded newlines */
    char *line_start = tmp;
    while (line_start && *line_start) {
        /* Find end of current line */
        char *line_end = strchr(line_start, '\n');
        size_t line_size = line_end ? (size_t)(line_end - line_start) : strlen(line_start);
        
        char *line_copy = strndup(line_start, line_size);
        if (!line_copy) {
            normalize_cue_text_cleanup(&buf, &out, &tmp);
            return NULL;
        }

        /* Tokenize the current line by spaces, but avoid splitting inside tags */
    #define TOKEN_FAIL() do { free(tok); free(line_copy); normalize_cue_text_cleanup(&buf, &out, &tmp); return NULL; } while (0)
        int has_content = 0;  /* Track if this source line has any content */
        char *cursor = line_copy;
        while (cursor && *cursor) {
            /* Skip leading spaces */
            while (*cursor == ' ') cursor++;
            if (!*cursor) break;

            char *tok_start = cursor;
            int in_angle = 0;
            int in_brace = 0;
            while (*cursor) {
                if (!in_angle && !in_brace && *cursor == ' ') break;
                if (*cursor == '<') in_angle = 1;
                else if (*cursor == '>' && in_angle) in_angle = 0;
                else if (*cursor == '{') in_brace = 1;
                else if (*cursor == '}' && in_brace) in_brace = 0;
                cursor++;
            }

            size_t tok_len = (size_t)(cursor - tok_start);
            if (tok_len == 0) continue;
            char *tok = strndup(tok_start, tok_len);
            if (!tok) {
                free(line_copy);
                normalize_cue_text_cleanup(&buf, &out, &tmp);
                return NULL;
            }

            has_content = 1;
            int wordlen = visible_len(tok);
            int sym_line = (whole_cue_is_symbol && wordlen == 1);

            if (sym_line) {
                if (line_len > 0) {
                    if (out_len + 1 >= out_cap) {
                        size_t nc = out_cap * 2 + 16;
                        char *nt = realloc(out, nc);
                        if (!nt) {
                            TOKEN_FAIL();
                        }
                        out = nt; out_cap = nc;
                    }
                    out[out_len++] = '\n'; out[out_len] = '\0';
                    lines++;
                }
                size_t tlen = strlen(tok);
                if (out_len + tlen >= out_cap) {
                    size_t nc = out_len + tlen + 16;
                    char *nt = realloc(out, nc);
                    if (!nt) {
                        TOKEN_FAIL();
                    }
                    out = nt; out_cap = nc;
                }
                memcpy(out + out_len, tok, tlen);
                out_len += tlen; out[out_len] = '\0';
                line_len = wordlen;
            }
            else if (wordlen > 0 && line_len + wordlen + 1 > max_chars && lines < max_lines) {
                /* break line */
                if (out_len + 1 >= out_cap) {
                    size_t nc = out_cap * 2 + 16;
                    char *nt = realloc(out, nc);
                    if (!nt) {
                        TOKEN_FAIL();
                    }
                    out = nt; out_cap = nc;
                }
                out[out_len++] = '\n'; out[out_len] = '\0';
                line_len = 0; 
                lines++;

                size_t tlen = strlen(tok);
                if (out_len + tlen >= out_cap) {
                    size_t nc = out_len + tlen + 16;
                    char *nt = realloc(out, nc);
                    if (!nt) {
                        TOKEN_FAIL();
                    }
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
                        if (!nt) {
                            TOKEN_FAIL();
                        }
                        out = nt; out_cap = nc;
                    }
                    out[out_len++] = ' '; out[out_len] = '\0';
                    line_len++;
                }
                size_t tlen = strlen(tok);
                if (out_len + tlen >= out_cap) {
                    size_t nc = out_len + tlen + 16;
                    char *nt = realloc(out, nc);
                    if (!nt) {
                        TOKEN_FAIL();
                    }
                    out = nt; out_cap = nc;
                }
                memcpy(out + out_len, tok, tlen);
                out_len += tlen; out[out_len] = '\0';
                line_len += wordlen;
            }

            free(tok);
        }

#undef TOKEN_FAIL

        free(line_copy);

        /* Move to next source line (preserve the newline between lines) */
        if (line_end) {
            /* Add newline between source lines if this line had content */
            if (has_content) {
                if (out_len + 1 >= out_cap) {
                    size_t nc = out_cap * 2 + 16;
                    char *nt = realloc(out, nc);
                    if (!nt) {
                        normalize_cue_text_cleanup(&buf, &out, &tmp);
                        return NULL;
                    }
                    out = nt; out_cap = nc;
                }
                out[out_len++] = '\n'; out[out_len] = '\0';
                lines++;
                line_len = 0;
            }
            line_start = line_end + 1;
        } else {
            break;
        }
    }

    free(tmp);
    free(buf);
    return out;
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
 * Validate and sanitize UTF-8 sequence. Returns 1 if valid, 0 if invalid.
 * If invalid and sanitize is true, replaces invalid bytes with replacement char.
 */
static int validate_utf8_sequence(unsigned char *s, size_t len, int sanitize) {
    if (!s || len == 0) return 1;
    
    if ((s[0] & 0x80) == 0) {
        /* ASCII, always valid */
        return 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        /* 2-byte sequence */
        if (len < 2 || (s[1] & 0xC0) != 0x80) {
            if (sanitize && len > 0) s[0] = '?';
            return 0;
        }
        return 1;
    } else if ((s[0] & 0xF0) == 0xE0) {
        /* 3-byte sequence */
        if (len < 3 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) {
            if (sanitize && len > 0) s[0] = '?';
            return 0;
        }
        return 1;
    } else if ((s[0] & 0xF8) == 0xF0) {
        /* 4-byte sequence */
        if (len < 4 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) {
            if (sanitize && len > 0) s[0] = '?';
            return 0;
        }
        return 1;
    } else {
        /* Invalid lead byte */
        if (sanitize && len > 0) s[0] = '?';
        return 0;
    }
}

/*
 * Sanitize a UTF-8 string by replacing invalid sequences with replacement char.
 * Returns newly allocated corrected string or original if valid.
 */
static char* sanitize_utf8(const char *in, SRTParserStats *stats) {
    if (!in) return NULL;
    
    size_t in_len = strlen(in);
    unsigned char *work = (unsigned char *)malloc(in_len + 1);
    if (!work) return NULL;
    
    memcpy(work, in, in_len + 1);
    int errors_found = 0;
    
    for (size_t i = 0; i < in_len; ) {
        size_t remaining = in_len - i;
        if (!validate_utf8_sequence(work + i, remaining, 1)) {
            errors_found++;
            if (stats) stats->encoding_errors_fixed++;
        }
        
        /* Skip this codepoint */
        unsigned char c = work[i];
        if ((c & 0x80) == 0) {
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            i += 4;
        } else {
            i += 1;
        }
    }
    
    if (errors_found > 0) {
        if (stats) stats->encoding_warnings++;
        if (debug_level > 1) {
            sp_log(2, "Sanitized %d UTF-8 encoding errors in string\n", errors_found);
        }
    }
    
    char *result = (char *)work;
    return result;
}

/*
 * Parse a timestamp line with fallback support for common format errors.
 * Handles: HH:MM:SS,mmm --> HH:MM:SS,mmm (standard)
 *          MM:SS,mmm --> MM:SS,mmm (no hours)
 *          HH:MM:SS.mmm --> HH:MM:SS.mmm (dot instead of comma)
 *          HH:MM:SS,mmm => HH:MM:SS,mmm (arrow variation)
 *
 * Returns 1 if successfully parsed, 0 if malformed and unrecoverable.
 */
static int parse_srt_timestamp(const char *line, int *h1, int *m1, int *s1, int *ms1,
                                int *h2, int *m2, int *s2, int *ms2, 
                                SRTParserStats *stats) {
    if (!line || !h1 || !m1 || !s1 || !ms1 || !h2 || !m2 || !s2 || !ms2) return 0;
    
    /* Try standard format first: HH:MM:SS,mmm --> HH:MM:SS,mmm */
    if (sscanf(line, "%d:%d:%d,%d --> %d:%d:%d,%d",
               h1, m1, s1, ms1, h2, m2, s2, ms2) == 8) {
        return 1;
    }
    
    /* Try with dot instead of comma: HH:MM:SS.mmm --> HH:MM:SS.mmm */
    if (sscanf(line, "%d:%d:%d.%d --> %d:%d:%d.%d",
               h1, m1, s1, ms1, h2, m2, s2, ms2) == 8) {
        if (stats) stats->encoding_warnings++;
        if (debug_level > 0) {
            sp_log(1, "Timestamp format correction: dot changed to comma\n");
        }
        return 1;
    }
    
    /* Try without hours (MM:SS,mmm --> MM:SS,mmm) */
    if (sscanf(line, "%d:%d,%d --> %d:%d,%d", m1, s1, ms1, m2, s2, ms2) == 6) {
        *h1 = 0; *h2 = 0;
        if (stats) stats->validation_warnings++;
        if (debug_level > 0) {
            sp_log(1, "Timestamp format correction: missing hours (assumed 0)\n");
        }
        return 1;
    }
    
    /* Try without hours with dot: MM:SS.mmm --> MM:SS.mmm */
    if (sscanf(line, "%d:%d.%d --> %d:%d.%d", m1, s1, ms1, m2, s2, ms2) == 6) {
        *h1 = 0; *h2 = 0;
        if (stats) stats->encoding_warnings++;
        if (debug_level > 0) {
            sp_log(1, "Timestamp format correction: missing hours and dot format\n");
        }
        return 1;
    }
    
    /* Try arrow variations: => or -> instead of --> */
    char *arrow_variants[] = {"=>", "->"};
    for (int i = 0; i < 2; i++) {
        char fmt[128];
        snprintf(fmt, sizeof(fmt), "%%d:%%d:%%d,%%d %s %%d:%%d:%%d,%%d", arrow_variants[i]);
        if (sscanf(line, fmt, h1, m1, s1, ms1, h2, m2, s2, ms2) == 8) {
            if (stats) stats->validation_warnings++;
            if (debug_level > 0) {
                sp_log(1, "Timestamp format correction: arrow changed from '%s' to '-->'\n", arrow_variants[i]);
            }
            return 1;
        }
    }
    
    return 0;
}

/*
 * Validate cue text size (line count and visible length).
 * Returns 1 if within limits, 0 if oversized.
 * Logs warnings when limits are approached or exceeded.
 */
static int validate_cue_size(const char *text, int max_line_length, int max_line_count,
                             SRTParserStats *stats) {
    if (!text) return 1;
    
    /* Count lines and track longest line */
    int line_count = 0;
    int max_line_len = 0;
    const char *p = text;
    const char *line_start = p;
    
    while (*p) {
        if (*p == '\n') {
            int line_len = visible_len(line_start);
            if (line_len > max_line_len) max_line_len = line_len;
            line_count++;
            line_start = p + 1;
        }
        p++;
    }
    
    /* Check final line */
    if (p > line_start) {
        int line_len = visible_len(line_start);
        if (line_len > max_line_len) max_line_len = line_len;
        line_count++;
    }
    
    int size_ok = 1;
    
    /* Check line count */
    if (max_line_count > 0 && line_count > max_line_count) {
        if (stats) stats->validation_warnings++;
        if (debug_level > 0) {
            sp_log(1, "Cue exceeds max line count: %d > %d\n", line_count, max_line_count);
        }
        size_ok = 0;
    }
    
    /* Check line length */
    if (max_line_length > 0 && max_line_len > max_line_length) {
        if (stats) stats->validation_warnings++;
        if (debug_level > 0) {
            sp_log(1, "Cue line exceeds max length: %d > %d\n", max_line_len, max_line_length);
        }
        size_ok = 0;
    }
    
    return size_ok;
}

/*
 * Track seen cue IDs and detect duplicates/non-sequential patterns.
 * Returns auto-generated sequential ID if duplicate or invalid.
 */
static int process_cue_id(int cue_id, int *last_id, 
                          SRTParserStats *stats, int auto_fix) {
    if (!stats) return cue_id;
    
    /* Check for duplicate ID */
    if (cue_id == *last_id) {
        if (stats) stats->duplicate_ids_fixed++;
        if (auto_fix) {
            int new_id = *last_id + 1;
            if (debug_level > 0) {
                sp_log(1, "Duplicate cue ID %d detected, renumbered to %d\n", cue_id, new_id);
            }
            *last_id = new_id;
            return new_id;
        }
        return cue_id;
    }
    
    /* Check for non-sequential ID */
    if (cue_id > *last_id + 1) {
        int gap = cue_id - *last_id - 1;
        if (stats) stats->sequences_fixed++;
        if (auto_fix) {
            int new_id = *last_id + 1;
            if (debug_level > 0) {
                sp_log(1, "Non-sequential cue IDs detected (gap of %d), using %d instead of %d\n", gap, new_id, cue_id);
            }
            *last_id = new_id;
            return new_id;
        }
    }
    
    *last_id = cue_id;
    return cue_id;
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
 * Clean up resources allocated in normalize_tags on error.
 */
static void normalize_tags_cleanup(char **tmp) {
    if (tmp && *tmp) { free(*tmp); *tmp = NULL; }
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
            if (dyn_append(&tmp, &cap, &tmp_len, "<i>") < 0) {
                normalize_tags_cleanup(&tmp);
                return NULL;
            }
            stack[sp++] = "</i>";
            if (sp >= MAX_TAG_STACK) sp = MAX_TAG_STACK-1;
            p += 5;
        }
        else if (!strncmp(p, "{\\i0}", 5)) {
            for (int j=sp-1; j>=0; j--) {
                if (strcmp(stack[j], "</i>")==0) {
                    if (dyn_append(&tmp, &cap, &tmp_len, stack[j]) < 0) {
                        normalize_tags_cleanup(&tmp);
                        return NULL;
                    }
                    sp = j;
                    break;
                }
            }
            p += 5;
        }
        else if (!strncmp(p, "{\\b1}", 5)) {
            if (dyn_append(&tmp, &cap, &tmp_len, "<b>") < 0) {
                normalize_tags_cleanup(&tmp);
                return NULL;
            }
            stack[sp++] = "</b>";
            if (sp >= MAX_TAG_STACK) sp = MAX_TAG_STACK-1;
            p += 5;
        }
        else if (!strncmp(p, "{\\b0}", 5)) {
            for (int j=sp-1; j>=0; j--) {
                if (strcmp(stack[j], "</b>")==0) {
                    if (dyn_append(&tmp, &cap, &tmp_len, stack[j]) < 0) {
                        normalize_tags_cleanup(&tmp);
                        return NULL;
                    }
                    sp = j;
                    break;
                }
            }
            p += 5;
        }
        else if (!strncmp(p, "{\\u1}", 5)) {
            if (dyn_append(&tmp, &cap, &tmp_len, "<u>") < 0) {
                normalize_tags_cleanup(&tmp);
                return NULL;
            }
            stack[sp++] = "</u>";
            if (sp >= MAX_TAG_STACK) sp = MAX_TAG_STACK-1;
            p += 5;
        }
        else if (!strncmp(p, "{\\u0}", 5)) {
            for (int j=sp-1; j>=0; j--) {
                if (strcmp(stack[j], "</u>")==0) {
                    if (dyn_append(&tmp, &cap, &tmp_len, stack[j]) < 0) {
                        normalize_tags_cleanup(&tmp);
                        return NULL;
                    }
                    sp = j;
                    break;
                }
            }
            p += 5;
        }
        else if (!strncmp(p, "{\\c&H", 5) || !strncmp(p, "{\\1c&H", 6)) {
            for (int j=sp-1; j>=0; j--) {
                if (strncmp(stack[j], "</span>", 7)==0) {
                    if (dyn_append(&tmp, &cap, &tmp_len, stack[j]) < 0) {
                        normalize_tags_cleanup(&tmp);
                        return NULL;
                    }
                    sp = j;
                    break;
                }
            }
            char color[16];
            parse_ass_color(p, color, sizeof(color));
            char span[64];
            snprintf(span, sizeof(span), "<span foreground=\"%s\">", color);
            if (dyn_append(&tmp, &cap, &tmp_len, span) < 0) {
                normalize_tags_cleanup(&tmp);
                return NULL;
            }
            stack[sp++] = "</span>";
            if (sp >= MAX_TAG_STACK) sp = MAX_TAG_STACK-1;
            const char *q = strchr(p, '}');
            if (q) p = q+1;
            else {
                if (dyn_append(&tmp, &cap, &tmp_len, "{") < 0) {
                    normalize_tags_cleanup(&tmp);
                    return NULL;
                }
                p++;
            }
        }
        else if (!strncmp(p, "{\\fn", 4)) {
            for (int j=sp-1; j>=0; j--) {
                if (strncmp(stack[j], "</span>", 7)==0) {
                    if (dyn_append(&tmp, &cap, &tmp_len, stack[j]) < 0) {
                        normalize_tags_cleanup(&tmp);
                        return NULL;
                    }
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
                if (dyn_append(&tmp, &cap, &tmp_len, span) < 0) {
                    normalize_tags_cleanup(&tmp);
                    return NULL;
                }
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
            else { if (dyn_append(&tmp, &cap, &tmp_len, "{") < 0) {
                normalize_tags_cleanup(&tmp);
                return NULL;
            } p++; }
        }
        else {
            char one[2] = { *p++, '\0' };
            if (dyn_append(&tmp, &cap, &tmp_len, one) < 0) {
                normalize_tags_cleanup(&tmp);
                return NULL;
            }
        }
    }

    for (int j=sp-1; j>=0; j--) {
        if (dyn_append(&tmp, &cap, &tmp_len, stack[j]) < 0) {
            normalize_tags_cleanup(&tmp);
            return NULL;
        }
    }

    return tmp;
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
            /* Strip any trailing newlines/CR that might have been added during normalization */
            rstrip(norm);
    }

        (*entries_out)[n].start_ms = start;
        (*entries_out)[n].end_ms   = end;
        (*entries_out)[n].text     = norm;

        /* Correct overlaps (gap < 0), but allow zero gap (touching subtitles) */
        /* Keep current cue's end time fixed, adjust next cue's start time */
        if (n > 0 && (*entries_out)[n].start_ms < (*entries_out)[n-1].end_ms) {
            (*entries_out)[n].start_ms = (*entries_out)[n-1].end_ms;
            if ((*entries_out)[n].start_ms > (*entries_out)[n].end_ms) {
                /* never allow negative duration on current cue */
                (*entries_out)[n].end_ms = (*entries_out)[n].start_ms + 1;
            }

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
 * Enhanced parsing function that collects robustness statistics.
 * Returns number of successfully parsed cues, -1 on error.
 * stats_out is optionally filled with parse statistics if provided.
 */
int parse_srt_with_stats(const char *filename, SRTEntry **entries_out, FILE *qc,
                         const SRTParserConfig *cfg, SRTParserStats *stats_out) {
    extern int debug_level;
    
    /* Initialize stats if provided */
    if (stats_out) {
        memset(stats_out, 0, sizeof(SRTParserStats));
        stats_out->min_duration = INT64_MAX;
        stats_out->min_gap = INT64_MAX;
    }
    
    /* Use provided config or construct from globals */
    SRTParserConfig default_cfg = {0};
    if (!cfg) {
        default_cfg.use_ass = use_ass;
        default_cfg.video_w = video_w;
        default_cfg.video_h = video_h;
        default_cfg.validation_level = SRT_VALIDATE_LENIENT;
        default_cfg.max_line_length = 200;
        default_cfg.max_line_count = 5;
        default_cfg.auto_fix_duplicates = 1;
        default_cfg.auto_fix_encoding = 1;
        default_cfg.warn_on_short_duration = 1;
        default_cfg.warn_on_long_duration = 1;
        cfg = &default_cfg;
    }
    
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
    int last_cue_id = 0;
    
    *entries_out = calloc(cap, sizeof(SRTEntry));
    if (!*entries_out) {
        if (debug_level > 0) {
            sp_log(1, "Failed to allocate entries array for '%s'\n", filename);
        }
        fclose(f);
        return -1;
    }

    int is_hd = (cfg->video_w > 720 || cfg->video_h > 576);
    
    while (fgets(line, sizeof(line), f)) {
        rstrip(line);
        if (strlen(line) == 0) continue;

        /* Strip UTF-8 BOM */
        if ((unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF) {
            memmove(line, line + 3, strlen(line + 3) + 1);
        }

        int idx = 0;
        if (sscanf(line, "%d", &idx) == 1) {
            if (cfg->auto_fix_duplicates) {
                idx = process_cue_id(idx, &last_cue_id, stats_out, 1);
            }
            if (!fgets(line, sizeof(line), f)) break;
            rstrip(line);
        }

        if (stats_out) stats_out->total_cues++;
        
        int h1,m1,s1,ms1,h2,m2,s2,ms2;
        if (!parse_srt_timestamp(line, &h1, &m1, &s1, &ms1, &h2, &m2, &s2, &ms2, stats_out)) {
            if (stats_out) stats_out->skipped_cues++;
            if (debug_level > 1) {
                sp_log(2, "Malformed timestamp line (skipped): '%s'\n", line);
            }
            continue;
        }
        
        /* Validate timestamp ranges */
        if (m1 < 0 || m1 > 59 || s1 < 0 || s1 > 59 || ms1 < 0 || ms1 > 999 ||
            m2 < 0 || m2 > 59 || s2 < 0 || s2 > 59 || ms2 < 0 || ms2 > 999) {
            if (stats_out) {
                stats_out->skipped_cues++;
                stats_out->validation_warnings++;
            }
            if (debug_level > 0) {
                sp_log(1, "Invalid timestamp ranges (skipped): '%s'\n", line);
            }
            continue;
        }
        
        int64_t start = ((int64_t)h1*3600 + m1*60 + s1) * 1000 + ms1;
        int64_t end   = ((int64_t)h2*3600 + m2*60 + s2) * 1000 + ms2;
        
        if (end <= start) {
            if (stats_out) {
                stats_out->skipped_cues++;
                stats_out->validation_warnings++;
            }
            if (debug_level > 0) {
                sp_log(1, "Invalid cue timing end <= start (skipped): '%s'\n", line);
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
        
        /* Check for empty text */
        if (strlen(textbuf) == 0) {
            if (stats_out) {
                stats_out->skipped_cues++;
                stats_out->validation_warnings++;
            }
            if (debug_level > 1) {
                sp_log(2, "Empty cue text (skipped) at %lld ms\n", (long long)start);
            }
            continue;
        }

        if (n >= cap) {
            cap *= 2;
            SRTEntry *tmp = realloc(*entries_out, cap * sizeof(SRTEntry));
            if (!tmp) {
                if (debug_level > 0) {
                    sp_log(1, "realloc failed\n");
                }
                free(*entries_out);
                *entries_out = NULL;
                fclose(f);
                return -1;
            }
            *entries_out = tmp;
        }

        /* Sanitize UTF-8 if configured */
        char *textbuf_sanitized = textbuf;
        if (cfg->auto_fix_encoding) {
            char *sanitized = sanitize_utf8(textbuf, stats_out);
            if (sanitized) {
                textbuf_sanitized = sanitized;
            }
        }

        char *norm = NULL;
        if (cfg->use_ass) {
            norm = strdup(textbuf_sanitized);
            if (!norm) {
                if (stats_out) stats_out->skipped_cues++;
                free(textbuf_sanitized);
                continue;
            }
            replace_ass_h(norm);
        } else {
            norm = normalize_cue_text(textbuf_sanitized, is_hd);
            if (!norm) {
                if (stats_out) stats_out->skipped_cues++;
                free(textbuf_sanitized);
                continue;
            }
            remove_ass_h(norm);
            rstrip(norm);
        }

        if (textbuf_sanitized != textbuf) {
            free(textbuf_sanitized);
        }

        /* Validate cue text size */
        if (cfg->max_line_length > 0 || cfg->max_line_count > 0) {
            if (!validate_cue_size(norm, cfg->max_line_length, cfg->max_line_count, stats_out)) {
                /* Size validation warning logged; continue anyway */
                if (debug_level > 1) {
                    sp_log(2, "Cue size validation warning: text may not render correctly\n");
                }
            }
        }

        (*entries_out)[n].start_ms = start;
        (*entries_out)[n].end_ms   = end;
        (*entries_out)[n].text     = norm;

        /* Correct overlaps (gap < 0), but allow zero gap (touching subtitles) */
        /* Keep current cue's end time fixed, adjust next cue's start time */
        if (n > 0 && (*entries_out)[n].start_ms < (*entries_out)[n-1].end_ms) {
            (*entries_out)[n].start_ms = (*entries_out)[n-1].end_ms;
            if ((*entries_out)[n].start_ms > (*entries_out)[n].end_ms) {
                (*entries_out)[n].end_ms = (*entries_out)[n].start_ms + 1;
            }
            if (stats_out) stats_out->overlaps_corrected++;
            if (debug_level > 0) {
                sp_log(1,
                    "Overlap corrected: cue %d end=%lld, cue %d start=%lld\n",
                    (int)(n-1), (long long)(*entries_out)[n-1].end_ms,
                    (int)n, (long long)(*entries_out)[n].start_ms);
            }
        }

        /* Calculate duration and gap statistics */
        int64_t duration = (*entries_out)[n].end_ms - (*entries_out)[n].start_ms;
        if (stats_out) {
            stats_out->valid_cues++;
            if (duration < stats_out->min_duration) stats_out->min_duration = duration;
            if (duration > stats_out->max_duration) stats_out->max_duration = duration;
            
            if (n > 0) {
                int64_t gap = (*entries_out)[n].start_ms - (*entries_out)[n-1].end_ms;
                if (gap < stats_out->min_gap) stats_out->min_gap = gap;
                if (gap > stats_out->max_gap) stats_out->max_gap = gap;
            }
        }

        /* Check duration warnings */
        if (cfg->warn_on_short_duration && duration < 100) {
            if (stats_out) stats_out->timing_warnings++;
            if (debug_level > 0) {
                sp_log(1, "Cue %d has very short duration (%lld ms)\n", (int)n, (long long)duration);
            }
        }
        if (cfg->warn_on_long_duration && duration > 30000) {
            if (stats_out) stats_out->timing_warnings++;
            if (debug_level > 0) {
                sp_log(1, "Cue %d has very long duration (%lld ms)\n", (int)n, (long long)duration);
            }
        }

        int align = 2;
        const char *tag = strstr(textbuf, "{\\an");
        if (tag && strlen(tag) >= 5) {
            int code = tag[4]-'0';
            if (code >= 1 && code <= 9) align = code;
        }
        (*entries_out)[n].alignment = align;

        char *plain = strip_tags((*entries_out)[n].text);
        if (!plain) {
            if (stats_out) stats_out->skipped_cues++;
            continue;
        }
        
        SRTEntry tmp = (*entries_out)[n];
        tmp.text = plain;

        qc_check_entry(filename, (int)n, &tmp,
                       (n > 0 ? &(*entries_out)[n-1] : NULL), qc);

        if (debug_level > 1) {
            sp_log(2, "Cue %d: %lld → %lld ms (%lld ms) | text='%s'\n",
                (int)n,
                (long long)(*entries_out)[n].start_ms,
                (long long)(*entries_out)[n].end_ms,
                (long long)duration,
                plain);
        }

        free(plain);
        n++;
    }
    
    fclose(f);
    
    /* Finalize statistics */
    if (stats_out) {
        if (stats_out->valid_cues > 0) {
            /* Calculate average duration */
            int64_t total_duration = 0;
            for (size_t i = 0; i < n; i++) {
                total_duration += (*entries_out)[i].end_ms - (*entries_out)[i].start_ms;
            }
            stats_out->avg_duration = total_duration / stats_out->valid_cues;
        }
        
        if (debug_level > 0) {
            sp_log(1, "Parse complete: %d valid, %d skipped, %d corrections applied\n",
                stats_out->valid_cues, stats_out->skipped_cues,
                stats_out->duplicate_ids_fixed + stats_out->overlaps_corrected + 
                stats_out->encoding_errors_fixed + stats_out->sequences_fixed);
        }
    }
    
    return (int)n;
}

/*
 * Clean up resources allocated in srt_html_to_ass on error.
 */
static void srt_html_to_ass_cleanup(char **out) {
    if (out && *out) { free(*out); *out = NULL; }
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
        if (!strncasecmp(p,"<i>",3)) {
            if (dyn_append(&out,&cap,&out_len,"{\\i1}") < 0) {
                srt_html_to_ass_cleanup(&out);
                return NULL;
            }
            p+=3;
        }
        else if (!strncasecmp(p,"</i>",4)) {
            if (dyn_append(&out,&cap,&out_len,"{\\i0}") < 0) {
                srt_html_to_ass_cleanup(&out);
                return NULL;
            }
            p+=4;
        }
        else if (!strncasecmp(p,"<b>",3)) {
            if (dyn_append(&out,&cap,&out_len,"{\\b1}") < 0) {
                srt_html_to_ass_cleanup(&out);
                return NULL;
            }
            p+=3;
        }
        else if (!strncasecmp(p,"</b>",4)) {
            if (dyn_append(&out,&cap,&out_len,"{\\b0}") < 0) {
                srt_html_to_ass_cleanup(&out);
                return NULL;
            }
            p+=4;
        }
        else if (!strncasecmp(p,"<font color=",12)) {
            const char *q = strchr(p,'"'); if (!q) { p++; continue; }
            const char *r = strchr(q+1,'"'); if (!r) { p++; continue; }
            char color[16];
            size_t color_len = (size_t)(r - (q+1));
            if (color_len >= sizeof(color)) color_len = sizeof(color) - 1;
            memcpy(color, q+1, color_len);
            color[color_len] = 0;
            unsigned rr=255,gg=255,bb=255;
            unsigned aa=255;
            size_t clen = strlen(color);
            if (color[0]=='#' && clen==7) {
                sscanf(color+1,"%02x%02x%02x",&rr,&gg,&bb);
            } else if (color[0]=='#' && clen==9) {
                sscanf(color+1,"%02x%02x%02x%02x",&rr,&gg,&bb,&aa);
            }
            char tag[96];
            if (clen==9) {
                unsigned ass_a = 255 - (aa & 0xFF); /* ASS alpha: 00 opaque, FF transparent */
                snprintf(tag,sizeof(tag),"{\\1c&H%02X%02X%02X&\\1a&H%02X&}",rr,gg,bb,ass_a);
            } else {
                snprintf(tag,sizeof(tag),"{\\1c&H%02X%02X%02X&}",rr,gg,bb);
            }
            if (dyn_append(&out,&cap,&out_len,tag) < 0) {
                srt_html_to_ass_cleanup(&out);
                return NULL;
            }
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
            if (dyn_append(&out,&cap,&out_len,tag) < 0) {
                srt_html_to_ass_cleanup(&out);
                return NULL;
            }
            p = r+2; // skip ">"
        }
        else if (!strncasecmp(p,"</font>",7)) {
            if (dyn_append(&out,&cap,&out_len,"{\\r}") < 0) {
                srt_html_to_ass_cleanup(&out);
                return NULL;
            }
            p+=7;
        }
        else if (*p == '\n') {
            if (dyn_append(&out,&cap,&out_len,"\\N") < 0) {
                srt_html_to_ass_cleanup(&out);
                return NULL;
            }
            p++;
        }
        else if (*p == '\r') {
            p++;
        }
        else {
            char one[2] = { *p++, '\0' };
            if (dyn_append(&out,&cap,&out_len,one) < 0) {
                srt_html_to_ass_cleanup(&out);
                return NULL;
            }
        }
    }
    return out;
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

/*
 * Report parser statistics in human-readable format.
 * If stats is NULL or all stats are zero, prints nothing.
 */
void srt_report_stats(const SRTParserStats *stats, FILE *out) {
    if (!stats || !out) return;
    
    if (stats->total_cues == 0) return;
    
    fprintf(out, "\n=== SRT Parser Statistics ===\n");
    fprintf(out, "Total cues encountered:       %d\n", stats->total_cues);
    fprintf(out, "Valid cues stored:            %d\n", stats->valid_cues);
    fprintf(out, "Skipped/malformed cues:       %d\n", stats->skipped_cues);
    fprintf(out, "\n=== Corrections Applied ===\n");
    fprintf(out, "Duplicate IDs fixed:          %d\n", stats->duplicate_ids_fixed);
    fprintf(out, "Non-sequential sequences:     %d\n", stats->sequences_fixed);
    fprintf(out, "Overlaps corrected:           %d\n", stats->overlaps_corrected);
    fprintf(out, "Encoding errors fixed:        %d\n", stats->encoding_errors_fixed);
    fprintf(out, "\n=== Warnings Issued ===\n");
    fprintf(out, "Encoding warnings:            %d\n", stats->encoding_warnings);
    fprintf(out, "Timing warnings:              %d\n", stats->timing_warnings);
    fprintf(out, "Validation warnings:          %d\n", stats->validation_warnings);
    
    if (stats->valid_cues > 0) {
        fprintf(out, "\n=== Duration Statistics ===\n");
        fprintf(out, "Min duration:                 %lld ms\n", (long long)stats->min_duration);
        fprintf(out, "Max duration:                 %lld ms\n", (long long)stats->max_duration);
        fprintf(out, "Avg duration:                 %lld ms\n", (long long)stats->avg_duration);
        fprintf(out, "\n=== Gap Statistics ===\n");
        fprintf(out, "Min gap between cues:         %lld ms\n", (long long)stats->min_gap);
        fprintf(out, "Max gap between cues:         %lld ms\n", (long long)stats->max_gap);
    }
    
    fprintf(out, "\n");
}

/*
 * Analyze gaps between consecutive cues and report timing anomalies.
 * Useful for detecting missing cues, misaligned timing, or unusual patterns.
 * Flags large gaps (>5s) and reports all gap ranges.
 */
void srt_analyze_gaps(const SRTEntry *entries, int n_cues, FILE *out) {
    if (!entries || n_cues < 2 || !out) return;
    
    fprintf(out, "\n=== Gap Analysis ===\n");
    fprintf(out, "Analyzing %d cues for timing gaps and anomalies...\n\n", n_cues);
    
    int large_gaps = 0;
    int small_gaps = 0;
    int64_t total_gap = 0;
    int64_t min_gap = INT64_MAX;
    int64_t max_gap = 0;
    
    for (int i = 1; i < n_cues; i++) {
        int64_t gap = entries[i].start_ms - entries[i-1].end_ms;
        total_gap += gap;
        
        if (gap < min_gap) min_gap = gap;
        if (gap > max_gap) max_gap = gap;
        
        /* Flag unusually large gaps (potential missing cues) */
        if (gap > 5000) {
            large_gaps++;
            fprintf(out, "⚠ Large gap: %.2fs between cue %d (ends at %.2fs) and cue %d (starts at %.2fs)\n",
                gap / 1000.0, i-1, entries[i-1].end_ms / 1000.0, i, entries[i].start_ms / 1000.0);
        }
        
        /* Flag very small gaps (potential tight timing) */
        if (gap > 0 && gap < 100) {
            small_gaps++;
        }
    }
    
    int avg_gap = (n_cues > 1) ? (int)(total_gap / (n_cues - 1)) : 0;
    
    fprintf(out, "\nGap Statistics:\n");
    fprintf(out, "  Min gap:                  %lld ms\n", (long long)min_gap);
    fprintf(out, "  Max gap:                  %lld ms\n", (long long)max_gap);
    fprintf(out, "  Avg gap:                  %d ms\n", avg_gap);
    fprintf(out, "  Total gap duration:       %.2fs\n", total_gap / 1000.0);
    fprintf(out, "  Large gaps (>5s):         %d\n", large_gaps);
    fprintf(out, "  Small gaps (<100ms):      %d\n", small_gaps);
    
    if (large_gaps == 0) {
        fprintf(out, "  Status:                   ✓ No suspicious gaps detected\n");
    } else {
        fprintf(out, "  Status:                   ⚠ Check for missing cues\n");
    }
    fprintf(out, "\n");
}

/*
 * Generate a simple timing summary for all cues.
 * Useful for quick visual inspection of subtitle timing structure.
 */
void srt_print_timing_summary(const SRTEntry *entries, int n_cues, FILE *out, int max_rows) {
    if (!entries || n_cues == 0 || !out) return;
    
    if (max_rows <= 0) max_rows = 10;  /* Default to first 10 cues */
    if (max_rows > n_cues) max_rows = n_cues;
    
    fprintf(out, "\n=== Timing Summary (first %d of %d cues) ===\n", max_rows, n_cues);
    fprintf(out, "Cue#  Start       End         Duration  Gap to next\n");
    fprintf(out, "─────────────────────────────────────────────────────\n");
    
    for (int i = 0; i < max_rows; i++) {
        int64_t duration = entries[i].end_ms - entries[i].start_ms;
        int64_t gap = (i + 1 < n_cues) ? (entries[i+1].start_ms - entries[i].end_ms) : 0;
        
        fprintf(out, "%3d   %8.2fs   %8.2fs   %6lldms   %6lldms\n",
            i + 1,
            entries[i].start_ms / 1000.0,
            entries[i].end_ms / 1000.0,
            (long long)duration,
            (long long)gap);
    }
    
    if (max_rows < n_cues) {
        fprintf(out, "... (%d more cues)\n", n_cues - max_rows);
    }
    fprintf(out, "\n");
}


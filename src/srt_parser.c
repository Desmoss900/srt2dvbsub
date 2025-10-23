#define _POSIX_C_SOURCE 200809L
#include "srt_parser.h"
#include "qc.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <ctype.h>

#define MAX_TAG_STACK 32
// --- Configurable limits ---
#define MAX_LINES_SD 3
#define MAX_CHARS_SD 37
#define MAX_CHARS_HD 67

extern int use_ass;
extern int video_w;
extern int video_h;

// Count Unicode codepoints (visible characters) in a UTF-8 string
static int u8_len(const char *s) {
    int count = 0;
    unsigned char c;
    while ((c = (unsigned char)*s)) {
        if ((c & 0x80) == 0) s++;           // 1-byte ASCII
        else if ((c & 0xE0) == 0xC0) s+=2;  // 2-byte sequence
        else if ((c & 0xF0) == 0xE0) s+=3;  // 3-byte sequence
        else if ((c & 0xF8) == 0xF0) s+=4;  // 4-byte sequence
        else s++; // fallback, skip invalid
        count++;
    }
    return count;
}

// Join lines, enforce max chars per line, max lines
static char* normalize_cue_text(const char *raw, int is_hd) {
    int max_lines = MAX_LINES_SD;
    int max_chars = is_hd ? MAX_CHARS_HD : MAX_CHARS_SD;

    // Step 1: normalize whitespace and join lines
    char buf[8192]; buf[0] = 0;
    const char *p = raw;
    int last_space = 0;
    while (*p) {
        if (*p == '\n' || *p == '\r' || isspace((unsigned char)*p)) {
            if (!last_space) {
                strcat(buf, " ");
                last_space = 1;
            }
            p++;
        } else {
            size_t len = strlen(buf);
            buf[len] = *p++;
            buf[len+1] = 0;
            last_space = 0;
        }
    }

    char *out = malloc(strlen(buf) + 1);
    if (!out) return strdup(buf);
    out[0] = 0;
    int line_len = 0, lines = 1;
    char *tmp = strdup(buf);
    char *tok = strtok(tmp, " ");

    int whole_cue_is_symbol = 0;
    
    {
        char *plain = strip_tags(raw);
        int len = u8_len(plain);
        if (len == 1 && ((unsigned char)plain[0] & 0x80)) {
            whole_cue_is_symbol = 1;
        }
        free(plain);
    }

    while (tok) {
        int wordlen = u8_len(tok);

        // Only treat as standalone if cue is symbol-only
        int sym_line = (whole_cue_is_symbol && wordlen == 1);

        if (sym_line) {
            if (line_len > 0) strcat(out, "\n");
            strcat(out, tok);
            line_len = wordlen;
            lines++;
        }
        else if (line_len + wordlen + 1 > max_chars && lines < max_lines) {
            strcat(out, "\n");
            line_len = 0;
            lines++;
            strcat(out, tok);
            line_len = wordlen;
        }
        else {
            if (line_len > 0) {
                strcat(out, " ");
                line_len++;
            }
            strcat(out, tok);
            line_len += wordlen;
        }

        tok = strtok(NULL, " ");
    }
    free(tmp);
    return out;
}

static void rstrip(char *s) {
    int n = strlen(s);
    while (n>0 && (s[n-1]=='\n' || s[n-1]=='\r')) {
        s[--n] = 0;
    }
}

// Convert SSA/ASS color string (&HBBGGRR&) → "#RRGGBB"
static void parse_ass_color(const char *tag, char *out, size_t outsz) {
    unsigned int b=0,g=0,r=0;
    if (sscanf(tag, "{\\c&H%02X%02X%02X&}", &b,&g,&r) == 3 ||
        sscanf(tag, "{\\1c&H%02X%02X%02X&}", &b,&g,&r) == 3) {
        snprintf(out, outsz, "#%02X%02X%02X", r,g,b);
    } else {
        strncpy(out, "#FFFFFF", outsz);
        out[outsz-1] = 0;
    }
}

static void safe_append(char *out, size_t cap, const char *s) {
    if (strlen(out) + strlen(s) + 1 < cap) {
        strcat(out, s);
    }
}

// Replace ASS non-breaking spaces (\h) with normal spaces
static void replace_ass_h(char *text) {
    if (!text) return;
    char *p = text;
    while ((p = strstr(p, "\\h")) != NULL) {
        *p = ' ';                    // remove '\'
        memmove(p+1, p+2, strlen(p+2)+1); // shift left to drop 'h'
        p++;                         // continue after the inserted space
    }
}

// Remove ASS non-breaking spaces (\h) completely
static void remove_ass_h(char *text) {
    if (!text) return;
    char *p = text;
    while ((p = strstr(p, "\\h")) != NULL) {
        memmove(p, p + 2, strlen(p + 2) + 1); // shift left by 2 to drop "\h"
    }
}


// Replace SSA/ASS tags with Pango equivalents
static char* normalize_tags(const char *in) {
    size_t cap = strlen(in)*6 + 256;
    char *tmp = malloc(cap);
    if (!tmp) return NULL;
    tmp[0] = 0;

    const char *stack[MAX_TAG_STACK];
    int sp = 0;

    const char *p = in;
    while (*p) {
        if (!strncmp(p, "{\\i1}", 5)) {
            safe_append(tmp, cap, "<i>");
            stack[sp++] = "</i>";
            if (sp >= MAX_TAG_STACK) sp = MAX_TAG_STACK-1;
            p += 5;
        }
        else if (!strncmp(p, "{\\i0}", 5)) {
            for (int j=sp-1; j>=0; j--) {
                if (strcmp(stack[j], "</i>")==0) {
                    safe_append(tmp, cap, stack[j]);
                    sp = j;
                    break;
                }
            }
            p += 5;
        }
        else if (!strncmp(p, "{\\b1}", 5)) {
            safe_append(tmp, cap, "<b>");
            stack[sp++] = "</b>";
            if (sp >= MAX_TAG_STACK) sp = MAX_TAG_STACK-1;
            p += 5;
        }
        else if (!strncmp(p, "{\\b0}", 5)) {
            for (int j=sp-1; j>=0; j--) {
                if (strcmp(stack[j], "</b>")==0) {
                    safe_append(tmp, cap, stack[j]);
                    sp = j;
                    break;
                }
            }
            p += 5;
        }
        else if (!strncmp(p, "{\\u1}", 5)) {
            safe_append(tmp, cap, "<u>");
            stack[sp++] = "</u>";
            if (sp >= MAX_TAG_STACK) sp = MAX_TAG_STACK-1;
            p += 5;
        }
        else if (!strncmp(p, "{\\u0}", 5)) {
            for (int j=sp-1; j>=0; j--) {
                if (strcmp(stack[j], "</u>")==0) {
                    safe_append(tmp, cap, stack[j]);
                    sp = j;
                    break;
                }
            }
            p += 5;
        }
        else if (!strncmp(p, "{\\c&H", 5) || !strncmp(p, "{\\1c&H", 6)) {
            for (int j=sp-1; j>=0; j--) {
                if (strncmp(stack[j], "</span>", 7)==0) {
                    safe_append(tmp, cap, stack[j]);
                    sp = j;
                    break;
                }
            }
            char color[16];
            parse_ass_color(p, color, sizeof(color));
            char span[64];
            snprintf(span, sizeof(span), "<span foreground=\"%s\">", color);
            safe_append(tmp, cap, span);
            stack[sp++] = "</span>";
            if (sp >= MAX_TAG_STACK) sp = MAX_TAG_STACK-1;
            const char *q = strchr(p, '}');
            p = q ? q+1 : p+1;
        }
        else if (!strncmp(p, "{\\fn", 4)) {
            for (int j=sp-1; j>=0; j--) {
                if (strncmp(stack[j], "</span>", 7)==0) {
                    safe_append(tmp, cap, stack[j]);
                    sp = j;
                    break;
                }
            }
            const char *q = strchr(p, '}');
            if (q) {
                char fontname[128];
                size_t len = q - (p+3);
                if (len >= sizeof(fontname)) len = sizeof(fontname)-1;
                strncpy(fontname, p+3, len);
                fontname[len] = 0;
                char span[256];
                snprintf(span, sizeof(span), "<span font=\"%s\">", fontname);
                safe_append(tmp, cap, span);
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
            p = q ? q+1 : p+1;
        }
        else {
            size_t len = strlen(tmp);
            tmp[len] = *p++;
            tmp[len+1] = 0;
        }
    }

    for (int j=sp-1; j>=0; j--) {
        safe_append(tmp, cap, stack[j]);
    }

    // Sanitize raw text after tag normalization
    // char *san = sanitize_markup(tmp);
    // free(tmp);
    return tmp;
}

int parse_srt(const char *filename, SRTEntry **entries_out, FILE *qc) {
     extern int debug_level;
    FILE *f = fopen(filename, "r");
    if (!f) {
        if (debug_level > 0) {
            fprintf(stderr, "[srt_parser] Failed to open SRT '%s': %s\n", filename, strerror(errno));
        }
        return -1;
    }

    char line[2048];
    int cap = 128, n = 0;
    *entries_out = calloc(cap, sizeof(SRTEntry));
    if (!*entries_out) {
        if (debug_level > 0) {
            fprintf(stderr, "[srt_parser] Failed to allocate entries array for '%s'\n", filename);
        }
        fclose(f); return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        rstrip(line);
        if (strlen(line) == 0) continue;

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
        int64_t start = ((int64_t)h1*3600+m1*60+s1)*1000 + ms1;
        int64_t end   = ((int64_t)h2*3600+m2*60+s2)*1000 + ms2;

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
            *entries_out = realloc(*entries_out, cap * sizeof(SRTEntry));
        }

        // Decide SD vs HD by actual input resolution from main.c
        int is_hd = (video_w > 720 || video_h > 576);

        char *norm = NULL;
        if (use_ass) {
            norm = normalize_tags(textbuf);
            if (!norm) norm = strdup(textbuf);
            replace_ass_h(norm);
        } else {
            char *stripped = strip_tags(textbuf);
            norm = normalize_cue_text(stripped, is_hd);
            remove_ass_h(norm);
            free(stripped);
        }

        (*entries_out)[n].start_ms = start;
        (*entries_out)[n].end_ms   = end;
        (*entries_out)[n].text     = norm;

        // Force at least 50ms gap between adjacent cues
        if (n > 0 && (*entries_out)[n].start_ms <= (*entries_out)[n-1].end_ms) {
            (*entries_out)[n-1].end_ms = (*entries_out)[n].start_ms - 50;
            if ((*entries_out)[n-1].end_ms < (*entries_out)[n-1].start_ms) {
                // never allow negative duration
                (*entries_out)[n-1].end_ms = (*entries_out)[n-1].start_ms + 1;
            }
            (*entries_out)[n].start_ms = (*entries_out)[n-1].end_ms + 50;

            if (debug_level > 0) {
                fprintf(stderr,
                    "[srt_parser] Overlap HARD-corrected between cue %d and %d → new: end[%d]=%lld, start[%d]=%lld\n",
                    n-1, n,
                    n-1, (long long)(*entries_out)[n-1].end_ms,
                    n,   (long long)(*entries_out)[n].start_ms);
            }
        }

       
        if (debug_level > 1) {
            fprintf(stderr,
                "[srt_parser] Cue %d: %lld → %lld ms | text='%s'\n",
                n,
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

        // Use stripped text (no HTML/ASS tags) for QC length checks
        char *plain = strip_tags((*entries_out)[n].text);
        SRTEntry tmp = (*entries_out)[n];
        tmp.text = plain;

        qc_check_entry(filename, n, &tmp,
                    (n > 0 ? &(*entries_out)[n-1] : NULL), qc);

        if (debug_level > 1) {
            fprintf(stderr,
                "[srt_parser] Cue %d: %lld → %lld ms | raw_len=%d plain_len=%d | text='%s'\n",
                n,
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
    return n;
}

// Convert minimal HTML tags (<i>, <b>, <font color>) into ASS overrides
char* srt_html_to_ass(const char *in) {
    size_t cap = strlen(in) * 8 + 128;
    char *out = malloc(cap);
    if (!out) return NULL;
    out[0] = 0;
    const char *p = in;
    while (*p) {
        if (!strncasecmp(p,"<i>",3))      { strcat(out,"{\\i1}"); p+=3; }
        else if (!strncasecmp(p,"</i>",4)){ strcat(out,"{\\i0}"); p+=4; }
        else if (!strncasecmp(p,"<b>",3)) { strcat(out,"{\\b1}"); p+=3; }
        else if (!strncasecmp(p,"</b>",4)){ strcat(out,"{\\b0}"); p+=4; }
        else if (!strncasecmp(p,"<font color=",12)) {
            const char *q = strchr(p,'"'); if (!q) { p++; continue; }
            const char *r = strchr(q+1,'"'); if (!r) { p++; continue; }
            char color[16]; strncpy(color,q+1,r-q-1); color[r-q-1]=0;
            unsigned rr=255,gg=255,bb=255;
            if (color[0]=='#' && strlen(color)==7)
                sscanf(color+1,"%02x%02x%02x",&rr,&gg,&bb);
            char tag[64];
            snprintf(tag,sizeof(tag),"{\\c&H%02X%02X%02X&}",bb,gg,rr);
            strcat(out,tag);
            p = r+2; // skip ">
        }
        else if (!strncasecmp(p,"<font face=",11)) {
            const char *q = strchr(p,'"'); if (!q) { p++; continue; }
            const char *r = strchr(q+1,'"'); if (!r) { p++; continue; }
            char face[64]; strncpy(face,q+1,r-q-1); face[r-q-1]=0;
            char tag[128];
            snprintf(tag,sizeof(tag),"{\\fn%s}",face);
            strcat(out,tag);
            p = r+2; // skip ">
        }
        else if (!strncasecmp(p,"</font>",7)) {
            strcat(out,"{\\r}");
            p+=7;
        }
        else { size_t l=strlen(out); out[l]=*p++; out[l+1]=0; }
    }
    return out;
}

// Helper: strip ASS/HTML tags for clean length calculations
char* strip_tags(const char *in) {
    char *out = malloc(strlen(in) + 1);
    int j = 0;
    for (int i = 0; in[i]; i++) {
        if (in[i] == '{') {
            while (in[i] && in[i] != '}') i++;
        }
        else if (in[i] == '<') {
            while (in[i] && in[i] != '>') i++;
        }
        else {
            out[j++] = in[i];
        }
    }
    out[j] = 0;
    return out;
}


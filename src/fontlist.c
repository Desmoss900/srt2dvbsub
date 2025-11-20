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
*   Email: [license@chili-iptv.de]  
*   Website: [www.chili-iptv.de]
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

#include "fontlist.h"

#ifdef HAVE_FONTCONFIG
#include <fontconfig/fontconfig.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    char *base;
    char **styles;
    size_t styles_len;
    size_t styles_cap;
    char **languages;
    size_t languages_len;
    size_t languages_cap;
} FontGroup;

static int stricmp_orstrcmp(const char *a, const char *b) {
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
#if defined(_WIN32) || defined(_WIN64)
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
}

static char *dup_n(const char *s, size_t n) {
    char *out = malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static void trim(char *s) {
    if (!s) return;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
}

static void split_family(const char *family, char **base_out, char **lang_out) {
    *base_out = NULL;
    *lang_out = NULL;
    if (!family || !*family) return;

    char *dup = strdup(family);
    if (!dup) return;
    trim(dup);
    if (!*dup) { free(dup); return; }

    char *base = NULL;
    char *lang = NULL;

    if (strncasecmp(dup, "Noto Sans", 9) == 0) {
        char *after = dup + 9;
        while (*after == ' ') after++;
        size_t base_len = after - dup;
        if (base_len > 0 && dup[base_len - 1] == ' ') base_len--;
        base = dup_n(dup, base_len);
        if (*after) lang = strdup(after);
    } else if (strncasecmp(dup, "Noto Serif", 10) == 0) {
        char *after = dup + 10;
        while (*after == ' ') after++;
        size_t base_len = after - dup;
        if (base_len > 0 && dup[base_len - 1] == ' ') base_len--;
        base = dup_n(dup, base_len);
        if (*after) lang = strdup(after);
    } else if (strncasecmp(dup, "Noto ", 5) == 0) {
        base = dup_n("Noto", 4);
        char *after = dup + 5;
        while (*after == ' ') after++;
        if (*after) lang = strdup(after);
    } else {
        base = strdup(dup);
    }

    if (!base) {
        free(lang);
    }
    free(dup);
    *base_out = base;
    if (lang) {
        trim(lang);
        if (*lang)
            *lang_out = lang;
        else
            free(lang);
    }
}

static FontGroup *find_or_add_group(FontGroup **groups, size_t *len, size_t *cap, const char *base) {
    for (size_t i = 0; i < *len; ++i) {
        if (stricmp_orstrcmp((*groups)[i].base, base) == 0) return &(*groups)[i];
    }
    if (*len == *cap) {
        size_t ncap = (*cap == 0) ? 16 : (*cap * 2);
        FontGroup *tmp = realloc(*groups, ncap * sizeof(FontGroup));
        if (!tmp) return NULL;
        *groups = tmp;
        *cap = ncap;
    }
    FontGroup *grp = &(*groups)[(*len)++];
    grp->base = strdup(base);
    grp->styles = NULL; grp->styles_len = grp->styles_cap = 0;
    grp->languages = NULL; grp->languages_len = grp->languages_cap = 0;
    return grp;
}

static int add_unique(char ***arr, size_t *len, size_t *cap, const char *val) {
    if (!val || !*val) return 0;
    for (size_t i = 0; i < *len; ++i) {
        if (stricmp_orstrcmp((*arr)[i], val) == 0) return 0;
    }
    if (*len == *cap) {
        size_t ncap = (*cap == 0) ? 8 : (*cap * 2);
        char **tmp = realloc(*arr, ncap * sizeof(char*));
        if (!tmp) return -1;
        *arr = tmp;
        *cap = ncap;
    }
    (*arr)[*len] = strdup(val);
    if (!(*arr)[*len]) return -1;
    (*len)++;
    return 0;
}

static int cmp_string_ptrs(const void *a, const void *b) {
    const char * const *sa = a;
    const char * const *sb = b;
    return stricmp_orstrcmp(*sa, *sb);
}

static int cmp_groups(const void *a, const void *b) {
    const FontGroup *ga = a;
    const FontGroup *gb = b;
    return stricmp_orstrcmp(ga->base, gb->base);
}

static void print_spaces(size_t count) {
    for (size_t i = 0; i < count; ++i) putchar(' ');
}

static void print_wrapped_list(const char *label, char **items, size_t count) {
    if (!items || count == 0) return;
    const size_t max_width = 100;
    const size_t label_indent = 2;
    const size_t label_width = 9; /* length of "Languages" to align colons */
    const size_t start_col = label_indent + label_width + 2; /* spaces + label + ': ' */

    print_spaces(label_indent);
    printf("%-*s", (int)label_width, label);
    putchar(':');
    putchar(' ');
    size_t line_len = start_col;

    for (size_t i = 0; i < count; ++i) {
        const char *item = items[i] ? items[i] : "";
        size_t item_len = strlen(item);
        const char *sep = (i + 1 < count) ? ", " : "";
        size_t chunk_len = item_len + strlen(sep);

        if (line_len > start_col && line_len + chunk_len > max_width) {
            putchar('\n');
            print_spaces(start_col);
            line_len = start_col;
        }

        fputs(item, stdout);
        fputs(sep, stdout);
        line_len += chunk_len;
    }
    putchar('\n');
}

static void free_groups(FontGroup *groups, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        free(groups[i].base);
        for (size_t j = 0; j < groups[i].styles_len; ++j)
            free(groups[i].styles[j]);
        free(groups[i].styles);
        for (size_t j = 0; j < groups[i].languages_len; ++j)
            free(groups[i].languages[j]);
        free(groups[i].languages);
    }
    free(groups);
}

int fontlist_print_all(void)
{
    if (!FcInit())
    {
        fprintf(stderr, "fontlist: FcInit() failed\n");
        return 1;
    }

    FcPattern *pat = FcPatternCreate();
    if (!pat)
    {
        fprintf(stderr, "fontlist: FcPatternCreate() failed\n");
        FcFini();
        return 1;
    }

    FcObjectSet *os = FcObjectSetBuild(FC_FAMILY, FC_STYLE, FC_LANG, NULL);
    if (!os)
    {
        fprintf(stderr, "fontlist: FcObjectSetBuild() failed\n");
        FcPatternDestroy(pat);
        FcFini();
        return 1;
    }

    FcFontSet *fs = FcFontList(NULL, pat, os);
    if (!fs)
    {
        fprintf(stderr, "fontlist: FcFontList() failed\n");
        FcObjectSetDestroy(os);
        FcPatternDestroy(pat);
        FcFini();
        return 1;
    }

    FontGroup *groups = NULL;
    size_t groups_len = 0, groups_cap = 0;

    for (int i = 0; i < fs->nfont; ++i)
    {
        FcPattern *font = fs->fonts[i];
        FcChar8 *family = NULL;
        if (FcPatternGetString(font, FC_FAMILY, 0, &family) != FcResultMatch)
            continue;
        FcChar8 *style = NULL;
        if (FcPatternGetString(font, FC_STYLE, 0, &style) != FcResultMatch)
            style = (FcChar8*)"Regular";

        char *base = NULL;
        char *lang = NULL;
        split_family((const char*)family, &base, &lang);
        if (!base) {
            base = strdup((const char*)family);
            if (!base) {
                free(lang);
                continue;
            }
        }

        FontGroup *grp = find_or_add_group(&groups, &groups_len, &groups_cap, base);
        if (!grp) {
            free(base);
            free(lang);
            continue;
        }
        if (style && *style)
            add_unique(&grp->styles, &grp->styles_len, &grp->styles_cap, (const char*)style);
        if (lang && *lang)
            add_unique(&grp->languages, &grp->languages_len, &grp->languages_cap, lang);

        free(base);
        free(lang);
    }

    qsort(groups, groups_len, sizeof(FontGroup), cmp_groups);
    for (size_t i = 0; i < groups_len; ++i) {
        if (groups[i].styles_len > 1)
            qsort(groups[i].styles, groups[i].styles_len, sizeof(char*), cmp_string_ptrs);
        if (groups[i].languages_len > 1)
            qsort(groups[i].languages, groups[i].languages_len, sizeof(char*), cmp_string_ptrs);
    }

    printf("Available fonts:\n");
    for (size_t i = 0; i < groups_len; ++i) {
        printf("\nFont Family: %s\n\n", groups[i].base);
        print_wrapped_list("Styles", groups[i].styles, groups[i].styles_len);
        printf("\n");
        print_wrapped_list("Languages", groups[i].languages, groups[i].languages_len);
    }

    free_groups(groups, groups_len);
    FcFontSetDestroy(fs);
    FcObjectSetDestroy(os);
    FcPatternDestroy(pat);
    FcFini();
    return 0;
}
#endif

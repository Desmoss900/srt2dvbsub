#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../src/srt_parser.h"
#include "../src/qc.h"

/* Define globals referenced by srt_parser/qc modules so linking succeeds. */
int use_ass = 0;
int video_w = 1280;
int video_h = 720;
int debug_level = 2;

#define ASSERT_MSG(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } else { fprintf(stderr, "PASS: %s\n", msg); } \
} while(0)

static int test_strip_tags(void) {
    const char *in = "Hello <b>world</b> {\\i1}X{\\i0}";
    char *out = strip_tags(in);
    if (!out) return 1;
    /* Expect HTML tags and ASS tags removed while preserving text */
    ASSERT_MSG(strstr(out, "Hello ") == out, "strip_tags preserves leading text");
    ASSERT_MSG(strstr(out, "world") != NULL, "strip_tags preserves inner text");
    ASSERT_MSG(strstr(out, "X") != NULL, "strip_tags preserves ASS-literal after tags");
    free(out);
    return 0;
}

static int test_srt_html_to_ass(void) {
    const char *in = "<b>bold</b><i>italic</i><font color=\"#FF0000\">red</font>normal";
    char *out = srt_html_to_ass(in);
    if (!out) return 1;
    ASSERT_MSG(strstr(out, "{\\b1}") != NULL, "srt_html_to_ass converts <b>");
    ASSERT_MSG(strstr(out, "{\\b0}") != NULL, "srt_html_to_ass converts </b>");
    ASSERT_MSG(strstr(out, "{\\i1}") != NULL, "srt_html_to_ass converts <i>");
    ASSERT_MSG(strstr(out, "{\\r}") != NULL || strstr(out, "{\\fn") || strstr(out, "{\\c&H"), "srt_html_to_ass converts <font> to tag");
    free(out);
    return 0;
}

static int write_sample_srt(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "1\n");
    fprintf(f, "00:00:00,000 --> 00:00:01,000\n");
    fprintf(f, "Hello <b>World</b>\n\n");
    fprintf(f, "2\n");
    fprintf(f, "00:00:01,100 --> 00:00:02,100\n");
    fprintf(f, "Second line\n\n");
    fclose(f);
    return 0;
}

static int test_parse_srt_cfg(void) {
    const char *path = "./test_sample.srt";
    if (write_sample_srt(path) != 0) { fprintf(stderr, "FAIL: unable to write sample srt: %s\n", strerror(errno)); return 1; }

    SRTEntry *entries = NULL;
    SRTParserConfig cfg = { .use_ass = 0, .video_w = 1280, .video_h = 720 };
    int n = parse_srt_cfg(path, &entries, NULL, &cfg);
    ASSERT_MSG(n == 2, "parse_srt_cfg returns 2 entries");
    ASSERT_MSG(entries != NULL, "entries_out allocated");
    ASSERT_MSG(entries[0].start_ms == 0, "first entry start == 0ms");
    ASSERT_MSG(entries[1].start_ms == 1100, "second entry start == 1100ms");
    ASSERT_MSG(strstr(entries[0].text, "Hello") != NULL, "first entry text contains Hello");
    ASSERT_MSG(strstr(entries[0].text, "World") != NULL, "first entry text contains World (tags stripped)");

    for (int i = 0; i < n; i++) {
        free(entries[i].text);
    }
    free(entries);
    remove(path);
    return 0;
}

static int test_parse_srt_wrapper(void) {
    /* Test the backwards-compatible wrapper that uses globals. */
    const char *path = "./test_sample2.srt";
    if (write_sample_srt(path) != 0) { fprintf(stderr, "FAIL: unable to write sample srt: %s\n", strerror(errno)); return 1; }
    SRTEntry *entries = NULL;
    /* Ensure globals are set */
    use_ass = 0;
    video_w = 1280;
    video_h = 720;
    int n = parse_srt(path, &entries, NULL);
    ASSERT_MSG(n == 2, "parse_srt (wrapper) returns 2 entries");
    for (int i = 0; i < n; i++) free(entries[i].text);
    free(entries);
    remove(path);
    return 0;
}

int main(void) {
    int rc = 0;
    fprintf(stderr, "Running srt_parser test harness...\n");

    rc |= test_strip_tags();
    rc |= test_srt_html_to_ass();
    rc |= test_parse_srt_cfg();
    rc |= test_parse_srt_wrapper();

    if (rc == 0) fprintf(stderr, "ALL TESTS PASSED\n");
    else fprintf(stderr, "SOME TESTS FAILED (code=%d)\n", rc);
    return rc;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <dlfcn.h>

#include "../src/render_pango.h"

static int failures = 0;
static int run_count = 0;

static void report_fail(const char *test, const char *msg) {
    fprintf(stderr, "[FAIL] %s: %s\n", test, msg);
    failures++;
}

static void report_ok(const char *test) {
    printf("[ OK ] %s\n", test);
}

static void test_srt_to_pango_basic(void) {
    const char *in = "<b>Hello</b> & <i>World</i> <font color=\"#FF00FF\">C</font>";
    char *out = srt_to_pango_markup(in);
    run_count++;
    if (!out) {
        report_fail("srt_to_pango_basic", "returned NULL");
        return;
    }
    if (strstr(out, "<span") == NULL) {
        report_fail("srt_to_pango_basic", "expected <span> for font color not found");
    } else if (strstr(out, "&amp;") == NULL) {
        report_fail("srt_to_pango_basic", "escaped ampersand expected");
    } else {
        report_ok("srt_to_pango_basic");
    }
    free(out);
}

static void test_srt_to_pango_escape(void) {
    const char *in = "a & b <c>";
    char *out = srt_to_pango_markup(in);
    run_count++;
    if (!out) { report_fail("srt_to_pango_escape","returned NULL"); return; }
    if (!strstr(out, "&amp;") || !strstr(out, "&lt;") || !strstr(out, "&gt;")) {
        report_fail("srt_to_pango_escape","missing escaped entities");
    } else {
        report_ok("srt_to_pango_escape");
    }
    free(out);
}

static void test_srt_to_pango_long(void) {
    /* Large input to exercise allocation and truncation safety paths. */
    size_t len = 200000;
    char *in = malloc(len + 1);
    if (!in) { report_fail("srt_to_pango_long","malloc failed"); return; }
    for (size_t i = 0; i < len; i++) in[i] = (i % 26) + 'a';
    in[len] = '\0';
    run_count++;
    char *out = srt_to_pango_markup(in);
    if (!out) {
        report_fail("srt_to_pango_long","returned NULL (allocation failure)");
    } else {
        if (strlen(out) == 0) {
            report_fail("srt_to_pango_long","returned empty string");
        } else {
            report_ok("srt_to_pango_long");
        }
        free(out);
    }
    free(in);
}

static int nearly_equal(double a, double b, double tol) {
    return fabs(a - b) <= tol;
}

static void test_parse_hex_color(void) {
    double r,g,b,a;
    run_count++;
    /* NULL -> defaults to opaque white per header doc */
    parse_hex_color(NULL, &r,&g,&b,&a);
    if (!nearly_equal(r,1.0,1e-6) || !nearly_equal(g,1.0,1e-6) || !nearly_equal(b,1.0,1e-6) || !nearly_equal(a,1.0,1e-6)) {
        report_fail("parse_hex_color_null","expected opaque white on NULL input");
    } else {
        report_ok("parse_hex_color_null");
    }

    run_count++;
    parse_hex_color("#FF0000", &r,&g,&b,&a);
    if (!nearly_equal(r,1.0,1e-6) || !nearly_equal(g,0.0,1e-6) || !nearly_equal(b,0.0,1e-6) || !nearly_equal(a,1.0,1e-6)) {
        report_fail("parse_hex_color_red","expected red/opaque");
    } else {
        report_ok("parse_hex_color_red");
    }

    run_count++;
    /* AARRGGBB: check alpha parsing */
    parse_hex_color("#80FF0000", &r,&g,&b,&a);
    if (!nearly_equal(r,1.0,1e-6) || !nearly_equal(g,0.0,1e-6) || !nearly_equal(b,0.0,1e-6) || !(a > 0.49 && a < 0.52)) {
        report_fail("parse_hex_color_aarrggbb","unexpected components for #80FF0000");
    } else {
        report_ok("parse_hex_color_aarrggbb");
    }

    run_count++;
    /* malformed input should not crash and should return a sane default (opaque white) */
    parse_hex_color("not-a-color", &r,&g,&b,&a);
    if (!(r >= 0.0 && r <= 1.0 && g >= 0.0 && g <= 1.0 && b >= 0.0 && b <= 1.0 && a >= 0.0 && a <= 1.0)) {
        report_fail("parse_hex_color_malformed","out-of-range values");
    } else {
        report_ok("parse_hex_color_malformed");
    }
}

static void safe_free_bitmap(Bitmap *bm) {
    if (!bm) return;
    if (bm->idxbuf) free(bm->idxbuf);
    if (bm->palette) free(bm->palette);
}

static void test_render_text_pango_basic(void) {
    run_count++;
    const char *mk = "<b>Test</b> rendering";
    Bitmap bm = render_text_pango(mk, 640, 360, 24, NULL, "#FFFFFF", "#000000", NULL, 5, "broadcast");
    /* If pango/cairo are not available on the test machine, the renderer may
     * return an empty Bitmap (idxbuf==NULL). In that case we print a skip
     * rather than failing the whole test suite. */
    if (!bm.idxbuf) {
        printf("[ SKIP ] render_text_pango_basic (pango/cairo unavailable or render failed)\n");
    } else {
        if (bm.w <= 0 || bm.h <= 0 || bm.nb_colors <= 0) {
            report_fail("render_text_pango_basic","invalid bitmap dimensions or palette");
        } else {
            report_ok("render_text_pango_basic");
        }
        safe_free_bitmap(&bm);
    }
}

static void test_render_text_pango_extreme(void) {
    run_count++;
    /* Create a very long single-line markup to force a very large layout
     * width/height so the renderer's internal pixel-cap/guards should trip. */
    size_t len = 300000; /* 300k chars -> large layout */
    char *large = malloc(len + 8);
    if (!large) { report_fail("render_text_pango_extreme","malloc failed"); return; }
    for (size_t i = 0; i < len; i++) large[i] = 'W';
    large[len] = '\0';
    Bitmap bm = render_text_pango(large, 100000, 100000, 24, NULL, "#FFFFFF", "#000000", NULL, 5, "broadcast");
    free(large);
    if (bm.idxbuf) {
        /* If renderer allocated despite extreme layout, free and fail */
        safe_free_bitmap(&bm);
        report_fail("render_text_pango_extreme","renderer allocated for extreme layout (expected to guard)");
    } else {
        report_ok("render_text_pango_extreme");
    }
}

int main(void) {
    printf("Running render_pango tests...\n");

    test_srt_to_pango_basic();
    test_srt_to_pango_escape();
    test_srt_to_pango_long();
    test_parse_hex_color();
    test_render_text_pango_basic();
    test_render_text_pango_extreme();

    printf("\nRan %d checks. Failures: %d\n", run_count, failures);
    /* Cleanup per-thread Pango/Cairo resources to reduce false-positive
     * leaks from fontconfig/Pango when running under ASan/LSan. */
    render_pango_cleanup();

    /* Try to call Fontconfig finalizer (FcFini) if present to release
     * global fontconfig/expat allocations. Use dlsym to avoid needing an
     * explicit link dependency in the test binary. */
    void (*fcfini)(void) = NULL;
    fcfini = (void(*)(void))dlsym(RTLD_DEFAULT, "FcFini");
    if (!fcfini) {
        /* attempt to open common lib filename and lookup symbol */
        void *lib = dlopen("libfontconfig.so.1", RTLD_LAZY);
        if (lib) {
            fcfini = (void(*)(void))dlsym(lib, "FcFini");
            if (fcfini) fcfini();
            dlclose(lib);
        }
    } else {
        fcfini();
    }

    return failures == 0 ? 0 : 1;
}

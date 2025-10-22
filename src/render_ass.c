#define _POSIX_C_SOURCE 200809L
#include "render_ass.h"
#include "palette.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <cairo/cairo.h>

// Convert "#RRGGBB" or "#AARRGGBB" to ASS &HAABBGGRR format
static void hex_to_ass_color(const char *hex, char *out, size_t outsz) {
    unsigned r=255,g=255,b=255,a=0;
    if (!hex || hex[0] != '#') {
        snprintf(out, outsz, "&H00FFFFFF"); // default white
        return;
    }
    if (strlen(hex) == 7) {
        sscanf(hex+1, "%02x%02x%02x", &r,&g,&b);
        a = 0x00;
    } else if (strlen(hex) == 9) {
        sscanf(hex+1, "%02x%02x%02x%02x", &a,&r,&g,&b);
    }
    unsigned inv_a = 0xFF - a; // ASS alpha is inverted
    snprintf(out, outsz, "&H%02X%02X%02X%02X", inv_a, b, g, r);
}

// Initialize libass
ASS_Library* render_ass_init() {
    ASS_Library *lib = ass_library_init();
    if (!lib) {
        fprintf(stderr, "libass: failed to init\n");
        return NULL;
    }
    ass_set_message_cb(lib, NULL, NULL); // silence libass logs
    return lib;
}

ASS_Renderer* render_ass_renderer(ASS_Library *lib, int w, int h) {
    ASS_Renderer *r = ass_renderer_init(lib);
    if (!r) return NULL;
    ass_set_frame_size(r, w, h);
    ass_set_fonts(r, NULL, "Sans", 1, NULL, 1); // auto font fallback
    return r;
}

ASS_Track* render_ass_new_track(ASS_Library *lib) {
    ASS_Track *track = ass_new_track(lib);
    if (track) {
        // libass requires explicit track headers; start empty
        track->track_type = TRACK_TYPE_ASS;
    }
    return track;
}

void render_ass_add_event(ASS_Track *track,
                          const char *text,
                          int64_t start_ms,
                          int64_t end_ms)
{
    int ev = ass_alloc_event(track);
    if (ev < 0) return;
    track->events[ev].Start    = (int)start_ms;
    track->events[ev].Duration = (int)(end_ms - start_ms);
    track->events[ev].Style    = 0;  // index of "Default" style (first one in header)
    track->events[ev].Text     = strdup(text ? text : "");

    extern int debug_level;
    if (debug_level > 1) {
        fprintf(stderr,
            "[render_ass] Added event #%d: %lld → %lld ms | text='%s'\n",
            ev,
            (long long)start_ms,
            (long long)end_ms,
            text ? text : "(null)");
    }
}

// Convert libass frame to Bitmap
Bitmap render_ass_frame(ASS_Renderer *renderer,
                        ASS_Track *track,
                        int64_t now_ms,
                        const char *palette_mode)
{
    Bitmap bm = {0};
    int detect_change = 0;
    ASS_Image *img = ass_render_frame(renderer, track, (int)now_ms, &detect_change);

    if (!img) return bm;

    // Get bounding box
    int minx=99999, miny=99999, maxx=0, maxy=0;
    for (ASS_Image *cur = img; cur; cur = cur->next) {
        if (cur->w <= 0 || cur->h <= 0) continue;
        if (cur->dst_x < minx) minx = cur->dst_x;
        if (cur->dst_y < miny) miny = cur->dst_y;
        if (cur->dst_x+cur->w > maxx) maxx = cur->dst_x+cur->w;
        if (cur->dst_y+cur->h > maxy) maxy = cur->dst_y+cur->h;
    }
    if (minx > maxx || miny > maxy) return bm;

    int w = maxx - minx;
    int h = maxy - miny;

    bm.w = w;
    bm.h = h;
    bm.x = minx;
    bm.y = miny;
    bm.idxbuf = calloc(w*h, 1);

    // Use existing palette logic
    bm.palette = malloc(16 * sizeof(uint32_t));
    init_palette(bm.palette, palette_mode);

    for (ASS_Image *cur = img; cur; cur = cur->next) {
        // Convert libass ARGB → DVB palette index
        uint32_t argb = cur->color;
        uint8_t a = 255 - ((argb >> 24) & 0xFF); // libass alpha is inverted
        uint8_t r = (argb >> 16) & 0xFF;
        uint8_t g = (argb >> 8) & 0xFF;
        uint8_t b = (argb) & 0xFF;
        uint32_t rgba = (a << 24) | (r << 16) | (g << 8) | b;
        int palidx = nearest_palette_index(bm.palette, 16, rgba);

        for (int yy=0; yy<cur->h; yy++) {
            for (int xx=0; xx<cur->w; xx++) {
                uint8_t cov = cur->bitmap[yy*cur->stride + xx];
                if (cov > 0) {
                    int dx = cur->dst_x + xx - minx;
                    int dy = cur->dst_y + yy - miny;
                    if (dx >= 0 && dx < w && dy >= 0 && dy < h) {
                        bm.idxbuf[dy*w + dx] = palidx;
                    }
                }
            }
        }
    }

    return bm;
}

void render_ass_done(ASS_Library *lib, ASS_Renderer *renderer) {
    if (renderer) ass_renderer_done(renderer);
    if (lib) ass_library_done(lib);
}

// Backwards-compatible wrappers
void render_ass_free_track(ASS_Track *track) {
    if (!track) return;
    // Free events and text within the track
    if (track->events) {
        for (int i = 0; i < track->n_events; i++) {
            if (track->events[i].Text) free(track->events[i].Text);
        }
        free(track->events);
        track->events = NULL;
        track->n_events = 0;
    }
    // libass does not expose a public free for ASS_Track; assume caller will discard pointer
}

void render_ass_free_renderer(ASS_Renderer *renderer) {
    if (renderer) ass_renderer_done(renderer);
}

void render_ass_free_lib(ASS_Library *lib) {
    if (lib) ass_library_done(lib);
}

void render_ass_set_style(ASS_Track *track,
                          const char *font, int size,
                          const char *fg, const char *outline, const char *shadow)
{
    
    if (track) {
        track->track_type = TRACK_TYPE_ASS;
        track->n_styles = 0;   // drop built-in "Default"
    }

    char fg_ass[32], outline_ass[32], shadow_ass[32];
    hex_to_ass_color(fg, fg_ass, sizeof(fg_ass));
    hex_to_ass_color(outline, outline_ass, sizeof(outline_ass));
    hex_to_ass_color(shadow, shadow_ass, sizeof(shadow_ass));

    char header[2048];
    snprintf(header, sizeof(header),
        "[Script Info]\n"
        "ScriptType: v4.00+\n"
        "PlayResX: %d\n"
        "PlayResY: %d\n"
        "\n"
        "[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
        "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
        "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
        "Alignment, MarginL, MarginR, MarginV, Encoding\n"
        "Style: Default,%s,%d,%s,%s,%s,%s,"
        "0,0,0,0,100,100,0,0,1,0,0,2,10,10,10,1\n"
        "\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n",
        720, 576,   // default, or pass in video_w/h
        font, size, fg_ass, fg_ass, outline_ass, shadow_ass);

    ass_process_data(track, header, strlen(header));
}

void render_ass_debug_styles(ASS_Track *track) {
    if (!track) {
        fprintf(stderr, "[render_ass] No track to debug\n");
        return;
    }

    fprintf(stderr, "\n=== [render_ass] Style Debug Dump ===\n");
    fprintf(stderr, "Track has %d styles\n", track->n_styles);

    for (int i = 0; i < track->n_styles; i++) {
        ASS_Style *st = &track->styles[i];
        fprintf(stderr,
            "  [%d] Name='%s' Font='%s' Size=%f Align=%d\n"
            "       Primary=%08X Secondary=%08X Outline=%08X Back=%08X\n",
            i,
            st->Name ? st->Name : "(null)",
            st->FontName ? st->FontName : "(null)",
            st->FontSize,
            st->Alignment,
            st->PrimaryColour,
            st->SecondaryColour,
            st->OutlineColour,
            st->BackColour);
    }

    fprintf(stderr, "======================================\n");
}
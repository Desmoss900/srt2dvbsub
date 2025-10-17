#pragma once
#include <stdint.h>
#include <ass/ass.h>
#include "dvb_sub.h"   // for Bitmap struct

// Initialize the libass library + renderer
ASS_Library* render_ass_init();
ASS_Renderer* render_ass_renderer(ASS_Library *lib, int w, int h);

// Create a new libass track
ASS_Track* render_ass_new_track(ASS_Library *lib);

// Feed a subtitle event (SRT/ASS text, timing) into the track
void render_ass_add_event(ASS_Track *track,
                          const char *text,
                          int64_t start_ms,
                          int64_t end_ms);

// Render the frame at a given time (ms) into a Bitmap
Bitmap render_ass_frame(ASS_Renderer *renderer,
                        ASS_Track *track,
                        int64_t now_ms,
                        const char *palette_mode);

// Cleanup
void render_ass_done(ASS_Library *lib, ASS_Renderer *renderer);

void render_ass_set_style(ASS_Track *track,
                          const char *font, int size,
                          const char *fg, const char *outline, const char *shadow);

void render_ass_debug_styles(ASS_Track *track);                          
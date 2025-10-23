#pragma once
#include <stdint.h>
#include "dvb_sub.h"   // for Bitmap struct

/* If libass is available (configure --enable-ass) include its headers.
 * Otherwise provide opaque typedefs so callers can compile and link against
 * a stub implementation (render_ass_stub.c) without requiring libass.
 */
#ifdef HAVE_LIBASS
#include <ass/ass.h>
#else
typedef struct ASS_Library ASS_Library;
typedef struct ASS_Renderer ASS_Renderer;
typedef struct ASS_Track ASS_Track;
#endif

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

// Convenience/free wrappers matching older name expectations
void render_ass_free_track(ASS_Track *track);
void render_ass_free_renderer(ASS_Renderer *renderer);
void render_ass_free_lib(ASS_Library *lib);
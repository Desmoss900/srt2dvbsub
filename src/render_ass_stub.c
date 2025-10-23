/* Minimal stub replacements for render_ass.c when libass is not enabled
 * This file is compiled when configure is run without --enable-ass so the
 * rest of the code can call render_ass_* functions without a link error.
 */
#include <stdint.h>
#include <stdlib.h>
#include "dvb_sub.h"

/* Opaque pointer types used by callers in main.c. We don't expose libass
 * internals here; callers should guard behavior behind runtime use_ass flag.
 */
typedef void ASS_Library;
typedef void ASS_Renderer;
typedef void ASS_Track;

ASS_Library* render_ass_init() { return NULL; }
ASS_Renderer* render_ass_renderer(ASS_Library *lib, int w, int h) { (void)lib; (void)w; (void)h; return NULL; }
ASS_Track* render_ass_new_track(ASS_Library *lib) { (void)lib; return NULL; }
void render_ass_add_event(ASS_Track *track, const char *text, int64_t start_ms, int64_t end_ms) { (void)track; (void)text; (void)start_ms; (void)end_ms; }
Bitmap render_ass_frame(ASS_Renderer *renderer, ASS_Track *track, int64_t now_ms, const char *palette_mode) { (void)renderer; (void)track; (void)now_ms; (void)palette_mode; Bitmap b = {0}; return b; }
void render_ass_done(ASS_Library *lib, ASS_Renderer *renderer) { (void)lib; (void)renderer; }
void render_ass_set_style(ASS_Track *track, const char *font, int size, const char *fg, const char *outline, const char *shadow) { (void)track; (void)font; (void)size; (void)fg; (void)outline; (void)shadow; }
void render_ass_debug_styles(ASS_Track *track) { (void)track; }
void render_ass_free_track(ASS_Track *track) { (void)track; }
void render_ass_free_renderer(ASS_Renderer *renderer) { (void)renderer; }
void render_ass_free_lib(ASS_Library *lib) { (void)lib; }

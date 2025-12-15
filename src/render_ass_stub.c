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

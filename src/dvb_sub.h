/*
* Copyright (c) 2025 Mark E. Rosche, Chili IPTV Systems
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
*   Mark E. Rosche, Chili IPTV Systems
*   Email: license@chili-iptv.de
*   Website: www.chili-iptv.de
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

#ifndef DVB_SUB_H
#define DVB_SUB_H

#include <libavcodec/avcodec.h>
#include "render_pango.h"

/**
 * @file dvb_sub.h
 * @brief Convert internal Bitmaps into libav DVB subtitle structures.
 *
 * This helper translates the project's `Bitmap` representation into an
 * `AVSubtitle` and associated `AVSubtitleRect` structures suitable for
 * feeding to FFmpeg's DVB subtitle encoder. The translation copies the
 * index-plane and palette into the `AVSubtitle` data planes and sets
 * the display duration (end_display_time = end_ms - start_ms).
 *
 * Ownership / contract:
 *  - The returned `AVSubtitle*` and any internal allocations are created
 *    with libavutil allocators (e.g., av_malloc/av_mallocz). Callers must
 *    free the result using `avsubtitle_free()` and then `av_free()` to
 *    avoid leaks.
 *  - If `bm.idxbuf` or `bm.palette` are missing or the bitmap dimensions
 *    are invalid, the function returns an `AVSubtitle` with zero rects
 *    (num_rects == 0) which encoders interpret as an explicit clear.
 *
 * Example:
 * @code
 *   Bitmap bm = render_subtitle(...);
 *   AVSubtitle *sub = make_subtitle(bm, cue.start_ms, cue.end_ms);
 *   if (sub) {
 *       // send to encoder/muxer
 *       avsubtitle_free(sub);
 *       av_free(sub);
 *   }
 * @endcode
 *
 * @param bm Rendered Bitmap describing geometry, index buffer and palette.
 * @param start_ms Cue start time in milliseconds.
 * @param end_ms   Cue end time in milliseconds.
 * @return Pointer to an allocated AVSubtitle on success, or NULL on failure.
 */
AVSubtitle* make_subtitle(Bitmap bm, int64_t start_ms, int64_t end_ms);

/**
 * free_subtitle
 * 
 * @brief Frees the memory allocated for an AVSubtitle structure.
 * 
 * Convenience wrapper that releases an allocated AVSubtitle and
 * frees the structure itself. It performs the equivalent of:
 *   avsubtitle_free(*psub);
 *   av_freep(psub);
 *
 * Note: only use this for heap-allocated AVSubtitle pointers returned
 * by `make_subtitle()` (or otherwise allocated with av_malloc/av_mallocz).
 * Do NOT call this with the address of a stack-allocated AVSubtitle
 * (e.g., `AVSubtitle flush_sub; free_subtitle(&flush_sub);` would be
 * invalid). For stack-allocated objects continue to use `avsubtitle_free(&s)`.
 *
 * @param psub Pointer to a pointer to an AVSubtitle structure to be freed.
 */
void free_subtitle(AVSubtitle **psub);

#endif
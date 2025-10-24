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
*   Email: [license@chili-iptv.info]  
*   Website: [www.chili-iptv.info]
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
#pragma once
#ifndef RENDER_POOL_H
#define RENDER_POOL_H

#include <pthread.h>
#include "render_pango.h"

/*
 * @file render_pool.h
 * @brief Threaded rendering pool that asynchronously rasterizes Pango
 * markup into the project's indexed `Bitmap` format.
 *
 * The render pool maintains a small set of worker threads that process
 * submitted render jobs. Jobs can be submitted asynchronously and later
 * retrieved by (track_id, cue_index) key. For synchronous needs, a
 * convenience blocking API is provided that enqueues a job and waits for
 * completion.
 *
 * Thread-safety and ownership:
 *  - The pool copies string arguments when enqueuing, so callers may free
 *    their buffers after submission.
 *  - Returned `Bitmap` objects transfer ownership of `idxbuf` and `palette`
 *    to the caller; the caller must free them (av_free is used internally).
 */

/*
 * Initialize the render pool with `nthreads`. Pass 0 to disable the pool
 * (render_pool_render_sync will call the renderer directly).
 *
 * @param nthreads Number of worker threads to start (>0). Returns 0 on
 *                 success, -1 on failure (allocation/thread creation).
 */
int render_pool_init(int nthreads);

/*
 * Shutdown the render pool and free resources. Blocks until worker threads
 * exit and queued jobs are freed. Any pending job results are freed as
 * part of shutdown; callers should retrieve results prior to shutdown if
 * they need them.
 */
void render_pool_shutdown(void);

/*
 * Synchronously render markup using the worker pool. If the pool is
 * disabled, this calls render_text_pango() directly. Returns a Bitmap with
 * allocated idxbuf and palette which the caller must free.
 *
 * @return Bitmap (may be empty with w==0 on failure).
 */
Bitmap render_pool_render_sync(const char *markup,
                                int disp_w, int disp_h,
                                int fontsize, const char *fontfam,
                                const char *fgcolor, const char *outlinecolor,
                                const char *shadowcolor, int align_code,
                                const char *palette_mode);

/*
 * Submit an asynchronous render job keyed by (track_id, cue_index).
 * The pool copies string arguments internally. Returns 0 on success and
 * -1 if the pool is not running or on allocation failure.
 */
int render_pool_submit_async(int track_id, int cue_index,
                             const char *markup, int disp_w, int disp_h, int fontsize,
                             const char *fontfam, const char *fgcolor, const char *outlinecolor,
                             const char *shadowcolor, int align_code, const char *palette_mode);

/*
 * Try to retrieve a completed job for the given (track_id, cue_index).
 * If the job is done, returns 1 and fills `*out` with the Bitmap (ownership
 * transferred to the caller). If the job exists but is not yet done,
 * returns 0. If no job exists with that key, returns -1.
 */
int render_pool_try_get(int track_id, int cue_index, Bitmap *out);

#endif

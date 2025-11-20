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
 *
 * Locking and concurrency invariants (summary)
 * -------------------------------------------
 * The implementation exposes a few internal locking rules that callers
 * and future maintainers should follow when modifying `render_pool.c`:
 *
 *  - Global state protected by `job_mtx`:
 *      `job_head`, `job_tail`, `all_jobs`, and the queue/list structure
 *      must be accessed or modified while holding `job_mtx`.
 *      Additionally, changes to `running` that affect worker wakeup are
 *      done while holding `job_mtx`.
 *
 *  - Per-job synchronization:
 *      Each `RenderJob` has a per-job `done_mtx`/`done_cond` pair used to
 *      wait for that job's completion. Workers write the job's `result`
 *      and set the `done` flag while holding `done_mtx`, then signal
 *      `done_cond`.
 *
 *  - Lock ordering (important):
 *      To avoid deadlocks, code that needs to hold both the global and a
 *      per-job mutex must always lock `job_mtx` first and then acquire
 *      the job's `done_mtx`. Reversing this order risks deadlock with the
 *      worker threads which acquire `job_mtx` only briefly to dequeue
 *      jobs and later acquire `done_mtx` to publish results.
 *
 *  - Atomic flags:
 *      The implementation uses an atomic `pool_active` to allow safe,
 *      fast checks whether the pool is accepting work. The per-job `done`
 *      flag is implemented as an atomic integer; callers should use
 *      atomic loads/stores when reading or setting it.
 *
 *  - Ownership transfer of `Bitmap`:
 *      When returning a `Bitmap` from a job container to a caller, the
 *      implementation moves the `idxbuf`/`palette` pointers out of the
 *      job container (so the container cleanup code does not free them).
 *      Use the helper `steal_job_result()` (internal) to perform this
 *      transfer atomically with respect to cleanup.
 *
 *  - Destroying per-job cond/mutex:
 *      Cond/mutex objects are only destroyed if their initialization
 *      succeeded; the code tracks initialization success via flags in the
 *      job structure to avoid calling `pthread_cond_destroy` or
 *      `pthread_mutex_destroy` on uninitialized objects.
 *
 * Follow these invariants when modifying the pool implementation to
 * preserve correctness and avoid subtle races or UB.
 */

/*
 * AUDIT NOTE (2025-10-27):
 * The render pool API and implementation have been fully audited and
 * corrected to address shutdown and double-free races, partial thread
 * creation failures, and per-job synchronization edge cases. Tests for
 * partial pthread_create failures were added and executed under
 * AddressSanitizer/LeakSanitizer and Valgrind. No leaks or sanitizer
 * errors were observed for the exercised scenarios.
 * See: tools/rp_partial_create_test.c, tools/pthread_create_fail.c
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
                               const char *fontstyle,
                               const char *fgcolor, const char *outlinecolor,
                               const char *shadowcolor, const char *bgcolor, int align_code,
                               const char *palette_mode);

/*
 * Submit an asynchronous render job keyed by (track_id, cue_index).
 * The pool copies string arguments internally. Returns 0 on success and
 * -1 if the pool is not running or on allocation failure.
 */
int render_pool_submit_async(int track_id, int cue_index,
                            const char *markup, int disp_w, int disp_h, int fontsize,
                            const char *fontfam, const char *fontstyle,
                            const char *fgcolor, const char *outlinecolor,
                            const char *shadowcolor, const char *bgcolor, int align_code, const char *palette_mode);

/*
 * Try to retrieve a completed job for the given (track_id, cue_index).
 * If the job is done, returns 1 and fills `*out` with the Bitmap (ownership
 * transferred to the caller). If the job exists but is not yet done,
 * returns 0. If no job exists with that key, returns -1.
 */
int render_pool_try_get(int track_id, int cue_index, Bitmap *out);

#endif

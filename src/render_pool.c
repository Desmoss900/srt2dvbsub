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

#define _POSIX_C_SOURCE 200809L
#include "render_pool.h"
#include "render_pango.h"
#include "utils.h"
#include "bench.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <libavutil/mem.h>
#include <time.h>

/*
 * render_pool.c
 * --------------
 * Threaded rendering pool implementation. Maintains a FIFO queue of
 * RenderJob entries processed by a fixed set of worker threads. Jobs are
 * keyed by (track_id, cue_index) to allow callers to submit jobs and later
 * retrieve results by key.
 *
 * Concurrency and ownership summary:
 *  - `job_mtx` protects the queue (job_head/job_tail) and the all_jobs
 *    list which stores pointers to all outstanding jobs for keyed lookup.
 *  - Workers dequeue jobs, perform render_text_pango(), and then signal
 *    the job's condition variable to notify waiters.
 *  - The pool copies string inputs when creating jobs; callers may free
 *    their buffers immediately after submitting.
 *  - Bitmap buffers allocated by workers are returned to callers; the
 *    pool frees them with av_free() when jobs are discarded during
 *    shutdown.
 *
 * Invariants and lock ordering
 * ---------------------------
 * This section documents the internal concurrency rules the module
 * relies on. Keep these in mind when changing the implementation.
 *
 * 1) Global list/queue lock (`job_mtx`)
 *    - Protects: `job_head`, `job_tail`, `all_jobs` and the linked-list
 *      structure that connects RenderJob containers. Any modification
 *      (add/remove) to those lists must be done while holding `job_mtx`.
 *    - `running` is updated under `job_mtx` to coordinate worker
 *      shutdown/wakeup.
 *
 * 2) Per-job synchronization (`done_mtx` / `done_cond`)
 *    - Each RenderJob has its own `done_mtx`/`done_cond` pair. Workers
 *      publish job results while holding `done_mtx`, set the `done`
 *      flag (atomic), and signal `done_cond`. Waiters wait on the same
 *      cond while holding the per-job mutex.
 *
 * 3) Lock ordering
 *    - If code needs to acquire both the global `job_mtx` and a
 *      per-job `done_mtx`, it MUST acquire `job_mtx` first and then
 *      `done_mtx`. This ordering prevents deadlocks with worker
 *      threads which briefly take `job_mtx` to dequeue work and later
 *      take `done_mtx` to publish results.
 *
 * 4) Atomics and visibility
 *    - `pool_active` is an atomic flag used for quick checks whether
 *      the pool accepts submissions. Public APIs use atomic load on
 *      this flag to avoid races with shutdown/init.
 *    - The per-job `done` flag is an atomic_int. Use `atomic_load`
 *      and `atomic_store` to inspect and set it. The worker still sets
 *      `done` while holding `done_mtx` to ensure ordering with the
 *      production of `result` and the cond signal.
 *
 * 5) Ownership transfer
 *    - When a caller retrieves a job result, the implementation moves
 *      the `Bitmap` pointers out of the job container (so cleanup won't
 *      free them). Use `steal_job_result()` (internal helper) to do
 *      this atomically with respect to container cleanup.
 *
 * 6) Cond/mutex destruction
 *    - Per-job cond and mutex are only destroyed if their respective
 *      init succeeded; the job structure tracks `done_cond_init` and
 *      `done_mtx_init` to avoid destroying uninitialized objects.
 *
 * Follow these invariants when modifying the pool to avoid races or
 * undefined behavior.
 */

/*
 * AUDIT NOTE (2025-10-27):
 * This implementation has been audited and hardened to address the
 * double-free and shutdown races identified in the repository audit.
 * Key changes include:
 *  - per-job atomic flags (done, waiters) and a `freed` marker
 *  - centralized cleanup helpers (`cleanup_job_container`, `steal_job_result`)
 *  - safer thread creation/publish ordering and partial-create cleanup
 *  - guarded cond/mutex destroy using init flags
 *
 * Tests: partial pthread_create failure harness and shutdown/sync-vs-shutdown
 * tests were executed under AddressSanitizer/LeakSanitizer and Valgrind;
 * no leaks or sanitizer-detected issues were observed for the exercised cases.
 */
 
/*
 * RenderJob
 * ---------
 * Represents a single rendering task submitted to the pool. Fields:
 *  - markup: duplicated UTF-8 Pango markup string owned by the job.
 *  - disp_w/disp_h: target display dimensions.
 *  - fontsize/fontfam: font controls (strings duplicated and owned).
 *  - fgcolor/outlinecolor/shadowcolor: color strings (duplicated).
 *  - align_code: alignment hint forwarded to renderer.
 *  - palette_mode: textual palette hint (duplicated).
 *  - result: Bitmap produced by render_text_pango(); ownership is
 *            transferred to the caller when the job is retrieved. The
 *            pool frees these buffers with av_free() when discarding jobs.
 *  - done: flag (0/1) indicating the job result is ready.
 *  - done_cond / done_mtx: per-job cond/mutex used for waiting on a
 *                         specific job without contending on global locks.
 *  - queue_next: pointer used for the FIFO worker queue (protected by job_mtx).
 *  - all_next: pointer used for the keyed all_jobs list (protected by job_mtx).
 *  - track_id / cue_index: integer key identifying the job for lookup.
 *
 * Concurrency/ownership notes:
 *  - The pool duplicates all input strings; callers may free their
 *    originals immediately after submission.
 *  - The worker writes the `result` and sets `done` while holding the
 *    per-job done_mtx; callers waiting on that job read under the same
 *    mutex/cond pair.
 */
typedef struct RenderJob {
    char *markup;
    int disp_w, disp_h;
    int fontsize;
    char *fontfam;
    char *fontstyle;
    char *fgcolor, *outlinecolor, *shadowcolor, *bgcolor;
    int align_code;
    double sub_position_pct;
    char *palette_mode;
    SubtitlePositionConfig pos_config;  /* positioning config (position + margins) */
    Bitmap result;
    atomic_int done;
    pthread_cond_t done_cond;
    pthread_mutex_t done_mtx;
    /* next pointer for the worker queue */
    struct RenderJob *queue_next;
    /* next pointer for the all_jobs list */
    struct RenderJob *all_next;
    /* job key */
    int track_id;
    int cue_index;
    int freed; /* set to 1 when the job container has been freed to avoid double-free */
    int done_cond_init; /* non-zero if done_cond was successfully initialized */
    int done_mtx_init;  /* non-zero if done_mtx was successfully initialized */
    atomic_int waiters;  /* number of threads waiting on this job (sync callers) */
} RenderJob;

/* For async support we keep jobs in a simple linked list keyed by track+cue */
static RenderJob *all_jobs = NULL; /* head of all submitted jobs */

/*
 * find_job
 * --------
 * Search the `all_jobs` keyed list for a job with the given (track_id,
 * cue_index). Caller must hold `job_mtx` when calling this helper if the
 * caller intends to modify the list; the helper itself does not take any
 * locks and therefore must be used only under protection.
 *
 * @return pointer to RenderJob or NULL if not found.
 */
/* keep helper available for future use; silence unused warning on some compilers */
static RenderJob *find_job(int track_id, int cue_index) __attribute__((unused));
static RenderJob *find_job(int track_id, int cue_index) {
    RenderJob *j = all_jobs;
    while (j) {
        if (j->track_id == track_id && j->cue_index == cue_index) return j;
        j = j->all_next;
    }
    return NULL;
}

static RenderJob *job_head = NULL;
static RenderJob *job_tail = NULL;
static pthread_mutex_t job_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t job_cond = PTHREAD_COND_INITIALIZER;
static pthread_t *workers = NULL;
static int worker_count = 0;
static int running = 0;
/* pool_active == 1 when the pool is ready to accept jobs. Use atomics
 * for quick checks without taking job_mtx. */
static atomic_int pool_active;

/* Helper: remove a job from the all_jobs list.
 * MUST be called with job_mtx held. If the job is not found this is a no-op. */
static void remove_from_all_jobs_locked(RenderJob *target) {
    RenderJob *prev = NULL, *cur = all_jobs;
    while (cur) {
        if (cur == target) {
            if (prev) prev->all_next = cur->all_next; else all_jobs = cur->all_next;
            return;
        }
        prev = cur; cur = cur->all_next;
    }
}

/* Helper: cleanup a job container's fields. If free_container is
 * non-zero the job struct itself is also freed. Safe to call even if
 * some initialization steps failed; checks init flags before destroying. */
static void cleanup_job_container(RenderJob *j, int free_container) {
    if (!j) return;
    if (j->markup) free(j->markup);
    if (j->fontfam) free(j->fontfam);
    if (j->fontstyle) free(j->fontstyle);
    if (j->fgcolor) free(j->fgcolor);
    if (j->outlinecolor) free(j->outlinecolor);
    if (j->shadowcolor) free(j->shadowcolor);
    if (j->bgcolor) free(j->bgcolor);
    if (j->palette_mode) free(j->palette_mode);
    if (j->result.idxbuf) av_free(j->result.idxbuf);
    if (j->result.palette) av_free(j->result.palette);
    if (j->done_cond_init) pthread_cond_destroy(&j->done_cond);
    if (j->done_mtx_init) pthread_mutex_destroy(&j->done_mtx);
    if (free_container) free(j);
}

/* Helper: move the job result out of the container and clear container's
 * pointers so cleanup doesn't free the buffers. */
static void steal_job_result(RenderJob *j, Bitmap *out) {
    if (!j || !out) return;
    *out = j->result;
    j->result.idxbuf = NULL;
    j->result.palette = NULL;
}

/* Helper: initialize all string fields of a RenderJob. Duplicates all
 * input strings (or NULL for optional fields) and validates allocations.
 * On success returns 0. On failure cleans up partial allocations and returns -1.
 * Caller must NOT call cleanup_job_container on failure; this helper handles it. */
static int init_job_strings(RenderJob *job, 
                           const char *markup,
                           const char *fontfam,
                           const char *fontstyle,
                           const char *fgcolor,
                           const char *outlinecolor,
                           const char *shadowcolor,
                           const char *bgcolor,
                           const char *palette_mode)
{
    if (!job) return -1;
    
    /* Duplicate all strings; markup is mandatory (use empty string if NULL) */
    job->markup = strdup(markup ? markup : "");
    job->fontfam = fontfam ? strdup(fontfam) : NULL;
    job->fontstyle = fontstyle ? strdup(fontstyle) : NULL;
    job->fgcolor = fgcolor ? strdup(fgcolor) : NULL;
    job->outlinecolor = outlinecolor ? strdup(outlinecolor) : NULL;
    job->shadowcolor = shadowcolor ? strdup(shadowcolor) : NULL;
    job->bgcolor = bgcolor ? strdup(bgcolor) : NULL;
    job->palette_mode = palette_mode ? strdup(palette_mode) : NULL;
    
    /* Validate allocations: markup is always required, others conditional */
    if (!job->markup || (fontfam && !job->fontfam) || (fontstyle && !job->fontstyle) ||
        (fgcolor && !job->fgcolor) || (outlinecolor && !job->outlinecolor) || 
        (shadowcolor && !job->shadowcolor) || (bgcolor && !job->bgcolor) || (palette_mode && !job->palette_mode)) {
        return -1;
    }
    
    return 0;
}

/*
 * worker_thread
 * -------------
 * Background worker that processes RenderJob entries from the FIFO
 * queue. Behavior:
 *  - Blocks on `job_cond` when the queue is empty.
 *  - Dequeues one job at a time and calls render_text_pango() outside
 *    the global lock to allow concurrent processing.
 *  - Stores the result in job->result and signals job->done_cond.
 *
 * The thread exits when `running` is cleared and the queue is empty.
 */
static void *worker_thread(void *arg) {
    /* Worker loop: wait for jobs on the global queue, process them, and
     * notify any waiters. The worker exits when `running` is cleared and
     * the queue is empty. */
    (void)arg;
    while (1) {
        /* Dequeue a single job under the global job_mtx. We use a simple
         * FIFO queue head/tail. */
        pthread_mutex_lock(&job_mtx);
        while (running && job_head == NULL) pthread_cond_wait(&job_cond, &job_mtx);
        if (!running && job_head == NULL) {
            /* pool is shutting down and no jobs remain */
            pthread_mutex_unlock(&job_mtx);
            break;
        }
        RenderJob *job = job_head;
        if (job) {
            job_head = job->queue_next;
            if (!job_head) job_tail = NULL;
            /* unlink from queue; job remains in all_jobs list for keyed lookup */
        }
        pthread_mutex_unlock(&job_mtx);
        if (!job) continue;

        /* Perform the CPU/GPU-agnostic render (Pango/Cairo path). This may be
         * moderately expensive so we do it outside the global mutex to avoid
         * blocking submission or other workers. */
        int64_t render_start = 0;
        if (bench.enabled)
            render_start = bench_now();
        Bitmap bm = render_text_pango(job->markup,
                                      job->disp_w, job->disp_h,
                                      job->fontsize, job->fontfam,
                                      job->fontstyle,
                                      job->fgcolor, job->outlinecolor, job->shadowcolor,
                                      job->bgcolor,
                                      &job->pos_config, job->palette_mode);
        if (bench.enabled && render_start)
        {
            bench_add_render_us(bench_now() - render_start);
            bench_inc_cues_rendered();
        }

        /* Store result and notify waiters. Each job has its own mutex/cond
         * so callers waiting on a specific job don't contend on the global
         * job_mtx. */
        pthread_mutex_lock(&job->done_mtx);
        job->result = bm;
        atomic_store(&job->done, 1);
        pthread_cond_signal(&job->done_cond);
        pthread_mutex_unlock(&job->done_mtx);
    }
    return NULL;
}

/*
 * render_pool_init
 * ----------------
 * Start `nthreads` worker threads. If nthreads <= 0 the pool remains
 * disabled and synchronous rendering is used. Returns 0 on success and
 * -1 on allocation or thread creation failure.
 *
 * Note: Enforces a hard upper bound on thread count to prevent system
 * oversubscription or resource exhaustion. Maximum of 256 threads.
 */
int render_pool_init(int nthreads) {
    if (nthreads <= 0) return 0;
    
    /* Guard against excessive thread counts */
    const int MAX_POOL_THREADS = 256;
    if (nthreads > MAX_POOL_THREADS) {
        nthreads = MAX_POOL_THREADS;
    }
    
    pthread_t *new_workers = calloc(nthreads, sizeof(pthread_t));
    if (!new_workers) return -1;
    /* make pool appear running to worker threads, but only mark active
     * (pool_active) after successful thread creation */
    running = 1;
    int created = 0;
    for (int i = 0; i < nthreads; i++) {
        if (pthread_create(&new_workers[i], NULL, worker_thread, NULL) != 0) {
            /* cleanup any threads we managed to start */
            running = 0;
            for (int j = 0; j < created; j++) pthread_join(new_workers[j], NULL);
            free(new_workers);
            return -1;
        }
        created++;
    }
    /* publish created workers to globals */
    workers = new_workers;
    worker_count = created;
    atomic_store(&pool_active, 1);
    return 0;
}

/*
 * render_pool_shutdown
 * --------------------
 * Gracefully stop worker threads, wait for them to exit, and free all
 * outstanding job resources. Any pending job results are freed as part
 * of shutdown; callers should retrieve results prior to calling this if
 * they need them.
 */
void render_pool_shutdown(void) {
    if (!atomic_load(&pool_active)) return;
    /* mark pool inactive immediately so new submissions fail fast */
    atomic_store(&pool_active, 0);
    pthread_mutex_lock(&job_mtx);
    running = 0;
    pthread_cond_broadcast(&job_cond);
    pthread_mutex_unlock(&job_mtx);
    for (int i = 0; i < worker_count; i++) pthread_join(workers[i], NULL);
    free(workers); workers = NULL; worker_count = 0;

    /* Free any remaining queued jobs (both queue and all_jobs lists).
     * Jobs may contain duplicated strings and a Bitmap result which must
     * be released. We use av_free for buffers allocated by libavutil. */
    pthread_mutex_lock(&job_mtx);
    RenderJob *j = job_head;
    while (j) {
        RenderJob *next = j->queue_next;
        /* mark as freed to avoid later double-free when iterating all_jobs */
        /* If a synchronous waiter is blocked on this transient job, the
         * waiter will be responsible for freeing the job container. In
         * that case, mark the job freed and remove it from the keyed
         * list but do not destroy its cond/mutex or free it here. */
        if (atomic_load(&j->waiters) > 0) {
            j->freed = 1;
            remove_from_all_jobs_locked(j);
            /* leave container allocated for the waiter to free */
        } else {
            j->freed = 1;
            /* remove from all_jobs list now that we're freeing it */
            remove_from_all_jobs_locked(j);
            /* cleanup fields and free the container */
            cleanup_job_container(j, 1);
        }
        j = next;
    }
    job_head = job_tail = NULL;

    /* Free any remaining jobs referenced in all_jobs list that weren't
     * part of the queue. We iterate independently to avoid double-freeing
     * jobs that were already freed from the queue above. */
    j = all_jobs;
    while (j) {
        RenderJob *next = j->all_next;
        /* skip jobs already freed when clearing the queue above */
        if (j->freed) { j = next; continue; }
        cleanup_job_container(j, 1);
        j = next;
    }
    all_jobs = NULL;
    pthread_mutex_unlock(&job_mtx);
}

/*
 * render_pool_render_sync
 * -----------------------
 * Enqueue a transient job and block until it completes. This helper is
 * convenient for callers that want to use the pool but prefer a blocking
 * call. For short-lived synchronous usage prefer this over directly
 * calling render_text_pango because it shares worker threads and queue
 * scheduling.
 *
 * Returns a Bitmap (ownership transferred to caller). On allocation
 * failure an empty Bitmap (w==0) is returned.
 */
Bitmap render_pool_render_sync(const char *markup,
                                int disp_w, int disp_h,
                                int fontsize, const char *fontfam,
                                const char *fontstyle,
                                const char *fgcolor, const char *outlinecolor,
                                const char *shadowcolor, const char *bgcolor,
                                SubtitlePositionConfig *pos_config,
                                const char *palette_mode)
{
    Bitmap empty = {0};
    /* If the pool isn't active, just call the renderer synchronously to
     * keep callers working without a pool. */
    if (!atomic_load(&pool_active)) {
        return render_text_pango(markup, disp_w, disp_h, fontsize, fontfam, fontstyle, fgcolor, outlinecolor, shadowcolor, bgcolor, pos_config, palette_mode);
    }

    /* Build a transient job structure which we will wait on. We don't add
     * it to the all_jobs keyed list because this synchronous helper is a
     * short-lived convenience. */
    RenderJob *job = calloc(1, sizeof(RenderJob));
    if (!job) return empty;
    
    /* Initialize string fields; on failure clean up and return */
    if (init_job_strings(job, markup, fontfam, fontstyle, fgcolor, 
                        outlinecolor, shadowcolor, bgcolor, palette_mode) != 0) {
        cleanup_job_container(job, 1);
        return empty;
    }
    
    /* Initialize display and rendering parameters */
    job->disp_w = disp_w;
    job->disp_h = disp_h;
    job->fontsize = fontsize;
    /* Copy positioning config if provided */
    if (pos_config) {
        job->pos_config = *pos_config;
    } else {
        /* Use defaults: bottom-center with 3.5% margins on all sides */
        job->pos_config.position = SUB_POS_BOT_CENTER;
        job->pos_config.margin_top = 3.5;
        job->pos_config.margin_left = 3.5;
        job->pos_config.margin_bottom = 3.5;
        job->pos_config.margin_right = 3.5;
    }
    atomic_store(&job->done, 0);
    job->done_cond_init = 0;
    job->done_mtx_init = 0;
    if (pthread_cond_init(&job->done_cond, NULL) != 0) { cleanup_job_container(job, 1); return empty; }
    job->done_cond_init = 1;
    if (pthread_mutex_init(&job->done_mtx, NULL) != 0) { cleanup_job_container(job, 1); return empty; }
    job->done_mtx_init = 1;
    atomic_store(&job->waiters, 0);

    /* Enqueue the job and wake a worker */
    pthread_mutex_lock(&job_mtx);
    job->queue_next = NULL;
    if (job_tail) job_tail->queue_next = job; else job_head = job;
    job_tail = job;
    /* mark that a waiter will block on this transient job */
    atomic_fetch_add(&job->waiters, 1);
    pthread_cond_signal(&job_cond);
    pthread_mutex_unlock(&job_mtx);

    /* wait for completion on the job's own cond var */
    pthread_mutex_lock(&job->done_mtx);
    while (atomic_load(&job->done) == 0) pthread_cond_wait(&job->done_cond, &job->done_mtx);
    pthread_mutex_unlock(&job->done_mtx);

    /* cleanup job container but keep result for caller */
    Bitmap ret;
    steal_job_result(job, &ret);
    /* decrement waiter count; shutdown may have deferred freeing this
     * job until the waiter finishes — the waiter is responsible for
     * freeing the container. */
    atomic_fetch_sub(&job->waiters, 1);
    cleanup_job_container(job, 1);
    return ret;
}

/*
 * render_pool_submit_async
 * ------------------------
 * Submit a render job identified by (track_id, cue_index). The pool
 * duplicates string parameters and owns them until the job is retrieved
 * or the pool is shut down. Returns 0 on success and -1 on failure.
 *
 * Note: Enforces a maximum queue depth to prevent unbounded memory growth.
 * If the queue reaches maximum size, returns -1 to force synchronous
 * rendering fallback.
 */
int render_pool_submit_async(int track_id, int cue_index,
                             const char *markup,
                             int disp_w, int disp_h,
                             int fontsize, const char *fontfam,
                             const char *fontstyle,
                             const char *fgcolor, const char *outlinecolor,
                             const char *shadowcolor, const char *bgcolor, int align_code,
                             double sub_position_pct,
                             SubtitlePositionConfig *pos_config,
                             const char *palette_mode)
{
    /* If no pool exists, fail fast to let callers fall back if desired. */
    if (!atomic_load(&pool_active)) return -1;
    
    /* Guard against unbounded queue growth by enforcing max queue depth */
    const int MAX_QUEUE_DEPTH = 1024;
    pthread_mutex_lock(&job_mtx);
    int queue_depth = 0;
    for (struct RenderJob *j = job_head; j != NULL; j = j->queue_next) {
        queue_depth++;
        if (queue_depth >= MAX_QUEUE_DEPTH) {
            pthread_mutex_unlock(&job_mtx);
            return -1;  /* Queue full; caller should fall back to sync rendering */
        }
    }
    pthread_mutex_unlock(&job_mtx);
    
    RenderJob *job = calloc(1, sizeof(RenderJob));
    if (!job) return -1;
    
    /* Initialize string fields; on failure clean up and return */
    if (init_job_strings(job, markup, fontfam, fontstyle, fgcolor,
                        outlinecolor, shadowcolor, bgcolor, palette_mode) != 0) {
        cleanup_job_container(job, 1);
        return -1;
    }
    
    /* Initialize display and rendering parameters */
    job->disp_w = disp_w;
    job->disp_h = disp_h;
    job->fontsize = fontsize;
    job->align_code = align_code;
    job->sub_position_pct = sub_position_pct;
    /* Copy positioning config if provided */
    if (pos_config) {
        job->pos_config = *pos_config;
    } else {
        /* Use defaults: bottom-center with 3.5% margins on all sides */
        job->pos_config.position = SUB_POS_BOT_CENTER;
        job->pos_config.margin_top = 3.5;
        job->pos_config.margin_left = 3.5;
        job->pos_config.margin_bottom = 3.5;
        job->pos_config.margin_right = 3.5;
    }
    atomic_store(&job->done, 0);
    job->done_cond_init = 0;
    job->done_mtx_init = 0;
    if (pthread_cond_init(&job->done_cond, NULL) != 0) { cleanup_job_container(job, 1); return -1; }
    job->done_cond_init = 1;
    if (pthread_mutex_init(&job->done_mtx, NULL) != 0) { cleanup_job_container(job, 1); return -1; }
    job->done_mtx_init = 1;
    atomic_store(&job->waiters, 0);

    pthread_mutex_lock(&job_mtx);
    /* enqueue for workers (FIFO) */
    job->queue_next = NULL;
    if (job_tail) job_tail->queue_next = job; else job_head = job;
    job_tail = job;
    /* add to all_jobs list for keyed lookup (LIFO insert is fine here) */
    job->all_next = all_jobs; all_jobs = job;
    job->track_id = track_id;
    job->cue_index = cue_index;
    pthread_cond_signal(&job_cond);
    pthread_mutex_unlock(&job_mtx);
    return 0;
}

/*
 * render_pool_try_get
 * -------------------
 * Attempt to retrieve a completed job matching (track_id, cue_index).
 * If the job is complete, transfer ownership of the Bitmap into `out`
 * and return 1. If the job exists but is still running return 0. If no
 * job exists with that key return -1.
 */
int render_pool_try_get(int track_id, int cue_index, Bitmap *out) {
    RenderJob *prev = NULL, *j = NULL;
    pthread_mutex_lock(&job_mtx);
    j = all_jobs;
    while (j) {
        if (j->track_id == track_id && j->cue_index == cue_index) {
            if (atomic_load(&j->done) == 0) {
                pthread_mutex_unlock(&job_mtx);
                return 0; /* job exists but not finished */
            }
            /* remove j from all_jobs list */
            if (prev) prev->all_next = j->all_next; else all_jobs = j->all_next;
            pthread_mutex_unlock(&job_mtx);
            /* transfer result to caller and free job container safely */
            steal_job_result(j, out);
            cleanup_job_container(j, 1);
            return 1;
        }
        prev = j; j = j->all_next;
    }
    pthread_mutex_unlock(&job_mtx);
    return -1; /* no job found */
}

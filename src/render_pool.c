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

#define _POSIX_C_SOURCE 200809L
#include "render_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <libavutil/mem.h>

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
    char *fgcolor, *outlinecolor, *shadowcolor;
    int align_code;
    char *palette_mode;
    Bitmap result;
    int done;
    pthread_cond_t done_cond;
    pthread_mutex_t done_mtx;
    /* next pointer for the worker queue */
    struct RenderJob *queue_next;
    /* next pointer for the all_jobs list */
    struct RenderJob *all_next;
    /* job key */
    int track_id;
    int cue_index;
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

        /* Perform the CPU/GPU-agostic render (Pango/Cairo path). This may be
         * moderately expensive so we do it outside the global mutex to avoid
         * blocking submission or other workers. */
        Bitmap bm = render_text_pango(job->markup,
                                      job->disp_w, job->disp_h,
                                      job->fontsize, job->fontfam,
                                      job->fgcolor, job->outlinecolor, job->shadowcolor,
                                      job->align_code, job->palette_mode);

        /* Store result and notify waiters. Each job has its own mutex/cond
         * so callers waiting on a specific job don't contend on the global
         * job_mtx. */
        pthread_mutex_lock(&job->done_mtx);
        job->result = bm;
        job->done = 1;
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
 */
int render_pool_init(int nthreads) {
    if (nthreads <= 0) return 0;
    worker_count = nthreads;
    workers = calloc(worker_count, sizeof(pthread_t));
    if (!workers) return -1;
    running = 1;
    for (int i = 0; i < worker_count; i++) {
        if (pthread_create(&workers[i], NULL, worker_thread, NULL) != 0) {
            /* On failure stop starting new threads and return error. Caller
             * should call render_pool_shutdown() to clean up partially
             * created state if needed. */
            running = 0;
            return -1;
        }
    }
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
    if (!workers) return;
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
        if (j->markup) free(j->markup);
        if (j->fontfam) free(j->fontfam);
        if (j->fgcolor) free(j->fgcolor);
        if (j->outlinecolor) free(j->outlinecolor);
        if (j->shadowcolor) free(j->shadowcolor);
        if (j->palette_mode) free(j->palette_mode);
        if (j->result.idxbuf) av_free(j->result.idxbuf);
        if (j->result.palette) av_free(j->result.palette);
        pthread_cond_destroy(&j->done_cond);
        pthread_mutex_destroy(&j->done_mtx);
        free(j);
        j = next;
    }
    job_head = job_tail = NULL;

    /* Free any remaining jobs referenced in all_jobs list that weren't
     * part of the queue. We iterate independently to avoid double-freeing
     * jobs that were already freed from the queue above. */
    j = all_jobs;
    while (j) {
        RenderJob *next = j->all_next;
        if (j->markup) free(j->markup);
        if (j->fontfam) free(j->fontfam);
        if (j->fgcolor) free(j->fgcolor);
        if (j->outlinecolor) free(j->outlinecolor);
        if (j->shadowcolor) free(j->shadowcolor);
        if (j->palette_mode) free(j->palette_mode);
        if (j->result.idxbuf) av_free(j->result.idxbuf);
        if (j->result.palette) av_free(j->result.palette);
        pthread_cond_destroy(&j->done_cond);
        pthread_mutex_destroy(&j->done_mtx);
        free(j);
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
                                const char *fgcolor, const char *outlinecolor,
                                const char *shadowcolor, int align_code,
                                const char *palette_mode)
{
    Bitmap empty = {0};
    /* If the pool isn't active, just call the renderer synchronously to
     * keep callers working without a pool. */
    if (!workers) {
        return render_text_pango(markup, disp_w, disp_h, fontsize, fontfam, fgcolor, outlinecolor, shadowcolor, align_code, palette_mode);
    }

    /* Build a transient job structure which we will wait on. We don't add
     * it to the all_jobs keyed list because this synchronous helper is a
     * short-lived convenience. */
    RenderJob *job = calloc(1, sizeof(RenderJob));
    if (!job) return empty;
    job->markup = strdup(markup ? markup : "");
    job->disp_w = disp_w; job->disp_h = disp_h; job->fontsize = fontsize;
    job->fontfam = fontfam ? strdup(fontfam) : NULL;
    job->fgcolor = fgcolor ? strdup(fgcolor) : NULL;
    job->outlinecolor = outlinecolor ? strdup(outlinecolor) : NULL;
    job->shadowcolor = shadowcolor ? strdup(shadowcolor) : NULL;
    job->align_code = align_code;
    job->palette_mode = palette_mode ? strdup(palette_mode) : NULL;
    job->done = 0;
    pthread_cond_init(&job->done_cond, NULL);
    pthread_mutex_init(&job->done_mtx, NULL);

    /* Enqueue the job and wake a worker */
    pthread_mutex_lock(&job_mtx);
    job->queue_next = NULL;
    if (job_tail) job_tail->queue_next = job; else job_head = job;
    job_tail = job;
    pthread_cond_signal(&job_cond);
    pthread_mutex_unlock(&job_mtx);

    /* wait for completion on the job's own cond var */
    pthread_mutex_lock(&job->done_mtx);
    while (!job->done) pthread_cond_wait(&job->done_cond, &job->done_mtx);
    Bitmap bm = job->result;
    pthread_mutex_unlock(&job->done_mtx);

    /* cleanup job container but keep result for caller */
    free(job->markup);
    free(job->fontfam); free(job->fgcolor); free(job->outlinecolor); free(job->shadowcolor); free(job->palette_mode);
    pthread_cond_destroy(&job->done_cond);
    pthread_mutex_destroy(&job->done_mtx);
    free(job);
    return bm;
}

/*
 * render_pool_submit_async
 * ------------------------
 * Submit a render job identified by (track_id, cue_index). The pool
 * duplicates string parameters and owns them until the job is retrieved
 * or the pool is shut down. Returns 0 on success and -1 on failure.
 */
int render_pool_submit_async(int track_id, int cue_index,
                             const char *markup,
                             int disp_w, int disp_h,
                             int fontsize, const char *fontfam,
                             const char *fgcolor, const char *outlinecolor,
                             const char *shadowcolor, int align_code,
                             const char *palette_mode)
{
    /* If no pool exists, fail fast to let callers fall back if desired. */
    if (!workers) return -1;
    RenderJob *job = calloc(1, sizeof(RenderJob));
    if (!job) return -1;
    /* Duplicate input strings so the pool owns them */
    job->markup = strdup(markup ? markup : "");
    job->disp_w = disp_w; job->disp_h = disp_h; job->fontsize = fontsize;
    job->fontfam = fontfam ? strdup(fontfam) : NULL;
    job->fgcolor = fgcolor ? strdup(fgcolor) : NULL;
    job->outlinecolor = outlinecolor ? strdup(outlinecolor) : NULL;
    job->shadowcolor = shadowcolor ? strdup(shadowcolor) : NULL;
    job->align_code = align_code;
    job->palette_mode = palette_mode ? strdup(palette_mode) : NULL;
    job->done = 0;
    pthread_cond_init(&job->done_cond, NULL);
    pthread_mutex_init(&job->done_mtx, NULL);

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
            if (!j->done) {
                pthread_mutex_unlock(&job_mtx);
                return 0; /* job exists but not finished */
            }
            /* remove j from all_jobs list */
            if (prev) prev->all_next = j->all_next; else all_jobs = j->all_next;
            pthread_mutex_unlock(&job_mtx);
            /* transfer result to caller and free job container */
            *out = j->result;
            free(j->markup);
            free(j->fontfam); free(j->fgcolor); free(j->outlinecolor); free(j->shadowcolor); free(j->palette_mode);
            pthread_cond_destroy(&j->done_cond);
            pthread_mutex_destroy(&j->done_mtx);
            free(j);
            return 1;
        }
        prev = j; j = j->all_next;
    }
    pthread_mutex_unlock(&job_mtx);
    return -1; /* no job found */
}

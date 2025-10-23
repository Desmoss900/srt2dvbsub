#include "render_pool.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <libavutil/mem.h>

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

/* Helper to find a job by track/cue */
/* find a job by track_id/cue_index (returns pointer or NULL) */
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

static void *worker_thread(void *arg) {
    (void)arg;
    while (1) {
        pthread_mutex_lock(&job_mtx);
        while (running && job_head == NULL) pthread_cond_wait(&job_cond, &job_mtx);
        if (!running && job_head == NULL) {
            pthread_mutex_unlock(&job_mtx);
            break;
        }
        RenderJob *job = job_head;
        if (job) {
            job_head = job->queue_next;
            if (!job_head) job_tail = NULL;
        }
        pthread_mutex_unlock(&job_mtx);
        if (!job) continue;

        /* perform render */
        Bitmap bm = render_text_pango(job->markup,
                                      job->disp_w, job->disp_h,
                                      job->fontsize, job->fontfam,
                                      job->fgcolor, job->outlinecolor, job->shadowcolor,
                                      job->align_code, job->palette_mode);

        pthread_mutex_lock(&job->done_mtx);
        job->result = bm;
        job->done = 1;
        pthread_cond_signal(&job->done_cond);
        pthread_mutex_unlock(&job->done_mtx);
    }
    return NULL;
}

int render_pool_init(int nthreads) {
    if (nthreads <= 0) return 0;
    worker_count = nthreads;
    workers = calloc(worker_count, sizeof(pthread_t));
    if (!workers) return -1;
    running = 1;
    for (int i = 0; i < worker_count; i++) {
        if (pthread_create(&workers[i], NULL, worker_thread, NULL) != 0) {
            running = 0;
            return -1;
        }
    }
    return 0;
}

void render_pool_shutdown(void) {
    if (!workers) return;
    pthread_mutex_lock(&job_mtx);
    running = 0;
    pthread_cond_broadcast(&job_cond);
    pthread_mutex_unlock(&job_mtx);
    for (int i = 0; i < worker_count; i++) pthread_join(workers[i], NULL);
    free(workers); workers = NULL; worker_count = 0;

    /* Free any remaining queued jobs (both queue and all_jobs lists).
     * Jobs may contain allocated markup/font strings and a Bitmap result
     * (idxbuf and palette) which must be released. Use av_free for the
     * Bitmap buffers to match callers that free them with av_free. */
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
        /* free any rendered Bitmap buffers */
        if (j->result.idxbuf) av_free(j->result.idxbuf);
        if (j->result.palette) av_free(j->result.palette);
        pthread_cond_destroy(&j->done_cond);
        pthread_mutex_destroy(&j->done_mtx);
        free(j);
        j = next;
    }
    job_head = job_tail = NULL;

    /* Free any remaining jobs referenced in all_jobs list that weren't
     * part of the queue (avoid double-free by checking pointers). */
    j = all_jobs;
    while (j) {
        RenderJob *next = j->all_next;
        /* If job is still present in all_jobs but not in queue we must free it */
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

Bitmap render_pool_render_sync(const char *markup,
                                int disp_w, int disp_h,
                                int fontsize, const char *fontfam,
                                const char *fgcolor, const char *outlinecolor,
                                const char *shadowcolor, int align_code,
                                const char *palette_mode)
{
    Bitmap empty = {0};
    if (!workers) {
        /* pool disabled, call directly */
        return render_text_pango(markup, disp_w, disp_h, fontsize, fontfam, fgcolor, outlinecolor, shadowcolor, align_code, palette_mode);
    }

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

    pthread_mutex_lock(&job_mtx);
    job->queue_next = NULL;
    if (job_tail) job_tail->queue_next = job; else job_head = job;
    job_tail = job;
    pthread_cond_signal(&job_cond);
    pthread_mutex_unlock(&job_mtx);

    /* wait for completion */
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

int render_pool_submit_async(int track_id, int cue_index,
                             const char *markup,
                             int disp_w, int disp_h,
                             int fontsize, const char *fontfam,
                             const char *fgcolor, const char *outlinecolor,
                             const char *shadowcolor, int align_code,
                             const char *palette_mode)
{
    if (!workers) return -1;
    RenderJob *job = calloc(1, sizeof(RenderJob));
    if (!job) return -1;
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
    /* enqueue for workers */
    job->queue_next = NULL;
    if (job_tail) job_tail->queue_next = job; else job_head = job;
    job_tail = job;
    /* add to all_jobs list for keyed lookup */
    job->all_next = all_jobs; all_jobs = job;
    job->track_id = track_id;
    job->cue_index = cue_index;
    pthread_cond_signal(&job_cond);
    pthread_mutex_unlock(&job_mtx);
    return 0;
}

int render_pool_try_get(int track_id, int cue_index, Bitmap *out) {
    RenderJob *prev = NULL, *j = NULL;
    pthread_mutex_lock(&job_mtx);
    j = all_jobs;
    while (j) {
        if (j->track_id == track_id && j->cue_index == cue_index) {
            if (!j->done) {
                pthread_mutex_unlock(&job_mtx);
                return 0;
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
    return -1;
}

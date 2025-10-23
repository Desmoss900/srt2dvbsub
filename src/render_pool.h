#include <pthread.h>
#include "render_pango.h"

/* Initialize the render pool with nthreads (0 = disabled) */
int render_pool_init(int nthreads);
/* Shutdown and free resources */
void render_pool_shutdown(void);
/* Synchronously render: workers will perform render_text_pango and this call returns the Bitmap (caller frees bm.idxbuf and bm.palette) */
Bitmap render_pool_render_sync(const char *markup,
                                int disp_w, int disp_h,
                                int fontsize, const char *fontfam,
                                const char *fgcolor, const char *outlinecolor,
                                const char *shadowcolor, int align_code,
                                const char *palette_mode);

/* Asynchronous API: submit a render job for track/ cue index. Returns 0 on success. Job will be processed by workers.
 * The job's markup is copied by the function. If pool isn't running, return -1. */
/* Submit an asynchronous render job keyed by track_id and cue_index.
 * The markup and string args are copied by the pool; caller can free theirs after this returns.
 */
int render_pool_submit_async(int track_id, int cue_index,
                             const char *markup, int disp_w, int disp_h, int fontsize,
                             const char *fontfam, const char *fgcolor, const char *outlinecolor,
                             const char *shadowcolor, int align_code, const char *palette_mode);


/* Try to retrieve a completed render for track_id, cue_index. If found, fills out Bitmap and returns 1. If not ready, returns 0. On error, returns -1.
 * Caller takes ownership of Bitmap idxbuf/palette and must free them. */
/* Try to obtain a completed job for the given track_id/cue_index.
 * If the job is done, returns 1 and fills *out with the Bitmap (ownership transferred to caller).
 * If the job exists but is not done, returns 0. If no job exists for that key, returns -1.
 */
int render_pool_try_get(int track_id, int cue_index, Bitmap *out);

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

/* pool_alloc.c
 *
 * Simple thread-safe pool allocator. Buckets are keyed by exact size.
 * Each bucket holds up to MAX_PER_BUCKET cached buffers. Buffers are
 * zeroed before being returned to callers to preserve previous semantics.
 */
#define _POSIX_C_SOURCE 200809L
#include "pool_alloc.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <libavutil/mem.h>

/* Maximum cached entries per size bucket */
#define MAX_PER_BUCKET 32

/*
 * Represents a memory bucket used for pooling allocations of buffers of a specific size.
 * - size: The size of each buffer managed by this bucket.
 * - head: Pointer to the first buffer in the singly-linked list of cached buffers.
 *         The next buffer pointer is stored at the start of each buffer.
 * - count: Number of buffers currently cached in this bucket.
 * - next: Pointer to the next bucket in the list.
 */
typedef struct Bucket {
    size_t size;
    void *head; /* singly-linked list of cached buffers; next pointer stored at buffer start */
    int count;
    struct Bucket *next;
} Bucket;

static Bucket *buckets = NULL;
static pthread_mutex_t buckets_lock = PTHREAD_MUTEX_INITIALIZER;


/*
 * Searches the linked list of buckets for a bucket with the specified size.
 * Returns a pointer to the matching Bucket if found, or NULL if no such bucket exists.
 * This function does not perform any locking and should only be called when thread safety is ensured externally.
 *
 * Parameters:
 *   size - The size of the bucket to search for.
 *
 * Returns:
 *   Pointer to the Bucket with the matching size, or NULL if not found.
 */
static Bucket *find_bucket_unlocked(size_t size) {
    Bucket *b = buckets;
    while (b) {
        if (b->size == size) return b;
        b = b->next;
    }
    return NULL;
}

/*
 * Allocates a memory block of the specified size from a pool.
 * If a suitable cached buffer is available in the pool, it is reused,
 * zeroed out, and returned. Otherwise, a new zero-initialized buffer
 * is allocated.
 *
 * Parameters:
 *   size - The size of the memory block to allocate (in bytes).
 *
 * Returns:
 *   Pointer to the allocated memory block, or NULL if size is zero or
 *   allocation fails.
 *
 * Thread Safety:
 *   This function is thread-safe; it uses a mutex to protect access
 *   to the pool of cached buffers.
 */
void *pool_alloc(size_t size) {
    if (size == 0) return NULL;
    void *ptr = NULL;

    pthread_mutex_lock(&buckets_lock);
    Bucket *b = find_bucket_unlocked(size);
    if (b && b->head) {
        /* pop head */
        void *node = b->head;
        void *next = *((void **)node);
        b->head = next;
        b->count--;
        pthread_mutex_unlock(&buckets_lock);
        /* zero before returning */
        memset(node, 0, size);
        return node;
    }
    pthread_mutex_unlock(&buckets_lock);

    /* no cached buffer, allocate new one */
    ptr = av_mallocz(size);
    return ptr;
}

/*
 * Frees a memory block back to a pool allocator.
 *
 * If the pointer is NULL or the size is zero, the function returns immediately.
 * The function locates or creates a bucket corresponding to the given size.
 * If the bucket has space, the pointer is added to the bucket's freelist for reuse.
 * If the bucket is full, the memory is released using av_free().
 * Thread safety is ensured via a mutex protecting the bucket list.
 *
 * Parameters:
 *   ptr  - Pointer to the memory block to be freed.
 *   size - Size of the memory block.
 */
void pool_free(void *ptr, size_t size) {
    if (!ptr || size == 0) return;

    pthread_mutex_lock(&buckets_lock);
    Bucket *b = find_bucket_unlocked(size);
    if (!b) {
        /* create new bucket */
        b = av_mallocz(sizeof(Bucket));
        if (!b) {
            pthread_mutex_unlock(&buckets_lock);
            av_free(ptr);
            return;
        }
        b->size = size;
        b->head = NULL;
        b->count = 0;
        b->next = buckets;
        buckets = b;
    }

    if (b->count < MAX_PER_BUCKET) {
        /* push into freelist; store previous head at buffer start */
        *((void **)ptr) = b->head;
        b->head = ptr;
        b->count++;
        pthread_mutex_unlock(&buckets_lock);
        return;
    }

    /* bucket full: free buffer */
    pthread_mutex_unlock(&buckets_lock);
    av_free(ptr);
}

/*
 * Destroys the memory pool by freeing all cached buffers and buckets.
 * This function acquires the buckets_lock mutex to ensure thread safety,
 * iterates through all buckets and their cached buffers, and releases
 * all allocated memory. After cleanup, the global buckets pointer is set to NULL.
 */
void pool_destroy(void) {
    pthread_mutex_lock(&buckets_lock);
    Bucket *b = buckets;
    while (b) {
        /* free cached buffers */
        void *node = b->head;
        while (node) {
            void *next = *((void **)node);
            av_free(node);
            node = next;
        }
        Bucket *n = b->next;
        av_free(b);
        b = n;
    }
    buckets = NULL;
    pthread_mutex_unlock(&buckets_lock);
}

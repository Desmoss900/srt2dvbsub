/*
 * Threaded stress test for LOG() and pool_alloc/pool_free.
 * Spawns multiple threads that concurrently allocate/free buffers via
 * pool_alloc/pool_free and emit LOG messages. Intended to validate
 * thread-safety of the pool and logging infrastructure.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>

#define DEBUG_MODULE "stress"
#include "../src/debug.h"
#include "../src/pool_alloc.h"

int debug_level = 2; /* enable debug-level logs for test */

typedef struct {
    int id;
    unsigned int seed;
} WorkerArg;

void *worker(void *argp) {
    WorkerArg *a = (WorkerArg*)argp;
    for (int i = 0; i < 10000; i++) {
        /* pick a size among a small set */
        size_t sizes[] = {64, 256, 1024, 4096};
        int idx = rand_r(&a->seed) % (sizeof(sizes)/sizeof(sizes[0]));
        size_t sz = sizes[idx];
        void *p = pool_alloc(sz);
        if (!p) {
            LOG(1, "worker %d: pool_alloc(%zu) returned NULL\n", a->id, sz);
        } else {
            /* touch memory to ensure it is accessible */
            ((uint8_t*)p)[0] = (uint8_t)a->id;
            ((uint8_t*)p)[sz-1] = (uint8_t)a->id;
            LOG(3, "worker %d: alloc %zu -> %p\n", a->id, sz, p);
            /* small random delay using nanosleep (portable, no implicit-decl warnings) */
            if ((rand_r(&a->seed) & 0x7) == 0) {
                struct timespec ts = {0, 10 * 1000}; /* 10 microseconds */
                nanosleep(&ts, NULL);
            }
            pool_free(p, sz);
        }
        if ((i & 0x3ff) == 0) {
            LOG(2, "worker %d: iteration %d\n", a->id, i);
        }
    }
    return NULL;
}

int main(void) {
    const int nthreads = 8;
    pthread_t th[nthreads];
    WorkerArg args[nthreads];

    for (int i = 0; i < nthreads; i++) {
        args[i].id = i;
        args[i].seed = (unsigned int)(time(NULL) ^ (i * 0x9e3779b9));
        if (pthread_create(&th[i], NULL, worker, &args[i]) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    for (int i = 0; i < nthreads; i++) pthread_join(th[i], NULL);

    LOG(1, "All workers done, destroying pool...\n");
    pool_destroy();
    LOG(1, "Pool destroyed, exiting.\n");
    return 0;
}

#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

static atomic_int call_count = 0;

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
    static int (*real_pthread_create)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *) = NULL;
    if (!real_pthread_create) {
        real_pthread_create = dlsym(RTLD_NEXT, "pthread_create");
        if (!real_pthread_create) {
            fprintf(stderr, "[shim] pthread_create: dlsym failed\n");
            return EAGAIN;
        }
    }
    const char *env = getenv("PTHREAD_CREATE_FAIL_FIRST");
    int fail_first = env ? atoi(env) : 0;
    int n = atomic_fetch_add(&call_count, 1);
    int call_no = n + 1;
    /* Log each invocation so tests can see which calls occur */
    fprintf(stderr, "[shim] pthread_create called #%d start_routine=%p arg=%p fail_first=%d\n",
            call_no, (void *)start_routine, arg, fail_first);
    if (n < fail_first) {
        /* simulate transient failure */
        fprintf(stderr, "[shim] simulating failure for call #%d\n", call_no);
        return EAGAIN;
    }
    int ret = real_pthread_create(thread, attr, start_routine, arg);
    fprintf(stderr, "[shim] real pthread_create returned %d for call #%d\n", ret, call_no);
    return ret;
}

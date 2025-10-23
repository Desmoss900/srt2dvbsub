#define _POSIX_C_SOURCE 200809L
#include "cpu_count.h"
#include <unistd.h>

int get_cpu_count(void) {
#ifdef _SC_NPROCESSORS_ONLN
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0) return (int)n;
#endif
    return 1;
}

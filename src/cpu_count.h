#ifndef SRT2DVB_CPU_COUNT_H
#define SRT2DVB_CPU_COUNT_H

/* Return number of available CPUs (>=1). Uses POSIX sysconf fallback. */
int get_cpu_count(void);

#endif

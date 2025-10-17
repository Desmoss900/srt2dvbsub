#ifndef BENCH_H
#define BENCH_H
#include <stdint.h>

typedef struct {
    int enabled;
    int64_t t_parse_us;
    int64_t t_render_us;
    int64_t t_encode_us;
    int64_t t_mux_us;
    int cues_rendered;
    int cues_encoded;
    int packets_muxed;
} BenchStats;

extern BenchStats bench;

void bench_start(void);
int64_t bench_now(void);
void bench_report(void);

#endif
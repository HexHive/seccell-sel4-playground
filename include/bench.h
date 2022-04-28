#ifndef BENCH_H
#define BENCH_H

#include <stddef.h>

typedef struct perf_counters {
    size_t instret;
    size_t cycle;
    size_t time;
} perf_counters_t;

#define RDCTR(dest, counter)          \
    do {                              \
        asm volatile(                 \
            "rd" # counter " %[ctr]"  \
            : [ctr] "=r"(dest)        \
            :);                       \
    } while (0)

#endif /* BENCH_H */

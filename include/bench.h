#ifndef BENCH_H
#define BENCH_H

#include <stddef.h>

typedef struct perf_counters {
    uint64_t instret;
    uint64_t cycle;
    uint64_t time;
    uint64_t mem, tlb;
} perf_counters_t;

#define RDCTR(dest, counter)          \
    do {                              \
        asm volatile(                 \
            "rd" # counter " %[ctr]"  \
            : [ctr] "=r"(dest)        \
            :);                       \
    } while (0)


#define RD_CTR(dest, counter)          \
    do {                              \
        asm volatile(                 \
            "csrr %[ctr], " # counter \
            : [ctr] "=r"(dest)        \
            :);                       \
    } while (0)

#endif /* BENCH_H */

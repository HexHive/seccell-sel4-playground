#ifndef EVAL_H
#define EVAL_H

/* Inclusions */
#include <sel4/sel4.h>

/* Structs/datatypes */
typedef struct {
    seL4_Word start, end;
} hwcounter_t;

typedef struct {
    seL4_Word num_pages, page_bits;
} vma_t;

typedef struct {
    void *addr;
    size_t size;
} shared_mem_t;

/* Macros */
#define RDINSTRET(counter)        \
    do {                          \
        asm volatile(             \
            "rdinstret %[ctr]"    \
            : [ctr] "=r"(counter) \
            :);                   \
    } while (0)

#define RDCYCLE(counter)          \
    do {                          \
        asm volatile(             \
            "rdcycle %[ctr]"      \
            : [ctr] "=r"(counter) \
            :);                   \
    } while (0)

#define RDTIME(counter)           \
    do {                          \
        asm volatile(             \
            "rdtime %[ctr]"       \
            : [ctr] "=r"(counter) \
            :);                   \
    } while (0)

#endif /* EVAL_H */

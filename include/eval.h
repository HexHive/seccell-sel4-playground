#ifndef EVAL_H
#define EVAL_H

/* Inclusions */
#include <sel4/sel4.h>

/* Structs/datatypes */

typedef enum {
    EVAL_IPC,
    EVAL_TLB
} task_t;
typedef struct {
    seL4_Word start, end;
} hwcounter_t;

typedef struct {
    seL4_Word num_pages, page_bits;
} vma_t;

typedef struct {
    void *addr;
    seL4_Word size;
} shared_mem_t;

typedef struct {
    task_t task;
    shared_mem_t buf;
} eval_run_t;

/* Macros */
#define RDINSTRET(counter)        \
    do {                          \
        asm(                      \
            "rdinstret %[ctr]"    \
            : [ctr] "=r"(counter) \
            :);                   \
    } while (0)

#define RDCYCLE(counter)          \
    do {                          \
        asm(                      \
            "rdcycle %[ctr]"      \
            : [ctr] "=r"(counter) \
            :);                   \
    } while (0)

#define RDTIME(counter)           \
    do {                          \
        asm(                      \
            "rdtime %[ctr]"       \
            : [ctr] "=r"(counter) \
            :);                   \
    } while (0)

#endif /* EVAL_H */

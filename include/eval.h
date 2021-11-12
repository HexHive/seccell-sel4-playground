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
    seL4_Word base_addr, size;
} vma_t;

typedef struct {
    vma_t vma;
    task_t task;
    hwcounter_t inst, cycle, time;
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

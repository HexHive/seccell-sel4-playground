#include <seccells/scthreads.h>
#include <seccells/seccells.h>
#include <sel4/sel4.h>
#include <sel4/sel4_arch/mapping.h>
#include <sel4platsupport/bootinfo.h>
#include <sel4platsupport/platsupport.h>
#include <sel4utils/util.h>
#include <stdio.h>
#include <utils/util.h>

#include "alloc.h"
#include "debug.h"
#include "eval.h"

#define BASE_VADDR 0xA000000
#define CONTEXT_VADDR 0xB000000
#define NUM_SECDIVS 3 /* Number of userspace SecDivs (including the initially running SecDiv) */

const seL4_Word BUFSIZES[] = {
    0x1000,     /*    4kiB */
    0x10000,    /*  64 kiB */
    0x19000,    /* 100 kiB */
    0x100000,   /*   1 MiB */
    0x800000,   /*   8 MiB */
    0x6400000,  /* 100 MiB */
    0x20000000, /* 512 MiB */
};
#define RUNS (sizeof(BUFSIZES) / sizeof(*BUFSIZES))

void setup_secdivs(void);
void revoke_secdivs(void);
void *eval_thread_1(void *args);
void *eval_thread_2(void *args);

seL4_RISCV_RangeTable_AddSecDiv_t secdivs[NUM_SECDIVS];

int main(int argc, char *argv[]) {
    /* Parse the location of the seL4_BootInfo data structure from
       the environment variables set up by the default crt0.S */
    seL4_BootInfo *info = platsupport_get_bootinfo();
    /* Setup serial output via seL4_Debug_PutChar */
    if (platsupport_serial_setup_bootinfo_failsafe()) {
        /* Error occured during setup => terminate */
        return 1;
    }
#if DEBUG
    debug_print_bootinfo(info);
#endif /* DEBUG */

    /* Want to have at least 3 SecDivs: the initial one plus two SecDivs communicating */
    assert(NUM_SECDIVS >= 3);
    /* Set up SecDivs for communication */
    setup_secdivs();

    /* Assume SecDiv IDs are assigned sequentially (which currently is actually the case) */
    unsigned int max_secdiv_id = NUM_SECDIVS + 1;
    scthreads_init_contexts(info, (void *)CONTEXT_VADDR, max_secdiv_id);

    seL4_Word vaddr = BASE_VADDR;
    /* Map a read-write 4k range at BASE_VADDR for the evaluation run arguments */
    seL4_CPtr range = alloc_object(info, seL4_RISCV_RangeObject, BIT(seL4_MinRangeBits));
    seL4_Error error = seL4_RISCV_Range_Map(range, seL4_CapInitThreadVSpace, vaddr, seL4_ReadWrite,
                                            seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to map range @ %p", (void *)vaddr);

    /* Have to grant read-execute permissions for the other SecDivs to execute the code */
    grant(&eval_thread_1, secdivs[1].id, RT_R | RT_X);
    grant(&eval_thread_2, secdivs[2].id, RT_R | RT_X);

    /* Initialize common evaluation run arguments */
    eval_run_t *run_args = (eval_run_t *)vaddr;
    vaddr += BIT(seL4_MinRangeBits);
    memset(run_args, 0, sizeof(eval_run_t));
    run_args->vma.base_addr = vaddr;

    for (int i = 0; i < RUNS; i++) {
        seL4_Word buf_size = BUFSIZES[i];
        seL4_CPtr shared_buf = alloc_object(info, seL4_RISCV_RangeObject, buf_size);
        /* Initialize run-specific arguments and transfer permissions to runner thread */
        run_args->vma.size = buf_size;
        run_args->task = EVAL_IPC;
        /* Shared buffer setup */
        error = seL4_RISCV_Range_Map(shared_buf, seL4_CapInitThreadVSpace, run_args->vma.base_addr, seL4_ReadWrite,
                                     seL4_RISCV_Default_VMAttributes);
        ZF_LOGF_IF(error != seL4_NoError, "Failed to map range @ %p", (void *)run_args->vma.base_addr);
        /* Transfer permissions on shared buffer and run arguments to runner thread */
        tfer(run_args->vma.base_addr, secdivs[1].id, RT_R | RT_W);
        tfer(run_args, secdivs[1].id, RT_R | RT_W);

        scthreads_call(secdivs[1].id, &eval_thread_1, (void *)run_args);

        /* Shared buffer teardown */
        error = seL4_RISCV_Range_Unmap(shared_buf);
        ZF_LOGF_IF(error != seL4_NoError, "Failed to unmap range");

        printf("Evaluation run with buffer size 0x%x bytes\n", buf_size);
        printf("Metric               Value\n");
        printf("--------------------------\n");
        printf("Instructions    %10d\n", run_args->inst.end - run_args->inst.start);
        printf("Cycles          %10d\n", run_args->cycle.end - run_args->cycle.start);
        printf("Time            %10d\n\n", run_args->time.end - run_args->time.start);
    }

    /* Suspend the root server - isn't needed anymore */
    seL4_TCB_Suspend(seL4_CapInitThreadTCB);

    return 0;
}

void *eval_thread_1(void *args) {
    eval_run_t *run = (eval_run_t *)args;

    /* Start performance counters */
    RDINSTRET(run->inst.start);
    RDCYCLE(run->cycle.start);
    RDTIME(run->time.start);

    /* Communication buffer initialization => make sure every byte was touched */
    memset((void *)run->vma.base_addr, 0x41, run->vma.size);

    /* Communication with other scthread */
    tfer(run->vma.base_addr, secdivs[2].id, RT_R | RT_W);
    grant(&(run->vma), secdivs[2].id, RT_R);
    scthreads_call(secdivs[2].id, &eval_thread_2, &(run->vma));

    /* End performance counters */
    RDINSTRET(run->inst.end);
    RDCYCLE(run->cycle.end);
    RDTIME(run->time.end);

    tfer(run->vma.base_addr, secdivs[0].id, RT_R | RT_W);
    tfer(run, secdivs[0].id, RT_R | RT_W);
    scthreads_return(NULL);
}

void *eval_thread_2(void *args) {
    vma_t *vma = (vma_t *)args;

    /* Touch all the bytes in the passed buffer */
    memset((void *)vma->base_addr, 0x61, vma->size);

    /* Hand control back to calling thread */
    tfer(vma->base_addr, secdivs[1].id, RT_R | RT_W);
    prot(vma, 0);
    scthreads_return(NULL);
}

void setup_secdivs(void) {
    /* Save current SecDiv ID => probably the initial SecDiv anyway */
    csrr_usid(secdivs[0].id);
    /* Create new SecDivs */
    for (int i = 1; i < NUM_SECDIVS; i++) {
        secdivs[i] = seL4_RISCV_RangeTable_AddSecDiv(seL4_CapInitThreadVSpace);
        ZF_LOGF_IF(secdivs[i].error != seL4_NoError, "Failed to create new SecDiv");
        DEBUGPRINT("Created new SecDiv with ID %d\n", secdivs[i].id);
    }
}

void revoke_secdivs(void) {
    seL4_Error error;
    /* Revoke SecDiv permissions (not for initial SecDiv => start at index 1) */
    for (int i = 1; i < NUM_SECDIVS; i++) {
        error = seL4_RISCV_RangeTable_RevokeSecDiv(seL4_CapInitThreadVSpace, secdivs[i].id);
        ZF_LOGF_IF(error != seL4_NoError, "Failed to revoke SecDiv permissions");
    }
}

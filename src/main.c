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
#define BUFFER_VADDR (BASE_VADDR + BIT(seL4_MinRangeBits))
#define CONTEXT_VADDR 0xF000000
#define NUM_SECDIVS 2 /* Number of userspace SecDivs (including the initially running SecDiv) */

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

void run_ipc_eval(seL4_BootInfo *info, seL4_Word size);
void *eval_client(void *args);
void init_client(void);
seL4_CPtr init_buffer(seL4_BootInfo *info, shared_mem_t *buf);
void teardown_buffer(seL4_CPtr buf_cap);

seL4_RISCV_RangeTable_AddSecDiv_t secdivs[NUM_SECDIVS];
eval_run_t *run_args = (eval_run_t *)BASE_VADDR;

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

    /* Want to have at least 2 SecDivs: the initial one plus the client SecDiv */
    assert(NUM_SECDIVS >= 2);
    /* Set up second SecDiv/evaluation client */
    init_client();

    /* Assume SecDiv IDs are assigned sequentially (which currently is actually the case) */
    unsigned int max_secdiv_id = NUM_SECDIVS + 1;
    scthreads_init_contexts(info, (void *)CONTEXT_VADDR, max_secdiv_id);

    /* Map a read-write 4k range for the evaluation run arguments to pass to the second thread */
    seL4_CPtr range = alloc_object(info, seL4_RISCV_RangeObject, BIT(seL4_MinRangeBits));
    seL4_Error error = seL4_RISCV_Range_Map(range, seL4_CapInitThreadVSpace, (seL4_Word)run_args, seL4_ReadWrite,
                                            seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to map range @ %p", run_args);

    /* Have to grant read-execute permissions for the other SecDiv to execute the code */
    grant(&eval_client, secdivs[1].id, RT_R | RT_X);

    for (int i = 0; i < RUNS; i++) {
        run_ipc_eval(info, BUFSIZES[i]);
    }

    /* Suspend the root server - isn't needed anymore */
    seL4_TCB_Suspend(seL4_CapInitThreadTCB);

    return 0;
}

void run_ipc_eval(seL4_BootInfo *info, seL4_Word size) {
    hwcounter_t inst, cycle, time;

    /* Make sure the run_args are clean/empty before we run the evaluation */
    memset((void *)run_args, 0, sizeof(eval_run_t));
    /* Set up shared buffer */
    shared_mem_t buf = {
        .addr = (void *)BUFFER_VADDR,
        .size = size,
    };
    seL4_CPtr buf_cap = init_buffer(info, &buf);

    /* Start measurements */
    RDINSTRET(inst.start);
    RDCYCLE(cycle.start);
    RDTIME(time.start);

    /* Touch the buffer with the specified size once */
    memset(buf.addr, 0x41, buf.size);

    /* Setup data to pass along with the SecDiv switch and adapt permissions */
    run_args->task = EVAL_IPC;
    run_args->buf = buf;
    tfer(run_args->buf.addr, secdivs[1].id, RT_R | RT_W);
    tfer(run_args, secdivs[1].id, RT_R | RT_W);

    scthreads_call(secdivs[1].id, &eval_client, (void *)run_args);

    /* End performance counters */
    RDINSTRET(inst.end);
    RDCYCLE(cycle.end);
    RDTIME(time.end);

    teardown_buffer(buf_cap);

    printf("IPC Evaluation run with buffer size 0x%x bytes\n", size);
    printf("Metric               Value\n");
    printf("--------------------------\n");
    printf("Instructions    %10d\n", inst.end - inst.start);
    printf("Cycles          %10d\n", cycle.end - cycle.start);
    printf("Time            %10d\n\n", time.end - time.start);
}

/* Evaluate IPC: only done on small buffers, access the full buffer */
void *eval_client(void *args) {
    eval_run_t *run = (eval_run_t *)args;

    memset(run->buf.addr, 0x61, run->buf.size);

    /* Hand control back to calling thread */
    tfer(run->buf.addr, secdivs[0].id, RT_R | RT_W);
    tfer(run, secdivs[0].id, RT_R | RT_W);
    scthreads_return(NULL);
}

void init_client(void) {
    /* Save current SecDiv ID => probably the initial SecDiv anyway */
    csrr_usid(secdivs[0].id);
    /* Create new SecDivs */
    for (int i = 1; i < NUM_SECDIVS; i++) {
        secdivs[i] = seL4_RISCV_RangeTable_AddSecDiv(seL4_CapInitThreadVSpace);
        ZF_LOGF_IF(secdivs[i].error != seL4_NoError, "Failed to create new SecDiv");
        DEBUGPRINT("Created new SecDiv with ID %d\n", secdivs[i].id);
    }
}

/* Create a capability for a buffer of specified size and map it at the specified address */
seL4_CPtr init_buffer(seL4_BootInfo *info, shared_mem_t *buf) {
    /* Get a capability for the shared buffer */
    seL4_CPtr shared_buf = alloc_object(info, seL4_RISCV_RangeObject, buf->size);
    /* Shared buffer setup */
    seL4_Error error = seL4_RISCV_Range_Map(shared_buf, seL4_CapInitThreadVSpace, (seL4_Word)buf->addr, seL4_ReadWrite,
                                            seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to map range @ %p", buf->addr);

    return shared_buf;
}

/* Unmap buffer */
void teardown_buffer(seL4_CPtr buf_cap) {
    seL4_Error error = seL4_RISCV_Range_Unmap(buf_cap);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to unmap range");
}

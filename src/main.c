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

const seL4_Word IPC_BUFSIZES[] = {
    0x00001, /*    1 B         */
    0x00010, /*   16 B         */
    0x00080, /*  128 B         */
    0x00200, /*  512 B         */
    0x00400, /* 1024 B = 1 KiB */
};
#define IPC_RUNS (sizeof(IPC_BUFSIZES) / sizeof(*IPC_BUFSIZES))
/*
 * On RISC-V, we have page sizes 4 KiB, 2 MiB and 1 GiB
 * 4 KiB <==> seL4_PageBits
 * 2 MiB <==> seL4_LargePageBits
 * 1 GiB <==> seL4_HugePageBits
 * We obviously don't care about that for our range-based system but we keep the structure for easier comparison with
 * the baseline
 */
const vma_t TLB_BUFSIZES[] = {
    /* {num_pages, page_bits} */
    {0x00001, seL4_PageBits}, /*   4 KiB */
    {0x00010, seL4_PageBits}, /*  64 KiB */
    {0x00019, seL4_PageBits}, /* 100 KiB */
    {0x00100, seL4_PageBits}, /*   1 MiB */
    {0x00800, seL4_PageBits}, /*   8 MiB */
    {0x02000, seL4_PageBits}, /*  32 MiB */
};
#define TLB_RUNS (sizeof(TLB_BUFSIZES) / sizeof(*TLB_BUFSIZES))

void run_ipc_eval(seL4_BootInfo *info, seL4_Word size);
void run_tlb_eval(seL4_BootInfo *info, seL4_Word size);
void *eval_client(void *args);
void eval_ipc(void *buf, size_t bufsize);
void eval_tlb(void *buf, size_t bufsize);
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
    int cnt = -1;
    count(cnt, &eval_client, RT_R | RT_X);
    if (cnt == 1) {
        /* Second thread doesn't have access yet => grant executable permissions */
        grant(&eval_client, secdivs[1].id, RT_R | RT_X);
    }
    cnt = -1;
    count(cnt, &eval_ipc, RT_R | RT_X);
    if (cnt == 1) {
        /* Second thread doesn't have access yet => grant executable permissions */
        grant(&eval_ipc, secdivs[1].id, RT_R | RT_X);
    }
    cnt = -1;
    count(cnt, &eval_tlb, RT_R | RT_X);
    if (cnt == 1) {
        /* Second thread doesn't have access yet => grant executable permissions */
        grant(&eval_tlb, secdivs[1].id, RT_R | RT_X);
    }

    /* Address translation focused benchmark */
    for (int i = 0; i < TLB_RUNS; i++) {
        run_tlb_eval(info, TLB_BUFSIZES[i].num_pages * BIT(TLB_BUFSIZES[i].page_bits));
    }
    /* IPC speed focused benchmark */
    for (int i = 0; i < IPC_RUNS; i++) {
        run_ipc_eval(info, IPC_BUFSIZES[i]);
    }

    /* Suspend the root server - isn't needed anymore */
    seL4_TCB_Suspend(seL4_CapInitThreadTCB);

    return 0;
}

/* Make an evaluation run with the specified shared buffer size => focus on IPC/thread switching speed */
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

/* Make an evaluation run where we only sparsely touch the buffer => focus on address translation */
void run_tlb_eval(seL4_BootInfo *info, seL4_Word size) {
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

    /* Touch only each 4096th byte of the buffer once to force address translation */
    char *charbuf = (char *)buf.addr;
    for (size_t i = 0; i < buf.size; i += BIT(seL4_PageBits)) {
        charbuf[i] = 0x41;
    }

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

/* Dispatch evaluation tasks */
void *eval_client(void *args) {
    eval_run_t *run = (eval_run_t *)args;

    /* Determine what to do based on the task we got transferred */
    switch (run->task) {
        case EVAL_IPC: {
            eval_ipc(run->buf.addr, run->buf.size);
            break;
        }

        case EVAL_TLB: {
            eval_tlb(run->buf.addr, run->buf.size);
            break;
        }

        default: {
            /* Due to covering all enum values, we should never arrive here */
            UNREACHABLE();
            break;
        }
    }

    /* Hand control back to calling thread */
    tfer(run->buf.addr, secdivs[0].id, RT_R | RT_W);
    tfer(run, secdivs[0].id, RT_R | RT_W);
    scthreads_return(NULL);
}

/* Evaluate IPC: only done on small buffers, access the full buffer */
void eval_ipc(void *buf, size_t bufsize) {
    memset(buf, 0x61, bufsize);
}

/* Evaluate TLB reach: big buffers, only touch a single byte per page to force address translation */
void eval_tlb(void *buf, size_t bufsize) {
    char *charbuf = (char *)buf;
    for (size_t i = 0; i < bufsize; i += BIT(seL4_PageBits)) {
        charbuf[i] = 0x61;
    }
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
    seL4_CPtr shared_buf = alloc_object(info, seL4_RISCV_RangeObject, ROUND_UP(buf->size, BIT(seL4_MinRangeBits)));
    /* Shared buffer setup */
    seL4_Error error = seL4_RISCV_Range_Map(shared_buf, seL4_CapInitThreadVSpace, (seL4_Word)buf->addr, seL4_ReadWrite,
                                            seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to map range @ %p", buf->addr);

    return shared_buf;
}

/* Unmap buffer and free capabilities */
void teardown_buffer(seL4_CPtr buf_cap) {
    /* Unmap the buffer */
    seL4_Error error = seL4_RISCV_Range_Unmap(buf_cap);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to unmap range");
    /* Revoke the capability */
    error = seL4_CNode_Revoke(seL4_CapInitThreadCNode, buf_cap, seL4_WordBits);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to revoke buffer cap");
}

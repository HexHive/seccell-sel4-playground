#include <allocman/allocman.h>
#include <allocman/bootstrap.h>
#include <allocman/vka.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>
#include <sel4platsupport/platsupport.h>
#include <sel4utils/mapping.h>
#include <sel4utils/process.h>
#include <sel4utils/util.h>
#include <sel4utils/vspace.h>
#include <stdio.h>
#include <utils/util.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include "debug.h"
#include "eval.h"

/* Dimensions of virtual memory for the allocator to use */
#define ALLOCATOR_VIRTUAL_POOL_SIZE (BIT(seL4_PageBits) * 600)
/* Static memory for the allocator to bootstrap with */
#define ALLOCATOR_STATIC_POOL_SIZE (BIT(seL4_PageBits) * 10)
static char allocator_mem_pool[ALLOCATOR_STATIC_POOL_SIZE];
/* Global allocator state */
vka_t vka;
allocman_t *allocman;
vspace_t vspace;
sel4utils_alloc_data_t data;
sel4utils_process_t new_process;

void run_tlb_eval(seL4_CPtr endpoint, seL4_Word num_pages, seL4_Word page_bits);
void run_ipc_eval(seL4_CPtr endpoint, seL4_Word size);
void init_allocator(seL4_BootInfo *info);
seL4_CPtr init_endpoint(void);
void init_client(seL4_CPtr base_ep);
void init_buffer(shared_mem_t *buf);
void teardown_buffer(shared_mem_t *buf);

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

int main(int argc, char *argv[]) {
    /* Parse the location of the seL4_BootInfo data structure from
       the environment variables set up by the default crt0.S */
    seL4_BootInfo *info = platsupport_get_bootinfo();
    /* Setup serial output via seL4_Debug_PutChar */
    if (platsupport_serial_setup_bootinfo_failsafe()) {
        /* Error occured during setup => terminate */
        return 1;
    }
    debug_print_bootinfo(info);

    /* Set up benchmark environment (second process, IPC endpoints) */
    seL4_CPtr endpoint;
    init_allocator(info);
    endpoint = init_endpoint();
    init_client(endpoint);

    /* Do the benchmarking */
    for (int i = 0; i < TLB_RUNS; i++) {
        run_tlb_eval(endpoint, TLB_BUFSIZES[i].num_pages, TLB_BUFSIZES[i].page_bits);
    }
    for (int i = 0; i < IPC_RUNS; i++) {
        run_ipc_eval(endpoint, IPC_BUFSIZES[i]);
    }

    /* Stop the second process - expects 3 arguments */
    task_t task = EVAL_EXIT;
    seL4_SetMR(0, (seL4_Word)task);
    seL4_SetMR(1, 0);
    seL4_SetMR(2, 0);
    seL4_MessageInfo_t msginfo = seL4_MessageInfo_new(0x42, 0, 0, 3);
    seL4_Send(endpoint, msginfo);

    /* Suspend the root server - isn't needed anymore */
    DEBUGPRINT("Suspending... Bye!\n");
    seL4_TCB_Suspend(seL4_CapInitThreadTCB);

    UNREACHABLE();

    return 0;
}

/* Make an evaluation run with the specified shared buffer size => focus on IPC speed */
void run_ipc_eval(seL4_CPtr endpoint, seL4_Word size) {
    hwcounter_t inst, cycle, time;

    shared_mem_t buffer = {
        .local = NULL,
        .remote = NULL,
        .num_pages = 1,
        .page_bits = seL4_PageBits,
    };

    init_buffer(&buffer);

    /* Start measurements */
    RDINSTRET(inst.start);
    RDCYCLE(cycle.start);
    RDTIME(time.start);

    /* Touch the buffer with the specified size once */
    memset(buffer.local, 0x41, size);

    /* Setup data to pass along with the IPC */
    task_t task = EVAL_TLB;
    seL4_Word addr = (seL4_Word)buffer.remote;
    seL4_SetMR(0, (seL4_Word)task);
    seL4_SetMR(1, addr);
    seL4_SetMR(2, size);
    seL4_MessageInfo_t msginfo = seL4_MessageInfo_new(0xdeadbeef, 0, 0, 3);
    /* Call into the second process and wait for its response */
    msginfo = seL4_Call(endpoint, msginfo);

    /* End measurements */
    RDINSTRET(inst.end);
    RDCYCLE(cycle.end);
    RDTIME(time.end);

    DEBUGPRINT("Received answer with label 0x%x\n", seL4_MessageInfo_get_label(msginfo));

    teardown_buffer(&buffer);

    printf("IPC Evaluation run with buffer size 0x%x bytes\n", size);
    printf("Metric               Value\n");
    printf("--------------------------\n");
    printf("Instructions    %10d\n", inst.end - inst.start);
    printf("Cycles          %10d\n", cycle.end - cycle.start);
    printf("Time            %10d\n\n", time.end - time.start);
}

/* Make an evaluation run with the specified number of pages for the shared buffer => focus on address translation */
void run_tlb_eval(seL4_CPtr endpoint, seL4_Word num_pages, seL4_Word page_bits) {
    hwcounter_t inst, cycle, time;

    shared_mem_t buffer = {
        .local = NULL,
        .remote = NULL,
        .num_pages = num_pages,
        .page_bits = page_bits,
    };

    init_buffer(&buffer);

    /* Start measurements */
    RDINSTRET(inst.start);
    RDCYCLE(cycle.start);
    RDTIME(time.start);

    /* Touch each page of the buffer once to force address translation */
    char *buf = (char *)buffer.local;
    for (int i = 0; i < buffer.num_pages; i++) {
        buf[i * BIT(buffer.page_bits)] = 0x41;
    }

    /* Setup data to pass along with the IPC */
    task_t task = EVAL_TLB;
    seL4_Word addr = (seL4_Word)buffer.remote;
    seL4_Word size = buffer.num_pages * BIT(buffer.page_bits);
    seL4_SetMR(0, (seL4_Word)task);
    seL4_SetMR(1, addr);
    seL4_SetMR(2, size);
    seL4_MessageInfo_t msginfo = seL4_MessageInfo_new(0xdeadbeef, 0, 0, 3);
    /* Call into the second process and wait for its response */
    msginfo = seL4_Call(endpoint, msginfo);

    /* End measurements */
    RDINSTRET(inst.end);
    RDCYCLE(cycle.end);
    RDTIME(time.end);

    DEBUGPRINT("Received answer with label 0x%x\n", seL4_MessageInfo_get_label(msginfo));

    teardown_buffer(&buffer);

    printf("TLB Evaluation run with buffer size 0x%x bytes\n", size);
    printf("Metric               Value\n");
    printf("--------------------------\n");
    printf("Instructions    %10d\n", inst.end - inst.start);
    printf("Cycles          %10d\n", cycle.end - cycle.start);
    printf("Time            %10d\n\n", time.end - time.start);
}

/* Initialize an allocator with seL4_utils for easier object creation and manipulation */
void init_allocator(seL4_BootInfo *info) {
    /* Setup the allocator with some static memory */
    allocman = bootstrap_use_bootinfo(info, ALLOCATOR_STATIC_POOL_SIZE, allocator_mem_pool);
    assert(allocman);
    allocman_make_vka(&vka, allocman);

    /* Bootstrap a VSpace object with the current VSpace for easier interaction */
    int error = sel4utils_bootstrap_vspace_with_bootinfo_leaky(&vspace, &data, seL4_CapInitThreadVSpace, &vka, info);
    assert(error == 0);

    /* Provide virtual memory to the allocator */
    void *vaddr;
    reservation_t virtual_reservation = vspace_reserve_range(&vspace, ALLOCATOR_VIRTUAL_POOL_SIZE, seL4_AllRights, 1,
                                                             &vaddr);
    assert(virtual_reservation.res);
    bootstrap_configure_virtual_pool(allocman, vaddr, ALLOCATOR_VIRTUAL_POOL_SIZE, seL4_CapInitThreadVSpace);
}

/* Initialize an endpoint for the IPC communication with the second process */
seL4_CPtr init_endpoint(void) {
    vka_object_t ep = {0};
    int error = vka_alloc_endpoint(&vka, &ep);
    assert(error == 0);

    return ep.cptr;
}

/* Initialize another process (own CSpace, own VSpace, minted endpoint capability) and run it */
void init_client(seL4_CPtr base_ep) {
    /* Load ELF from CPIO archive and configure new process */
    sel4utils_process_config_t config = process_config_default("client", seL4_CapInitThreadASIDPool);
    config = process_config_priority(config, seL4_MaxPrio);
    config = process_config_auth(config, seL4_CapInitThreadTCB);
    int error = sel4utils_configure_process_custom(&new_process, &vka, &vspace, config);
    assert(error == 0);

    /* Mint the endpoint capability and copy it into the process's CSpace */
    cspacepath_t ep_path;
    vka_cspace_make_path(&vka, base_ep, &ep_path);
    seL4_CPtr minted_ep = sel4utils_mint_cap_to_process(&new_process, ep_path, seL4_AllRights, 0x42);
    assert(minted_ep != 0);

    /* Create argv for new process => pass the CPtr to the minted capability in the new process's CSpace */
    char arg_strings[1][WORD_STRING_SIZE];
    char *argv[1];
    sel4utils_create_word_args(arg_strings, argv, 1, minted_ep);

    /* Launch the new process with the prepared argv */
    error = sel4utils_spawn_process_v(&new_process, &vka, &vspace, 1, argv, 1);
    assert(error == 0);
}

/* Create capabilities for a buffer of specified size and map it at the specified virtual memory address */
void init_buffer(shared_mem_t *buf) {
    /* Map contiguous range in the current VSpace */
    buf->local = vspace_new_pages(&vspace, seL4_ReadWrite, buf->num_pages, buf->page_bits);
    assert(buf->local != 0);

    /* Map the range also in the second thread's VSpace */
    buf->remote = vspace_share_mem(&vspace, &new_process.vspace, buf->local, buf->num_pages, buf->page_bits,
                                   seL4_ReadWrite, 1);
    assert(buf->remote != 0);
}

/* Unmap buffer and free capabilities */
void teardown_buffer(shared_mem_t *buf) {
    /* Unmap pages in both VSpaces: the rootserver's and the second process's */
    vspace_unmap_pages(&vspace, buf->local, buf->num_pages, buf->page_bits, VSPACE_FREE);
    vspace_unmap_pages(&new_process.vspace, buf->remote, buf->num_pages, buf->page_bits, VSPACE_FREE);
}

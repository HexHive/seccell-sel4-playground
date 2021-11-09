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
#include <simple-default/simple-default.h>
#include <simple/simple.h>
#include <stdbool.h>
#include <stdio.h>
#include <utils/util.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include "debug.h"
#include "eval.h"

/* Dimensions of virtual memory for the allocator to use */
#define ALLOCATOR_VIRTUAL_POOL_SIZE (BIT(seL4_PageBits) * 100)
/* Static memory for the allocator to bootstrap with */
#define ALLOCATOR_STATIC_POOL_SIZE (BIT(seL4_PageBits) * 50)
static char allocator_mem_pool[ALLOCATOR_STATIC_POOL_SIZE];
/* Global allocator state */
simple_t simple;
vka_t vka;
allocman_t *allocman;
vspace_t vspace;
sel4utils_alloc_data_t data;
sel4utils_process_t new_process;

void run_eval(seL4_CPtr endpoint, seL4_Word num_pages, seL4_Word page_bits);
void init_allocator(seL4_BootInfo *info);
seL4_CPtr init_endpoint(void);
void init_client(seL4_CPtr base_ep);
void init_buffer(shared_mem_t *buf);
void teardown_buffer(shared_mem_t *buf);

/*
 * On RISC-V, we have page sizes 4 KiB, 2 MiB and 1 GiB
 * 4 KiB <==> seL4_PageBits
 * 2 MiB <==> seL4_LargePageBits
 * 1 GiB <==> seL4_HugePageBits
 */

const vma_t BUFSIZES[] = {
    /* {num_pages, page_bits} */
    {1, seL4_PageBits},        /*   4 KiB */
    {16, seL4_PageBits},       /*  64 KiB */
    {25, seL4_PageBits},       /* 100 KiB */
    {256, seL4_PageBits},      /*   1 MiB */
    {4, seL4_LargePageBits},   /*   8 MiB */
    {50, seL4_LargePageBits},  /* 100 MiB */
    {256, seL4_LargePageBits}, /* 512 MiB */
};
#define RUNS (sizeof(BUFSIZES) / sizeof(*BUFSIZES))

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

    seL4_CPtr endpoint;
    init_allocator(info);
    endpoint = init_endpoint();
    init_client(endpoint);

    for (int i = 0; i < RUNS; i++) {
        run_eval(endpoint, BUFSIZES[i].num_pages, BUFSIZES[i].page_bits);
    }

    /* Stop the second process */
    bool exit = true;
    seL4_SetMR(0, (seL4_Word)exit);
    seL4_MessageInfo_t msginfo = seL4_MessageInfo_new(0x42, 0, 0, 1);
    seL4_Send(endpoint, msginfo);

    /* Suspend the root server - isn't needed anymore */
    DEBUGPRINT("Suspending... Bye!\n");
    seL4_TCB_Suspend(seL4_CapInitThreadTCB);

    return 0;
}

/* Make an evaluation run with the specified number of pages for the shared buffer */
void run_eval(seL4_CPtr endpoint, seL4_Word num_pages, seL4_Word page_bits) {
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

    /* Touch the full buffer once */
    memset(buffer.local, 0x41, buffer.num_pages * BIT(buffer.page_bits));

    /* Setup data to pass along with the IPC */
    bool exit = false;
    seL4_Word addr = (seL4_Word)buffer.remote;
    seL4_Word size = buffer.num_pages * BIT(buffer.page_bits);
    seL4_SetMR(0, (seL4_Word)exit);
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

    printf("Evaluation run with buffer size 0x%x bytes\n", size);
    printf("Metric               Value\n");
    printf("--------------------------\n");
    printf("Instructions    %10d\n", inst.end - inst.start);
    printf("Cycles          %10d\n", cycle.end - cycle.start);
    printf("Time            %10d\n\n", time.end - time.start);
}

/* Initialize an allocator with seL4_utils for easier object creation and manipulation */
void init_allocator(seL4_BootInfo *info) {
    /* Initialize simple */
    simple_default_init_bootinfo(&simple, info);

    /* Setup the allocator with some static memory */
    allocman = bootstrap_use_current_simple(&simple, ALLOCATOR_STATIC_POOL_SIZE, allocator_mem_pool);
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
    sel4utils_process_config_t config = process_config_default_simple(&simple, "client", seL4_MaxPrio);
    config = process_config_auth(config, simple_get_tcb(&simple));
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

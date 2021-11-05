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
#include <stdio.h>
#include <utils/util.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#define BASE_VADDR 0xA000000

/* Dimensions of virtual memory for the allocator to use */
#define ALLOCATOR_VIRTUAL_POOL_SIZE (BIT(seL4_PageBits) * 100)
/* Static memory for the allocator to bootstrap with */
#define ALLOCATOR_STATIC_POOL_SIZE (BIT(seL4_PageBits) * 10)
static char allocator_mem_pool[ALLOCATOR_STATIC_POOL_SIZE];
/* Global allocator state */
simple_t simple;
vka_t vka;
allocman_t *allocman;
vspace_t vspace;
sel4utils_alloc_data_t data;

void init_allocator(seL4_BootInfo *info);
seL4_CPtr init_endpoint(void);
void init_client(seL4_CPtr base_ep);

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

    /* Call into the second process and wait for its response */
    seL4_MessageInfo_t msginfo = seL4_MessageInfo_new(0xdeadbeef, 0, 0, 0);
    msginfo = seL4_Call(endpoint, msginfo);
    printf("[server] Received answer with label 0x%x\n", seL4_MessageInfo_get_label(msginfo));

    /* Suspend the root server - isn't needed anymore */
    seL4_TCB_Suspend(seL4_CapInitThreadTCB);

    return 0;
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
    sel4utils_process_t new_process;
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

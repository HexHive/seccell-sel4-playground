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
void init_client(void);

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

    init_allocator(info);
    init_client();

    /* Suspend the root server - isn't needed anymore */
    seL4_TCB_Suspend(seL4_CapInitThreadTCB);

    return 0;
}

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

void init_client(void) {
    /* Load ELF from CPIO archive and configure new process */
    sel4utils_process_t new_process;
    sel4utils_process_config_t config = process_config_default_simple(&simple, "client", seL4_MaxPrio);
    config = process_config_auth(config, simple_get_tcb(&simple));
    int error = sel4utils_configure_process_custom(&new_process, &vka, &vspace, config);
    assert(error == 0);

    /* Launch the new process */
    error = sel4utils_spawn_process_v(&new_process, &vka, &vspace, 0, NULL, 1);
    assert(error == 0);
}

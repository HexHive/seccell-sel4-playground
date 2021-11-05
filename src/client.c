#include <sel4/sel4.h>
#include <sel4/sel4_arch/mapping.h>
#include <sel4platsupport/bootinfo.h>
#include <sel4platsupport/platsupport.h>
#include <sel4utils/util.h>
#include <stdbool.h>
#include <stdio.h>
#include <utils/util.h>

int main(int argc, char *argv[]) {
    /* Setup serial output via seL4_Debug_PutChar */
    if (platsupport_serial_setup_bootinfo_failsafe()) {
        /* Error occured during setup => terminate */
        return 1;
    }

    /*
     * Check for args and retrieve CPtr to endpoint capability
     * Note: argv[0] is not the name of the executable in this setup
     */
    if (argc != 1) {
        printf("Error: invalid number of arguments\n");
        return 1;
    }
    seL4_CPtr endpoint = atol(argv[0]);

    /* Communication loop */
    while (true) {
        /* Wait for message on endpoint */
        seL4_Word sender = 0;
        seL4_MessageInfo_t msginfo = seL4_Recv(endpoint, &sender);
        printf("[client] Got woken up by an IPC message with label 0x%x\n", seL4_MessageInfo_get_label(msginfo));

        bool exit = (bool)seL4_GetMR(0);
        if (exit) {
            /* Other process told us to stop benchmarking => quit the benchmarking loop */
            break;
        }

        /* Get benchmarking => retrieve address and size of buffer */
        seL4_Word addr = seL4_GetMR(1);
        seL4_Word size = seL4_GetMR(2);

        /* TODO: this is where the actual benchmarking happens, for now just print stuff */
        printf("[client] Received: addr = %p, size = %ld\n", (void *)addr, size);

        /* Reply to the sender of the previous IPC message */
        msginfo = seL4_MessageInfo_new(0x1337, 0, 0, 0);
        seL4_Reply(msginfo);
    }

    printf("[client] Quitting... Bye!\n");
    return 0;
}

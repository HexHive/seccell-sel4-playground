#include <sel4/sel4.h>
#include <sel4/sel4_arch/mapping.h>
#include <sel4platsupport/bootinfo.h>
#include <sel4platsupport/platsupport.h>
#include <sel4utils/util.h>
#include <stdio.h>
#include <utils/util.h>

#include "debug.h"
#include "eval.h"

void eval_ipc(void *buf, size_t bufsize);
void eval_tlb(void *buf, size_t bufsize);

int __attribute__((optimize(2))) main(int argc, char *argv[]) {
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

    /* Wait for initial message on endpoint */
    seL4_Word sender = 0;
    seL4_MessageInfo_t msginfo = seL4_Recv(endpoint, &sender);
    /* Communication loop */
    while (true) {
        DEBUGPRINT("Got woken up by an IPC message with label 0x%x\n", seL4_MessageInfo_get_label(msginfo));

        /* Retrieve arguments that got sent along the IPC call */
        task_t task = (task_t)seL4_GetMR(0);
        seL4_Word addr = seL4_GetMR(1);
        seL4_Word size = seL4_GetMR(2);
        DEBUGPRINT("Received: addr = %p, size = %ld\n", (void *)addr, size);

        /* Determine what to do based on the task we got transferred */
        switch (task) {
            case EVAL_EXIT: {
                /* Other process told us to stop benchmarking => exit */
                DEBUGPRINT("Quitting... Bye!\n");
                return 0;
            }

            case EVAL_CONTEXT_SWITCH: {
                /* Just return back to the caller */
                break;
            }

            case EVAL_IPC: {
                eval_ipc((void *)addr, (size_t)size);
                break;
            }

            case EVAL_TLB: {
                eval_tlb((void *)addr, (size_t)size);
                break;
            }

            default: {
                /* Due to covering all enum values, we should never arrive here */
                abort();
                break;
            }
        }

        /* Reply to the sender of the previous IPC message and wait for a new message*/
        msginfo = seL4_MessageInfo_new(0x1337, 0, 0, 0);
        seL4_ReplyRecv(endpoint, msginfo, &sender);
    }
    /* We should never arrive here: the process quits by getting told to do so in the IPC event loop */
    UNREACHABLE();
}

/* Evaluate IPC: only done on small buffers, access the full buffer */
void __attribute__((optimize(2))) eval_ipc(void *buf, size_t bufsize) {
    memset(buf, 0x61, bufsize);
}

/* Evaluate TLB reach: big buffers, only touch a single byte per page to force address translation */
void __attribute__((optimize(2))) eval_tlb(void *buf, size_t bufsize) {
    /*
     * Touch only each 4096th byte of the buffer once to force address translation; offset by 64 bytes with each
     * run to hit different cache sets in the experiments on the FPGA
     */
    char *charbuf = (char *)buf;
    size_t incr = BIT(seL4_PageBits) + BIT(6);

    for (size_t i = 0; i < bufsize; i += incr) {
        charbuf[i] = 0x61;
    }
}

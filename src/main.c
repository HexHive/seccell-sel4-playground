#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>
#include <sel4platsupport/platsupport.h>
#include <utils/util.h>
#include <sel4utils/util.h>
#include <sel4/sel4_arch/mapping.h>

#include "alloc.h"

#define BASE_VADDR    0xA000000

void kernel_thread_creation_test(seL4_BootInfo *info);
void kernel_thread_target(void);

seL4_Word stack[4096];
seL4_CPtr tcb;

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

    kernel_thread_creation_test(info);

    /* Suspend the root server - isn't needed anymore */
    seL4_TCB_Suspend(seL4_CapInitThreadTCB);

    return 0;
}

void kernel_thread_creation_test(seL4_BootInfo *info) {
    printf("Setting up new TCB... ");
    seL4_Word error;

    tcb = alloc_object(info, seL4_TCBObject, seL4_TCBBits);

    error = seL4_TCB_Configure(
        tcb,                        /* TCB cap      */
        seL4_CapNull,               /* EP cap       */
        seL4_CapInitThreadCNode,    /* CSpace cap   */
        seL4_NilData,               /* CSpace data  */
        seL4_CapInitThreadVSpace,   /* VSpace cap   */
        seL4_NilData,               /* VSpace data  */
        (seL4_Word) seL4_Null,      /* IPC buf ptr  */
        seL4_CapInitThreadIPCBuffer /* IPC buf cap  */
    );
    ZF_LOGF_IF(error != seL4_NoError, "Failed to configure new TCB");

    /* Setting priority to minimal ensures the thread only starts running once the original thread gives up control */
    error = seL4_TCB_SetPriority(tcb, seL4_CapInitThreadTCB, seL4_MinPrio);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to set the priority for new TCB object");

    seL4_UserContext ctx = {0};

    /* Registers are empty, only set stack/thread pointer and PC */
    ctx.pc = (seL4_Word)(&kernel_thread_target);
    ctx.sp = (seL4_Word)(stack) + sizeof(stack);
    ctx.tp = (seL4_Word)(stack) + sizeof(stack);

    error = seL4_TCB_WriteRegisters(tcb, 0, 0, 3, &ctx);
    ZF_LOGF_IF(error != seL4_NoError, "CPU context for new thread could not be set");

    printf("Set up new TCB\n");

    /* Schedule the new thread */
    error = seL4_TCB_Resume(tcb);
    ZF_LOGF_IF(error != seL4_NoError, "Thread could not be scheduled");

    printf("Scheduled new thread\n\n");
}

void kernel_thread_target(void) {
    printf("Executing in a new kernel thread\n");
    /* Suspend the new thread - not needed anymore */
    seL4_TCB_Suspend(tcb);
}

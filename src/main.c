#include <stdio.h>
#include <stdbool.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>
#include <sel4platsupport/platsupport.h>
#include <utils/util.h>
#include <sel4utils/util.h>
#include <sel4/sel4_arch/mapping.h>
#include <seccells/scthreads.h>
#include <seccells/seccells.h>

#include "alloc.h"

#define RUN_TEST(func, args...)                                                         \
    do {                                                                                \
        printf("###################### Starting " #func " ######################\n");   \
        func(args);                                                                     \
        printf("###################### Finished " #func " ######################\n\n"); \
    } while (0)

#define boolstr(x) \
    ((x) ? "true" : "false")

#define BASE_VADDR    0xA000000
#define CONTEXT_VADDR 0xF000000
#define NUM_RANGES  4   /* Number of additionally created ranges for tests */
#define NUM_SECDIVS 4   /* Number of userspace SecDivs (including the initially running SecDiv) */

void setup_secdivs(void);
void revoke_secdivs(void);
void print_test(void);
void permission_test(seL4_BootInfo *info);
void sdswitch_test(void);
void jump_target(void);
void kernel_thread_creation_test(seL4_BootInfo *info);
void kernel_thread_target(void);
void user_thread__switch_target(void);
void userspace_thread_switch_test(seL4_BootInfo *info);
void *user_thread__call_target(void);
void *chain_scthreads_call(void *args);
void userspace_thread_call_test(seL4_BootInfo *info);
void compile_test(void);

seL4_Word stack[4096];
seL4_CPtr tcb;

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
    debug_print_bootinfo(info);

    /* Create and initialize SecDivs for later use */
    setup_secdivs();

    /* Test availability of terminal/serial output */
    RUN_TEST(print_test);
    /* Test mapping and unmapping of ranges, granting and revoking permissions to/from SecDivs */
    RUN_TEST(permission_test, info);
    /* Test switching between SecDivs */
    RUN_TEST(sdswitch_test);
    /* Test creating a new kernel thread */
    RUN_TEST(kernel_thread_creation_test, info);
    /* Test creating new userspace threads */
    scthreads_init_contexts(info, (void *)CONTEXT_VADDR, NUM_SECDIVS + 1);
    RUN_TEST(userspace_thread_switch_test, info);
    RUN_TEST(userspace_thread_call_test, info);

    /* Revoke SecDiv permissions */
    revoke_secdivs();

    printf("Success!\n\n");

    /* Suspend the root server - isn't needed anymore */
    seL4_TCB_Suspend(seL4_CapInitThreadTCB);

    /* Only make sure all the instructions are compiling/successfully assembled - this code is never actually reached
       and errors (if present) are actually likely to already occur in the earlier tests */
    RUN_TEST(compile_test);

    return 0;
}

void setup_secdivs(void) {
    /* Save current SecDiv ID => probably the initial SecDiv anyway */
    csrr_usid(secdivs[0].id);
    /* Create new SecDivs */
    for (int i = 1; i < NUM_SECDIVS; i++) {
        secdivs[i] = seL4_RISCV_RangeTable_AddSecDiv(seL4_CapInitThreadVSpace);
        ZF_LOGF_IF(secdivs[i].error != seL4_NoError, "Failed to create new SecDiv");
        printf("Created new SecDiv with ID %d\n", secdivs[i].id);
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

void print_test(void) {
    printf("Hello world!\n");
}

void permission_test(seL4_BootInfo *info) {
    seL4_Error error;
    /* At least two ranges are needed further down in the test */
    assert(NUM_RANGES >= 2);
    seL4_CPtr ranges[NUM_RANGES];

    /* Allocate and map ranges read-only */
    for (int i = 0; i < NUM_RANGES; i++) {
        ranges[i] = alloc_object(info, seL4_RISCV_RangeObject, BIT(seL4_MinRangeBits));
        /* Map a read-only 4k range at BASE_VADDR + (i * 0x1000)  */
        uint64_t vaddr = BASE_VADDR + (i * 0x1000);
        error = seL4_RISCV_Range_Map(ranges[i], seL4_CapInitThreadVSpace, vaddr, seL4_CanRead,
                                     seL4_RISCV_Default_VMAttributes);
        ZF_LOGF_IF(error != seL4_NoError, "Failed to map range @ %p", (void *)vaddr);

        /* Print the mapping */
        seL4_RISCV_Range_GetAddress_t addr = seL4_RISCV_Range_GetAddress(ranges[i]);
        ZF_LOGF_IF(addr.error != seL4_NoError, "Failed to retrieve physical address");
        printf("Range %d: %p => %p\n", (i + 1), (void *)vaddr, (void *)addr.paddr);
    }

    /* Check read access */
    seL4_Word *x = (seL4_Word *) BASE_VADDR;
    for (int i = 0; i < NUM_RANGES; i++) {
        seL4_Word *y = x + (i * 0x1000 / sizeof(seL4_Word));
        printf("Read %p: %lu\n", y, *y);
    }

    /* Remap first range read-write */
    error = seL4_RISCV_Range_Unmap(ranges[0]);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to unmap range");
    error = seL4_RISCV_Range_Map(ranges[0], seL4_CapInitThreadVSpace, BASE_VADDR,
                                 seL4_ReadWrite, seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to map range");

    /* Check write access */
    *x = 5;
    printf("Set %p to 5\n", x);
    printf("Read %p: %lu\n", x, *x);
    assert(5 == *x);

    /* Check exclusive write access */
    unsigned int tmp;
    excl(tmp, x, RT_W);
    bool exclusive = (0 == tmp);
    printf("Exclusive access (write on %p): %s\n", x, boolstr(exclusive));
    assert(exclusive);

    /* Invalidate the first cell */
    inval(x);
    printf("After invalidation\n");

    /* Revalidate the first cell read-write */
    reval(x, RT_R | RT_W);
    printf("After revalidation: ");
    printf("Read %p: %lu\n", x, *x);
    printf("Set %p to 10\n", x);
    /* Value shouldn't have changed and write access hould succeed */
    assert(5 == *x);
    *x = 10;
    assert(10 == *x);

    /* Retrieve the current SecDiv's ID */
    unsigned int initial_secdiv;
    csrr_usid(initial_secdiv);
    printf("Currently running in SecDiv %d\n", initial_secdiv);

    /* Grant read-write-execute permissions on the code cell to another SecDiv
       Write is necessary due to the stack also being part of this cell by default and printf trying to put data onto
       the stack */
    seL4_RISCV_RangeTable_AddSecDiv_t secdiv = secdivs[NUM_SECDIVS - 1];

    grant(&main, secdiv.id, RT_R | RT_W | RT_X);
    tfer(BASE_VADDR + 0x1000, secdiv.id, RT_R);
    printf("After granting and transferring permissions to SecDiv %d\n", secdiv.id);

    /* Check exclusive execute access => should not be exclusive */
    excl(tmp, &main, RT_X);
    exclusive = (0 == tmp);
    printf("Exclusive access (execute on %p): %s\n", &main, boolstr(exclusive));
    assert(!exclusive);

    /* Drop write permissions on the first cell => reading should still succeed */
    prot(BASE_VADDR, RT_R | RT_W);
    printf("After dropping permissions: ");
    printf("Read %p: %lu\n", x, *x);
    printf("Set %p to 20\n", x);
    /* Should fail */
    *x = 20;

    /* Revoke permissions for the SecDiv granted earlier on */
    error = seL4_RISCV_RangeTable_RevokeSecDiv(seL4_CapInitThreadVSpace, secdiv.id);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to revoke SecDiv permissions");

    /* Unmap previously mapped ranges */
    for (int i = 0; i < NUM_RANGES; i++) {
        error = seL4_RISCV_Range_Unmap(ranges[0]);
        ZF_LOGF_IF(error != seL4_NoError, "Failed to unmap range");
    }
}

void sdswitch_test(void) {
    seL4_Error error;

    /* At least 2 additional SecDivs are needed further down in the test */
    assert(NUM_SECDIVS > 2);

    /* Retrieve the current SecDiv's ID */
    unsigned int initial_secdiv;
    csrr_usid(initial_secdiv);
    printf("Currently running in SecDiv %d\n", initial_secdiv);

    /* Switch SecDivs back and forth */
    unsigned int usid;
    grant(&sdswitch_test, secdivs[1].id, RT_R | RT_W | RT_X);
    jals(secdivs[1].id, sd1);
sd1:
    entry();
    csrr_usid(usid);
    printf("After switching to SecDiv %d via immediate\n", usid);

    jals(initial_secdiv, sd2);
sd2:
    entry();
    csrr_usid(usid);
    printf("After switching to SecDiv %d via immediate\n", usid);

    register uint64_t ret_addr asm("ra");
    jalrs(ret_addr, &&sd3, secdivs[1].id);
sd3:
    entry();
    csrr_usid(usid);
    printf("After switching to SecDiv %d via register\n", usid);

    /* Switch SecDiv into another function and return from it */
    grant(&jump_target, secdivs[2].id, RT_R | RT_W | RT_X);
    jalrs(ret_addr, &jump_target, secdivs[2].id);

    jalrs(ret_addr, &&sd4, initial_secdiv);
sd4:
    entry();
    csrr_usid(usid);
    printf("After switching to SecDiv %d via register\n", usid);

    revoke_secdivs();
}

void __attribute__((naked)) jump_target(void) {
    entry();
    /* Prologue */
    asm (
        "addi sp, sp, -8 \n\t"
        "sd ra, 0(sp)"
        );
    /* Function body */
    unsigned int usid;
    csrr_usid(usid);
    printf("Jump test into SecDiv %d\n", usid);
    /* Epilogue */
    asm (
        "ld ra, 0(sp)   \n\t"
        "addi sp, sp, 8 \n\t"
        "ret"
        );
}

void kernel_thread_creation_test(seL4_BootInfo *info) {
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

    /* Schedule the new thread */
    error = seL4_TCB_Resume(tcb);
    ZF_LOGF_IF(error != seL4_NoError, "Thread could not be scheduled");
}

void kernel_thread_target(void) {
    printf("Executing in a new kernel thread\n");
    RUN_TEST(print_test);
    /* Suspend the new thread - not needed anymore */
    seL4_TCB_Suspend(tcb);
}

void user_thread_switch_target(void) {
    printf("Executing in a new userspace thread\n");
    RUN_TEST(print_test);
    /* Switch back to initial SecDiv */
    scthreads_switch(secdivs[0].id);
}

void userspace_thread_switch_test(seL4_BootInfo *info) {
    seL4_Error error;

    /* At least 1 additional SecDiv is needed further down in the test */
    assert(NUM_SECDIVS > 1);

    scthreads_set_thread_entry(secdivs[NUM_SECDIVS - 1].id, &user_thread_switch_target);

    scthreads_switch(secdivs[NUM_SECDIVS - 1].id);

    printf("Executing in initial userspace thread\n");
}

void *user_thread_call_target(void *args) {
    printf("Executing in a new userspace thread\n");
    RUN_TEST(print_test);
    /* Return to previous SecDiv */
    scthreads_return(NULL);
}

void *chain_scthreads_call(void *args) {
    int usid = *(int *)args;
    grant(&user_thread_call_target, usid, RT_R | RT_W | RT_X);
    scthreads_call(usid, &user_thread_call_target, NULL);
    /* Return to previous SecDiv */
    scthreads_return((void *)42);
}

void userspace_thread_call_test(seL4_BootInfo *info) {
    seL4_Error error;

    /* At least 2 additional SecDivs are needed further down in the test */
    assert(NUM_SECDIVS > 2);

    int chain_usid = secdivs[NUM_SECDIVS / 2].id;
    void *ret = scthreads_call(secdivs[NUM_SECDIVS - 1].id, &chain_scthreads_call, (void *)&chain_usid);

    printf("Executing in initial userspace thread\n");

    /* scthreads_return should have passed return value 42 on to the caller */
    assert((seL4_Word)ret == 42);
    printf("Retval: %p\n", ret);
}

void compile_test(void) {
    /* Compile check - code should not actually be executed */
    asm volatile (
        "js x0, 0           \n\t"
        "jals x0, 0         \n\t"
        "jrs x0, x0         \n\t"
        "jalrs x0, x0, x0   \n\t"
        "entry              \n\t"
        "inval x0           \n\t"
        "reval x0, x0       \n\t"
        "prot x0, x0        \n\t"
        "grant x0, x0, 0    \n\t"
        "tfer x0, x0, 0     \n\t"
        "excl x0, x0, x0   \n\t"
    );
}

#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>
#include <sel4platsupport/platsupport.h>
#include <utils/util.h>
#include <sel4/sel4_arch/mapping.h>

#include "alloc.h"
#include "seccells.h"

#define TEST_VADDR 0xA000000

void assembly_compile_check(void) {
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
        "count x0, x0, x0   \n\t"
    );
}

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

    seL4_Error error;
    seL4_CPtr range = alloc_object(info, seL4_RISCV_RangeObject, seL4_MinRangeBits);
    seL4_CPtr range2 = alloc_object(info, seL4_RISCV_RangeObject, seL4_MinRangeBits);
    seL4_CPtr range3 = alloc_object(info, seL4_RISCV_RangeObject, seL4_MinRangeBits);
    seL4_CPtr range4 = alloc_object(info, seL4_RISCV_RangeObject, seL4_MinRangeBits);

    /* Map a read-only range at TEST_VADDR */
    error = seL4_RISCV_Range_Map(range, seL4_CapInitThreadVSpace, TEST_VADDR, seL4_CanRead,
                                 seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to map range");
    error = seL4_RISCV_Range_Map(range2, seL4_CapInitThreadVSpace, TEST_VADDR + 0x1000, seL4_ReadWrite,
                                 seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to map range");
    error = seL4_RISCV_Range_Map(range3, seL4_CapInitThreadVSpace, TEST_VADDR + 0x2000, seL4_CanRead,
                                 seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to map range");
    error = seL4_RISCV_Range_Map(range4, seL4_CapInitThreadVSpace, TEST_VADDR + 0x3000, seL4_ReadWrite,
                                 seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to map range");

    seL4_RISCV_Range_GetAddress_t addr = seL4_RISCV_Range_GetAddress(range);
    ZF_LOGF_IF(addr.error != seL4_NoError, "Failed to retrieve physical address");
    printf("Range 1: %p\n", (void *)addr.paddr);
    seL4_RISCV_Range_GetAddress_t addr2 = seL4_RISCV_Range_GetAddress(range2);
    ZF_LOGF_IF(addr2.error != seL4_NoError, "Failed to retrieve physical address");
    printf("Range 2: %p\n", (void *)addr2.paddr);
    seL4_RISCV_Range_GetAddress_t addr3 = seL4_RISCV_Range_GetAddress(range3);
    ZF_LOGF_IF(addr3.error != seL4_NoError, "Failed to retrieve physical address");
    printf("Range 3: %p\n", (void *)addr3.paddr);
    seL4_RISCV_Range_GetAddress_t addr4 = seL4_RISCV_Range_GetAddress(range4);
    ZF_LOGF_IF(addr4.error != seL4_NoError, "Failed to retrieve physical address");
    printf("Range 4: %p\n", (void *)addr4.paddr);

    seL4_Word *x = (seL4_Word *) TEST_VADDR;
    printf("Read x: %lu\n", *x);

    error = seL4_RISCV_Range_Unmap(range);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to unmap range");
    error = seL4_RISCV_Range_Map(range, seL4_CapInitThreadVSpace, TEST_VADDR,
                                 seL4_ReadWrite, seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to map range");

    error = seL4_RISCV_RangeTable_Compact(seL4_CapInitThreadVSpace);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to compact range table");

    /* Write to the range we mapped */
    *x = 5;
    printf("Set x to 5\n");

    /* Check that writing to the mapped range actually worked */
    printf("Read x: %lu\n", *x);
    printf("Read *(&x + 0x1000): %lu\n", *(seL4_Word *)(TEST_VADDR + 0x1000));
    printf("Read *(&x + 0x2000): %lu\n", *(seL4_Word *)(TEST_VADDR + 0x2000));
    printf("Read *(&x + 0x3000): %lu\n", *(seL4_Word *)(TEST_VADDR + 0x3000));


    /* Retrieve the current SecDiv's ID */
    unsigned int initial_secdiv = -1u;
    csrr_usid(initial_secdiv);
    printf("Currently running in SecDiv %d\n", initial_secdiv);

    /* Create and delete a SecDiv */
    seL4_RISCV_RangeTable_AddSecDiv_t secdiv = seL4_RISCV_RangeTable_AddSecDiv(seL4_CapInitThreadVSpace);
    ZF_LOGF_IF(secdiv.error != seL4_NoError, "Failed to create new SecDiv");
    printf("Created new SecDiv with ID %d\n", secdiv.id);

    error = seL4_RISCV_RangeTable_RemoveSecDiv(seL4_CapInitThreadVSpace, secdiv.id);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to delete SecDiv");

    /* Count SecDivs with write access => should be exactly one (rootserver) */
    int count = 0;
    uint64_t test_vaddr = TEST_VADDR;
    count(count, test_vaddr, RT_W);
    printf("Count (write on %p): %d\n", (void *)test_vaddr, count);
    assert(1 == count);

    /* Invalidate the first cell */
    inval(test_vaddr);
    printf("After invalidation\n");

    /* Revalidate the first cell read-write */
    reval(test_vaddr, RT_R | RT_W);
    printf("After revalidation: ");
    printf("Read x: %lu\n", *x);
    printf("Write x = 10\n");
    /* Should succeed */
    *x = 10;

    /* Grant read-write-execute permissions on the code cell to another SecDiv after creating it
       Write is necessary due to the stack also being part of this cell by default and printf trying to put data onto
       the stack */
    secdiv = seL4_RISCV_RangeTable_AddSecDiv(seL4_CapInitThreadVSpace);
    ZF_LOGF_IF(secdiv.error != seL4_NoError, "Failed to create new SecDiv");
    printf("Created new SecDiv with ID %d\n", secdiv.id);

    uint64_t main_addr = (uint64_t)&main;
    grant(main_addr, secdiv.id, RT_R | RT_W | RT_X);
    test_vaddr += 0x1000;
    tfer(test_vaddr, secdiv.id, RT_R | RT_W);
    printf("After granting and transferring permissions to SecDiv %d\n", secdiv.id);

    /* Count SecDivs with execute access => should be exactly two */
    count(count, main_addr, RT_X);
    printf("Count (execute on %p): %d\n", (void *)main_addr, count);
    assert(2 == count);

    /* Switch SecDivs back and forth */
    unsigned int usid = -1u;
    uint64_t secdiv_id = secdiv.id;
    jals(secdiv_id, sd1);
    entry(sd1);
    csrr_usid(usid);
    printf("After switching to SecDiv %d via immediate\n", usid);

    secdiv_id = initial_secdiv;
    jals(secdiv_id, sd2);
    entry(sd2);
    csrr_usid(usid);
    printf("After switching to SecDiv %d via register\n", usid);

    uint64_t ret_addr = 0, dest_addr = (uint64_t)&&sd3;
    jalrs(ret_addr, dest_addr, secdiv.id);
sd3:
    entry(_sd3);
    csrr_usid(usid);
    printf("After switching to SecDiv %d via register\n", usid);

    dest_addr = (uint64_t)&&sd4;
    jalrs(ret_addr, dest_addr, initial_secdiv);
sd4:
    entry(_sd4);
    csrr_usid(usid);
    printf("After switching to SecDiv %d via register\n", usid);

    /* Drop write permissions on the first cell => reading should still succeed */
    test_vaddr = TEST_VADDR;
    prot(test_vaddr, RT_R);
    printf("After dropping permissions: ");
    printf("Read x: %lu\n", *x);
    printf("Write x = 20\n");
    /* Should fail */
    *x = 20;

    printf("Success!\n");

    /* Suspend the root server - isn't needed anymore */
    seL4_TCB_Suspend(seL4_CapInitThreadTCB);

    /* Only make sure the instructions are compiling - this code is never actually reached */
    assembly_compile_check();

    return 0;
}

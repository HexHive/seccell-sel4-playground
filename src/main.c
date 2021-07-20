#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>
#include <sel4platsupport/platsupport.h>
#include <utils/util.h>
#include <sel4/sel4_arch/mapping.h>

#include "alloc.h"
#include "permissions.h"

#define TEST_VADDR 0xA000000

void assembly_compile_check(void) {
    /* Compile check - code should not actually be executed */
    asm volatile (
        "js x0, 2000        \n\t"
        "jrs x0, 1000(x0)   \n\t"
        "jrs x0, x0         \n\t"
        "entry              \n\t"
        "inval x0           \n\t"
        "reval x0, x0       \n\t"
        "grant x0, x0, x0   \n\t"
        "prot x0, x0        \n\t"
        "tfer x0, x0, x0    \n\t"
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

    /* Write to the range we mapped */
    *x = 5;
    printf("Set x to 5\n");

    /* Check that writing to the mapped range actually worked */
    printf("Read x: %lu\n", *x);
    printf("Read *(&x + 0x1000): %lu\n", *(seL4_Word *)(TEST_VADDR + 0x1000));
    printf("Read *(&x + 0x2000): %lu\n", *(seL4_Word *)(TEST_VADDR + 0x2000));
    printf("Read *(&x + 0x3000): %lu\n", *(seL4_Word *)(TEST_VADDR + 0x3000));

    error = seL4_RISCV_RangeTable_Compact(seL4_CapInitThreadVSpace);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to compact range table");

    /* Count SecDivs with write access => should be exactly one (rootserver) */
    int count = 0, perms = RT_W;
    asm(
        "count %[acount], %[addr], %[aperms]"
        : [acount] "=r" (count)
        : [addr] "p" (TEST_VADDR), [aperms] "r" (perms)
    );
    printf("Count: %d\n", count);

    /* Invalidate the first cell */
    asm(
        "inval %[addr]"
        :
        : [addr] "p" (TEST_VADDR)
    );
    printf("After invalidation\n");

    /* Revalidate the first cell read-write */
    perms = RT_R | RT_W;
    asm(
        "reval %[addr], %[aperms]"
        :
        : [addr] "p" (TEST_VADDR), [aperms] "r" (perms)
    );
    printf("After revalidation: ");
    printf("Read x: %lu\n", *x);
    printf("Write x = 10\n");
    /* Should succeed */
    *x = 10;

    /* Grant read-write-execute permissions on the code cell to another SecDiv
       Write is necessary due to the stack also being part of this cell by default and printf trying to put data onto
       the stack */
    perms = RT_R | RT_W | RT_X;
    asm(
        "addi t0, zero, 2               \n\t"
        "grant %[addr], t0, %[aperms]   \n\t"
        :
        : [addr] "p" (&main), [aperms] "r" (perms)
    );
    printf("After granting permissions to SecDiv 2\n");

    /* Switch SecDivs back and forth */
    unsigned int usid = 0;
    asm(
        "li t0, 2               \n\t"
        "js t0, 4               \n\t"
        "entry                  \n\t"
        "csrr %[ausid], usid    \n\t"
        : [ausid] "=r" (usid)
        :
    );
    printf("After switching to SecDiv %d via immediate\n", usid);
    asm(
        "li t0, 1               \n\t"
        "js t0, 4               \n\t"
        "entry                  \n\t"
        "csrr %[ausid], usid    \n\t"
        : [ausid] "=r" (usid)
        :
    );
    printf("After switching to SecDiv %d via immediate\n", usid);

    asm(
        "li t0, 2               \n\t"
        "la t1, sdswitch1       \n\t"
        "jrs t0, 0(t1)          \n\t"
        "nop                    \n\t"
        "nop                    \n\t"
        "nop                    \n\t"
        "nop                    \n\t"
        "nop                    \n\t"
        "nop                    \n\t"
        "nop                    \n"
        "sdswitch1:             \n\t"
        "entry                  \n\t"
        "csrr %[ausid], usid    \n\t"
        : [ausid] "=r" (usid)
        :
    );
    printf("After switching to SecDiv %d via register\n", usid);
    asm(
        "li t0, 1               \n\t"
        "la t1, sdswitch2       \n\t"
        "jrs t0, 0(t1)          \n\t"
        "nop                    \n\t"
        "nop                    \n\t"
        "nop                    \n\t"
        "nop                    \n\t"
        "nop                    \n\t"
        "nop                    \n\t"
        "nop                    \n"
        "sdswitch2:             \n\t"
        "entry                  \n\t"
        "csrr %[ausid], usid    \n\t"
        : [ausid] "=r" (usid)
        :
    );
    printf("After switching to SecDiv %d via register\n", usid);

    /* Drop write permissions on the first cell => reading should still succeed */
    perms = RT_R;
    asm(
        "prot %[addr], %[aperms]"
        :
        : [addr] "p" (TEST_VADDR), [aperms] "r" (perms)
    );
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

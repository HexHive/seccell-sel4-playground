#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>
#include <sel4platsupport/platsupport.h>
#include <utils/util.h>
#include <sel4/sel4_arch/mapping.h>

#include "alloc.h"

#define TEST_VADDR 0xA000000

int main(int argc, char *argv[]) {
    /* Parse the location of the seL4_BootInfo data structure from
       the environment variables set up by the default crt0.S */
    seL4_BootInfo *info = platsupport_get_bootinfo();
    /* Setup serial output via seL4_Debug_PutChar */
    if (platsupport_serial_setup_bootinfo_failsafe()) {
        /* Error occured during setup => terminate */
        return 1;
    }

    seL4_Error error;
    seL4_CPtr range = alloc_object(info, seL4_RISCV_RangeObject, seL4_MinRangeBits);
    seL4_CPtr range2 = alloc_object(info, seL4_RISCV_RangeObject, seL4_MinRangeBits);
    seL4_CPtr range3 = alloc_object(info, seL4_RISCV_RangeObject, seL4_MinRangeBits);
    seL4_CPtr range4 = alloc_object(info, seL4_RISCV_RangeObject, seL4_MinRangeBits);

    /* Map a read-only range at TEST_VADDR */
    error = seL4_RISCV_Range_Map(range, seL4_CapInitThreadVSpace, TEST_VADDR, seL4_CanRead, seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to map range");
    error = seL4_RISCV_Range_Map(range2, seL4_CapInitThreadVSpace, TEST_VADDR + 0x1000, seL4_ReadWrite, seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to map range");
    error = seL4_RISCV_Range_Map(range3, seL4_CapInitThreadVSpace, TEST_VADDR + 0x2000, seL4_ReadWrite, seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to map range");
    error = seL4_RISCV_Range_Map(range4, seL4_CapInitThreadVSpace, TEST_VADDR + 0x3000, seL4_ReadWrite, seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to map range");

    seL4_Word *x = (seL4_Word *) TEST_VADDR;
    printf("Read x: %lu\n", *x);

    error = seL4_RISCV_Range_Unmap(range);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to unmap range");
    error = seL4_RISCV_Range_Map(range, seL4_CapInitThreadVSpace, TEST_VADDR, seL4_ReadWrite, seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to map range");

    /* Write to the range we mapped */
    *x = 5;
    printf("Set x to 5\n");

    /* Check that writing to the mapped range actually worked */
    printf("Read x: %lu\n", *x);

    printf("Success!\n");

    while (1);

    return 0;
}

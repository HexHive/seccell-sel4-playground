#include "mmap_override.h"

#include <sel4/sel4.h>
#include <sel4/sel4_arch/mapping.h>
#include <sel4platsupport/bootinfo.h>
#include <sel4platsupport/platsupport.h>
#include <autoconf.h>

#include "alloc.h"

void *mmap_override(void *start, size_t len, int prot, int flags, int fd, off_t off) {
    /*
     * We just ignore any flags here and map a range with the requested size read-write accessible.
     * This is hacky, but good enough for now.
     * Ideally, we would have a dynamic allocator instead of the static allocation that mmap is using
     * (morecore section in the binary). However, this requires more modifications to the seL4 libraries
     * so that they can actually map new memory in.
     */
    seL4_BootInfo *info = platsupport_get_bootinfo();
    static void *addr = (void *)MMAP_BASE;
    void *ret = addr;
    seL4_Error error;
#ifdef CONFIG_RISCV_SECCELL
    seL4_CPtr range = alloc_object(info, seL4_RISCV_RangeObject, len);
    error = seL4_RISCV_Range_Map(range, seL4_CapInitThreadVSpace, (seL4_Word)addr, seL4_ReadWrite,
                                 seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to map range @ %p", addr);
    addr = (void *)((char *)addr + len);
#else
    /* Map page table structures */
    for (; (size_t)addr < (size_t)ret + len; addr += BIT(seL4_PageTableBits + seL4_PageTableIndexBits)) {
        seL4_CPtr pagetable = alloc_object(info, seL4_RISCV_PageTableObject, seL4_PageTableBits);
        error = seL4_RISCV_PageTable_Map(pagetable, seL4_CapInitThreadVSpace, (seL4_Word)addr,
                                         seL4_RISCV_Default_VMAttributes);
    }
    /* Map actual pages */
    for (addr = ret; (size_t)addr < (size_t)ret + len; addr += BIT(seL4_PageBits)) {
        seL4_CPtr page = alloc_object(info, seL4_RISCV_4K_Page, seL4_PageBits);
        error = seL4_RISCV_Page_Map(page, seL4_CapInitThreadVSpace, (seL4_Word)addr, seL4_ReadWrite,
                                    seL4_RISCV_Default_VMAttributes);
        ZF_LOGF_IF(error != seL4_NoError, "Failed to map page @ %p", addr);
    }
#endif /* CONFIG_RISCV_SECCELL */
    return ret;
}

#include "mmap_override.h"

#include <autoconf.h>
#include <sel4/sel4.h>
#include <sel4/sel4_arch/mapping.h>
#include <sel4platsupport/bootinfo.h>
#include <sel4platsupport/platsupport.h>

#include "alloc.h"
#include <sys/mman.h>

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
#ifdef CONFIG_RISCV_SECCELL
    len = ROUND_UP(len, BIT(seL4_MinRangeBits));
#else
    len = ROUND_UP(len, BIT(seL4_PageBits));
#endif /* CONFIG_RISCV_SECCELL */
    if (start == NULL) {
        /* No start address passed => use the available address range from MMAP_BASE on */
        start = addr;
        addr = (void *)((char *)addr + len);
    }
    void *ret = start;
    seL4_Error error;
#ifdef CONFIG_RISCV_SECCELL
    seL4_RISCV_VMAttributes vmattr = (prot & PROT_EXEC)? 
                                        seL4_RISCV_Default_VMAttributes: 
                                        seL4_RISCV_ExecuteNever;
    seL4_CapRights_t perms;
    if(prot & PROT_READ) 
        perms = (prot & PROT_WRITE)? seL4_ReadWrite: seL4_CanRead;
    else 
        perms = (prot & PROT_WRITE)? seL4_CanWrite: seL4_NoRights;

    seL4_CPtr range = alloc_object(info, seL4_RISCV_RangeObject, len);
    error = seL4_RISCV_Range_Map(range, seL4_CapInitThreadVSpace, (seL4_Word)start, perms,
                                 vmattr);
    if (unlikely(error != seL4_NoError)) {
        ZF_LOGF("Failed to map range @ %p", start);
        return NULL;
    }
#else
    /* Map page table structures */
    for (; (size_t)start < (size_t)ret + len; start += BIT(seL4_PageTableBits)) {
        error = seL4_NoError;
        while (error == seL4_NoError) {
            seL4_CPtr pagetable = alloc_object(info, seL4_RISCV_PageTableObject, seL4_PageTableBits);
            error = seL4_RISCV_PageTable_Map(pagetable, seL4_CapInitThreadVSpace, (seL4_Word)start,
                                             seL4_RISCV_Default_VMAttributes);
            ZF_LOGF_IF(error != seL4_NoError && error != seL4_DeleteFirst, "Failed to map page table @ %p", start)
        }
    }
    /* Map actual pages */
    for (start = ret; (size_t)start < (size_t)ret + len; start += BIT(seL4_PageBits)) {
        seL4_CPtr page = alloc_object(info, seL4_RISCV_4K_Page, seL4_PageBits);
        error = seL4_RISCV_Page_Map(page, seL4_CapInitThreadVSpace, (seL4_Word)start, seL4_ReadWrite,
                                    seL4_RISCV_Default_VMAttributes);
        ZF_LOGF_IF(error != seL4_NoError, "Failed to map page @ %p", start)
    }
#endif /* CONFIG_RISCV_SECCELL */
    return ret;
}

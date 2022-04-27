#include "sc_mmap.h"

#include <sel4/sel4.h>
#include <sel4/sel4_arch/mapping.h>
#include <sel4platsupport/bootinfo.h>
#include <sel4platsupport/platsupport.h>

#include "alloc.h"

void *sc_mmap(void *start, size_t len, int prot, int flags, int fd, off_t off) {
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
    seL4_CPtr range = alloc_object(info, seL4_RISCV_RangeObject, len);
    error = seL4_RISCV_Range_Map(range, seL4_CapInitThreadVSpace, (seL4_Word)addr, seL4_ReadWrite,
                                 seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error != seL4_NoError, "Failed to map range @ %p", addr);
    addr = (void *)((char *)addr + len);
    return ret;
}

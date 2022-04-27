#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <autoconf.h>
#include "config.h"
#include "cache.h"
#include "slabs.h"

static slabclass_t slabclass[MAX_NUMBER_OF_SLAB_CLASSES];
static const uint32_t slab_page_size = 8 * 1024 * 1024;

#ifdef CONFIG_RISCV_SECCELL
#include <sel4/sel4.h>
#include <sel4/sel4_arch/mapping.h>
#include <sel4platsupport/bootinfo.h>
#include <sel4platsupport/platsupport.h>
#include "alloc.h"

#define MMAP_BASE 0xA000000
/* Override mmap */
#define mmap sc_mmap

static void *sc_mmap(void *start, size_t len, int prot, int flags, int fd, off_t off) {
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
#endif /* CONFIG_RISCV_SECCELL */

static void do_slabs_free(void *ptr, const size_t size, unsigned int id) {
  slabclass_t *p;
  item *it;

  p = &slabclass[id];

  it = (item *)ptr;
  it->prev = 0;
  it->next = p->slots;
  if (it->next) it->next->prev = it;
  p->slots = it;

  p->sl_curr++;
}


static void split_slab_page_into_freelist(char *ptr, const unsigned int id) {
    slabclass_t *p = &slabclass[id];
    int x;
    for (x = 0; x < p->perslab; x++) {
        do_slabs_free(ptr, 0, id);
        ptr += p->size;
    }
}

static int grow_slab_list (const unsigned int id) {
    slabclass_t *p = &slabclass[id];
    if (p->slabs == p->list_size) {
        size_t new_size =  (p->list_size != 0) ? p->list_size * 2 : 16;
        void *new_list = realloc(p->slab_list, new_size * sizeof(void *));
        if (new_list == 0) return -1;
        p->list_size = new_size;
        p->slab_list = new_list;
    }
    return 1;
}

static int slab_preallocate(int id) {
  slabclass_t *slab = &slabclass[id];
  void *slabarea = mmap(NULL, slab_page_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
  if(!slabarea)
    return -1;
  memset(slabarea, 0, slab_page_size);

  if(grow_slab_list(id) < 0) {
    munmap(slabarea, slab_page_size);
    return -1;
  }

  split_slab_page_into_freelist(slabarea, id);

  slab->slab_list[slab->slabs++] = slabarea;
}

unsigned int slabs_clsid(const size_t size) {
  int res = 0;

  if (size == 0)
    return -1;
  while (size > slabclass[res].size) {
    if((res > MAX_NUMBER_OF_SLAB_CLASSES) || (slabclass[++res].size == 0))
      return -1;
  }
  return res;
}

/*************** External API *********************************/
void slab_init() {
  const double factor = 2;
  const uint32_t min_size = 32;
  const uint32_t max_size = 16*1024;

  uint32_t size = min_size;
  int i;

  memset(slabclass, 0, sizeof(slabclass));

  for(i = 0; i < MAX_NUMBER_OF_SLAB_CLASSES; i++) {
    if(size > max_size)
      break;

    slabclass[i].size = size;
    slabclass[i].perslab = slab_page_size / size;

    size *= factor;
  }

  /* Prealloc 64B slab only ;) */
  int id = slabs_clsid(64);
  slab_preallocate(id);
}

void *slabs_alloc(int size) {
  slabclass_t *p;
  void *ret = NULL;
  item *it = NULL;
  unsigned id;

  if((id = slabs_clsid(size)) < 0)
    return NULL;

  p = &slabclass[id];

  /* TODO */
  /* No free blocks?! Actually should reallocate. Now, won't bother */
  if (p->sl_curr == 0)
    return NULL;

  it = (item *)p->slots;
  p->slots = it->next;
  if (it->next) it->next->prev = 0;
  p->sl_curr--;
  it->slabs_clsid = id;

  return it;
}

void slabs_free(item *it) {
  slabclass_t *p;

  unsigned id;

  p = &slabclass[it->slabs_clsid];
  it->next = p->slots;
  if (it->next) it->next->prev = it;
  p->slots = it;
  p->sl_curr++;
}

uint64_t slabs_memory_used() {
  uint64_t storage = 0;
  uint64_t total_in_slab, remain_in_slab;
  slabclass_t *slab;

  for(int id = 0; id < MAX_NUMBER_OF_SLAB_CLASSES; id++) {
    if(slabclass[id].size == 0)
      continue;
    slab = &slabclass[id];
    total_in_slab = slab->slabs * (uint64_t)slab_page_size;
    remain_in_slab = slab->sl_curr * (uint64_t)slab->size;

    storage += (total_in_slab - remain_in_slab);
  }
  return storage;
}
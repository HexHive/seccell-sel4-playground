#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <autoconf.h>
#include "slabs.h"
#include "cache.h"
#include "hash.h"
#ifdef CONFIG_RISCV_SECCELL
#include <sel4/sel4.h>
#include <sys/mman.h>
#include <sel4utils/util.h>
#include <sel4platsupport/platsupport.h>
#include <seccells/seccells.h>
#include "sc_mmap.h"
#endif /* CONFIG_RISCV_SECCELL */

#define hashsize(n) ((uint32_t)1<<(n))
#define hashmask(n) (hashsize(n)-1)
#define MAX(x, y)   (((x) > (y))? (x): (y))
#define MIN(x, y)   (((x) < (y))? (x): (y))

static item **hashtable = NULL;
static unsigned hashpower = 10;
hash_func hash = jenkins_hash;

#ifdef CONFIG_RISCV_SECCELL
/* SecDiv IDs get initialized in the code */
static unsigned int cache_secdiv = 0;
static unsigned int request_secdiv = 0;
#endif /* CONFIG_RISCV_SECCELL */

int cache_init() {
  /* First block: hashtable and (if necessary) SecDiv initialization */
#ifdef CONFIG_RISCV_SECCELL
  /* Create the cache SecDiv */
  seL4_RISCV_RangeTable_AddSecDiv_t ret = seL4_RISCV_RangeTable_AddSecDiv(seL4_CapInitThreadVSpace);
  ZF_LOGF_IF(ret.error != seL4_NoError, "Failed to create new SecDiv");
  cache_secdiv = ret.id;
  /* Store the requesting SecDiv's ID */
  csrr_usid(request_secdiv);

  /*
   * Grant permissions to the new SecDiv so that we can actually switch to it and so that it can map memory
   * To achieve this, it needs access to:
   *   - Code and stack => by default in the same memory range (because that's how seL4 works...)
   *   - The bootinfo range for retyping untyped memory into proper ranges
   *   - The IPC buffer range for issuing syscalls (i.e., memory retyping)
   */
  seL4_Error err = seL4_RISCV_RangeTable_GrantSecDivPermissions(seL4_CapInitThreadVSpace, (seL4_Word)cache_secdiv,
                                                                (seL4_Word)&cache_init, RT_R | RT_W | RT_X);
  ZF_LOGF_IF(err != seL4_NoError, "Failed to grant permissions to new SecDiv");
  seL4_BootInfo *info = platsupport_get_bootinfo();
  err = seL4_RISCV_RangeTable_GrantSecDivPermissions(seL4_CapInitThreadVSpace, (seL4_Word)cache_secdiv,
                                                     (seL4_Word)info, RT_R | RT_W);
  ZF_LOGF_IF(err != seL4_NoError, "Failed to grant permissions to new SecDiv");
  err = seL4_RISCV_RangeTable_GrantSecDivPermissions(seL4_CapInitThreadVSpace, (seL4_Word)cache_secdiv,
                                                     (seL4_Word)info->ipcBuffer, RT_R | RT_W);
  ZF_LOGF_IF(err != seL4_NoError, "Failed to grant permissions to new SecDiv");

  /* Switch to new SecDiv */
  SD_ENTRY(cache_secdiv, cache_init);
  /* Hopefully, there's no overflow in the size calculation */
  size_t sz = hashsize(hashpower) * sizeof(*hashtable);
  hashtable = (item **)mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
  memset((void* )hashtable, 0, sz);
#else
  hashtable = (item **)calloc(hashsize(hashpower), sizeof(*hashtable));
#endif /* CONFIG_RISCV_SECCELL */
  /* Second block: slab initialization */
  slab_init();

  /* Third block: if necessary, return back to calling SecDiv */
#ifdef CONFIG_RISCV_SECCELL
  SD_EXIT(request_secdiv, cache_init);
#endif /* CONFIG_RISCV_SECCELL */
}

/* Find and return the item with key in the hashtable */
static item *item_get(const char *key, int nkey, uint32_t hv) {
  item *it = hashtable[hv & hashmask(hashpower)];

  while(it) {
    if((nkey == it->nkey) && memcmp(key, ITEM_key(it), nkey) == 0)
      break;
    it = it->next;
  }
  return it;
}

/* Generate the item */
static item *item_alloc(const char *key, int nkey, const char *value, int nval) {
  item *it = (item *)slabs_alloc(ITEM_ntotal(nkey, nval));

  if(it) {
    it->next = it->prev = NULL;
    it->nbytes = nval;
    it->nkey = nkey;
    /* Assuming key/value are null-terminated, the +1 size copies over the terminators */
    memcpy(ITEM_key(it), key, nkey + 1);
    memcpy(ITEM_val(it), value, nval + 1);
  }

  return it;
}

static item *item_unlink(const char *key, int nkey, uint32_t hv) {
  item *it, **it_head_ptr;
  it_head_ptr = &hashtable[hv & hashmask(hashpower)];
  it = *it_head_ptr;

  while(it) {
    if((nkey == it->nkey) && memcmp(key, ITEM_key(it), nkey) == 0) {
      /* Remove from list */
      if(it == *it_head_ptr){
        *it_head_ptr = it->next;
        if(it->next)
          it->next->prev = NULL;
      } else {
        it->prev->next = it->next;
        if(it->next)
          it->next->prev =  it->prev;
      }
      break;
    }
    it = it->next;
  }
  return it;
}

/*************** External API *********************************/
int cache_get(const char *key, int nkey, char *value, int maxnval) {
#ifdef CONFIG_RISCV_SECCELL
  SD_ENTRY(cache_secdiv, cache_get);
#endif /* CONFIG_RISCV_SECCELL */
  int nval;
  uint32_t hv = hash(key, nkey);

  item *it = item_get(key, nkey, hv);

  if(!it)
    return -1;

  nval = MIN(it->nbytes, maxnval - 1);
  memcpy(value, ITEM_val(it), nval);
  value[nval] = '\0';
#ifdef CONFIG_RISCV_SECCELL
  SD_EXIT(request_secdiv, cache_get);
#endif /* CONFIG_RISCV_SECCELL */
  return 0;
}

int cache_set(const char *key, int nkey, const char *value, int nval) {
#ifdef CONFIG_RISCV_SECCELL
  SD_ENTRY(cache_secdiv, cache_set);
#endif /* CONFIG_RISCV_SECCELL */
  item *it;
  uint32_t hv = hash(key, nkey);


  /* Remove item if exists, then insert new */
  it = item_unlink(key, nkey, hv);
  if(it)
    slabs_free(it);
  it = item_alloc(key, nkey, value, nval);

  if(!it)
    return -1;

  it->next = hashtable[hv & hashmask(hashpower)];
  if(it->next)
    it->next->prev = it;
  hashtable[hv & hashmask(hashpower)] = it;

#ifdef CONFIG_RISCV_SECCELL
  SD_EXIT(request_secdiv, cache_set);
#endif /* CONFIG_RISCV_SECCELL */
  return 0;
}

void dump_cache() {
#ifdef CONFIG_RISCV_SECCELL
  SD_ENTRY(cache_secdiv, dump_cache);
#endif /* CONFIG_RISCV_SECCELL */
  item *it;
  uint64_t storage = 0;

  for(int i = 0; i < hashsize(hashpower); i++) {
    it = hashtable[i];
    while (it)
    {
      storage += ITEM_ntotal(it->nkey, it->nbytes);
      printf((it->next)? "%s = %.3s, ": "%s = %.3s", ITEM_key(it), ITEM_val(it));
      it = it->next;
    }
    if(hashtable[i])
      printf("\n");
  }
  printf("Used %ld bytes\n", storage);
#ifdef CONFIG_RISCV_SECCELL
  SD_EXIT(request_secdiv, dump_cache);
#endif /* CONFIG_RISCV_SECCELL */
}

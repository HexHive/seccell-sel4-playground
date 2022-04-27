#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "slabs.h"
#include "cache.h"
#include "hash.h"

#define hashsize(n) ((uint32_t)1<<(n))
#define hashmask(n) (hashsize(n)-1)
#define MAX(x, y)   (((x) > (y))? (x): (y))
#define MIN(x, y)   (((x) < (y))? (x): (y))

static item **hashtable = NULL;
static unsigned hashpower = 10;
hash_func hash = jenkins_hash;

int cache_init() {
  hashtable = (item **)calloc(hashsize(hashpower), sizeof(*hashtable));
  slab_init();
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
  int nval;
  uint32_t hv = hash(key, nkey);

  item *it = item_get(key, nkey, hv);

  if(!it)
    return -1;

  nval = MIN(it->nbytes, maxnval - 1);
  memcpy(value, ITEM_val(it), nval);
  value[nval] = '\0';

  return 0;
}

int cache_set(const char *key, int nkey, const char *value, int nval) {
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

  return 0;
}

void dump_cache() {
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
}

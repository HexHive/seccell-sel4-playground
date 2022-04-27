#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>
#include "config.h"

typedef struct _stritem {
  /* Used for linking items into list in bucket */
  struct _stritem *next;
  struct _stritem *prev;
  /* Other information */
  int nbytes;               /* Size of value */
  int nkey;                 /* Size of key */

  uint8_t slabs_clsid; /* which slab class we're in */
} item;

#define ITEM_key(it)                   (((char *)it) + sizeof(item))
#define ITEM_val(it)                   (((char *)it) + sizeof(item) + it->nkey + 1)
#define ITEM_ntotal(nkey, nval)        (sizeof(item) + nkey + 1 + nval + 1)

int cache_init();

/* Our k-v store commands */
int cache_set(const char *key, int nkey, const char *value, int nval);
int cache_get(const char *key, int nkey, char *value, int maxnval);
int flush_all();
/* Future: add/replace/append/prepend/compare-swap */


void dump_cache();

#endif /* CACHE_H */

#ifndef SLABS_H
#define SLABS_H

typedef struct {
  unsigned int size;      /* sizes of items */
  unsigned int perslab;   /* how many items per slab */

  void *slots;           /* list of item ptrs */
  unsigned int sl_curr;   /* total free items in list */

  void **slab_list;       /* array of slab pointers */
  unsigned int list_size; /* capacity of prev array */
  unsigned int slabs;     /* how many slabs were allocated for this class */
} slabclass_t;
typedef struct _stritem item;

void slab_init();
void *slabs_alloc(int size);
void slabs_free(item *it);
uint64_t slabs_memory_used();

#endif /* SLABS_H */

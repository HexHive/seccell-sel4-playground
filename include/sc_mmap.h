#ifndef SC_MMAP_H
#define SC_MMAP_H

#include <sys/types.h>

#define MMAP_BASE 0xA000000
/* Override mmap */
#define mmap sc_mmap

void *sc_mmap(void *start, size_t len, int prot, int flags, int fd, off_t off);

#endif /* SC_MMAP_H */

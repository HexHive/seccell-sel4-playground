#ifndef MMAP_OVERRIDE_H
#define MMAP_OVERRIDE_H

#include <sys/types.h>

#define MMAP_BASE 0xA000000
/* Override mmap */
#define mmap mmap_override

void *mmap_override(void *start, size_t len, int prot, int flags, int fd, off_t off);

#endif /* MMAP_OVERRIDE_H */
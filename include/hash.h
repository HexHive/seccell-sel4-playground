#ifndef HASH_H
#define HASH_H

#include "jenkins_hash.h"

typedef uint32_t (*hash_func)(const void *key, size_t length);
extern hash_func hash;

#endif /* HASH_H */

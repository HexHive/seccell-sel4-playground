#ifndef CACHE_WRAPPER_H
#define CACHE_WRAPPER_H

// #include "mbedtls/aes.h"
#include "mbedtls_fake.h"

mbedtls_aes_context *wrapper_init();
int wrapper_free();
int cache_get_wrapper(char *enc_req, int enc_len, char *enc_rep, int max_enc_repn);

#endif /* CACHE_WRAPPER_H */

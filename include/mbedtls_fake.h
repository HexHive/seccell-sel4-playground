#ifndef MBEDTLS_FAKE_H
#define MBEDTLS_FAKE_H

#include <string.h>
/*
 * This header/source combination only exists to build with the mbedtls functions and structures.
 * To be replaced by actual MbedTLS once we're able to incorporate it into the build system.
 */
#define MBEDTLS_AES_ENCRYPT 0
#define MBEDTLS_AES_DECRYPT 0

typedef char mbedtls_aes_context;

/* Well... this is arguably ugly, but hey, compiling code is more important than correct/nice code, isn't it? */
#define mbedtls_aesni_crypt_ecb mbedtls_aes_crypt_ecb
static inline int mbedtls_aes_crypt_ecb(mbedtls_aes_context *ctx,
                                        int mode,
                                        const unsigned char input[16],
                                        unsigned char output[16])
{
    /* Our "encryption" consists simply of copying the input into the output :) */
    memcpy((void *)output, (void *)input, 16);
    return 0;
}

#define mbedtls_aes_setkey_dec mbedtls_aes_setkey_enc
static inline int mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx, const unsigned char *key, unsigned int keybits) {
    /* Do nothing */
    return 0;
}

static inline void mbedtls_aes_init(mbedtls_aes_context *ctx) {
    /* Do nothing */
    return;
}

static inline void mbedtls_aes_free(mbedtls_aes_context *ctx) {
    /* Do nothing */
    return;
}


#endif /* MBEDTLS_FAKE_H */

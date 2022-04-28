#include <assert.h>
#include "cache_wrapper.h"
#include "cache.h"

#define MAX( x, y ) ( ( x ) > ( y ) ? ( x ) : ( y ) )
#define MIN( x, y ) ( ( x ) < ( y ) ? ( x ) : ( y ) )

static mbedtls_aes_context encctx, decctx;
static char *key = "1234567890abcdef";
mbedtls_aes_context *wrapper_init() {
  cache_init();

  mbedtls_aes_init(&encctx);
  mbedtls_aes_init(&decctx);
  mbedtls_aes_setkey_enc(&encctx, key, 128);
  mbedtls_aes_setkey_dec(&decctx, key, 128);

  return &encctx;
}

#define MAX_MSG_SIZE  256
int cache_get_wrapper(char *enc_req, int enc_len, char *enc_rep, int max_enc_repn) {
  char dec_req[MAX_MSG_SIZE];
  char dec_rep[MAX_MSG_SIZE];
  char bytes_dec = 0, bytes_enc = 0;
  int ret, valn;

  /* Decrypt incoming request */
  while(enc_len > bytes_dec) {
    ret = mbedtls_aesni_crypt_ecb(&decctx, MBEDTLS_AES_DECRYPT,
                                &enc_req[bytes_dec], &dec_req[bytes_dec]);
    if(ret)
      return -1;
    bytes_dec += 16;
  }

  valn = cache_get(&dec_req[sizeof(int)], *(int *)dec_req,
                    &dec_rep[sizeof(int)], MAX_MSG_SIZE - sizeof(int));
  if(valn == -1)
    return -1;

  /* Encrypt reply */
  *(int *)dec_rep = valn;
  while(bytes_enc < valn + sizeof(int)) {
    ret = mbedtls_aesni_crypt_ecb(&encctx, MBEDTLS_AES_ENCRYPT,
                                &dec_rep[bytes_enc], &enc_rep[bytes_enc]);
    if(ret)
      return -1;
    bytes_enc += 16;
  }

  return 0;
}

int wrapper_free() {
  mbedtls_aes_free(&encctx);
  mbedtls_aes_free(&decctx);

  return 0;
}

#include <assert.h>
#include <sel4/sel4.h>
#include <sel4platsupport/platsupport.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "cache_wrapper.h"
#include "config.h"
#include "bench.h"

#define ALPHABETSIZE (26 + 26 + 10 + 2)

static inline char whichchar(unsigned i) {
    if (i < 26)
        return 'A' + i;
    else if (i < (26 + 26))
        return 'a' + (i - 26);
    else if (i < (26 + 26 + 10))
        return '0' + (i - (26 + 26));
    else if (i < (ALPHABETSIZE))
        return ':' + (i - (26 + 26 + 10));
    else
        return '\0';
}

typedef struct __attribute__((packed)) {
    int len;
    char keybuf[60];
} cachekey_t;

static void make_key(int intkey, cachekey_t *key) {
    key->len = NCHARS;

    int j;
    for (j = 0; j < NCHARS; j++) {
        key->keybuf[j] = whichchar(intkey % ALPHABETSIZE);
        intkey /= ALPHABETSIZE;
    }
    assert(intkey == 0);
    key->keybuf[j] = '\0';
}

static int encrypt_key(mbedtls_aes_context *ctx, cachekey_t *key, char *buf) {
    int enclen = 0, remaining;
    int ret;

    remaining = key->len + sizeof(int);
    while (remaining > 0) {
        ret = mbedtls_aes_crypt_ecb(ctx, MBEDTLS_AES_ENCRYPT,
                                    ((char *)key) + enclen, buf + enclen);
        if (ret)
            return -1;

        remaining -= 16;
        enclen += 16;
    }
    return enclen;
}

int main(int argc, char *argv[]) {
    /* Setup serial output via seL4_Debug_PutChar */
    if (platsupport_serial_setup_bootinfo_failsafe()) {
        /* Error occured during setup => terminate */
        return 1;
    }

    perf_counters_t counters[NPASSES] = { 0 };

    cachekey_t key;
    char enckeybuf[256];
    char encvalbuf[256];
    mbedtls_aes_context *ctx;

    ctx = wrapper_init();

    /* Set value into cache */
    for (int i = 0; i < NITEMS; i++) {
        make_key(i, &key);

        int r = cache_set(key.keybuf, NCHARS, "hell1hell2hell3hell4hell5", 25);
        assert(r >= 0);
    }

    /* Retrieve same values into cache */
    for (int j = 0; j < NPASSES; j++) {
        for (int i = 0; i < NITEMS; i++) {
            memset(&key, 0, sizeof(key));
            make_key(i, &key);
            int enclen = encrypt_key(ctx, &key, enckeybuf);
            size_t before_i, after_i, before_c, after_c;
            RDCTR(before_i, instret);
            RDCTR(before_c, cycle);
            cache_get_wrapper(enckeybuf, enclen, encvalbuf, 256);
            RDCTR(after_c, cycle);
            RDCTR(after_i, instret);
            counters[j].instret += after_i - before_i;
            counters[j].cycle += after_c - before_c;
        }
    }

    wrapper_free();

    const char *cumulative_cycles = "Cumulative cycles:";
    const char *cumulative_instret = "Cumulative retired insns:";
    const size_t cumulative_str_len = MAX(strlen(cumulative_cycles), strlen(cumulative_instret));
    for (size_t i = 0; i < NPASSES; i++) {
        printf("Pass %3zd:\n", i);
        printf("    %-*s %10zd\n", cumulative_str_len, cumulative_cycles, counters[i].cycle);
        printf("    %-*s %10zd\n", cumulative_str_len, cumulative_instret, counters[i].instret);
    }

    seL4_TCB_Suspend(seL4_CapInitThreadTCB);

    return 0;
}

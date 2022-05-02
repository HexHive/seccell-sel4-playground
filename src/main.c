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

#define LD_INST_RET   9
#define ST_INST_RET   10
#define AT_INST_RET   11
#define L2_TLB_MISS   13

    /* HPM event: L2 TLB miss */
    uint64_t event3 = (1 << L2_TLB_MISS) | 0x2;  
    /* HPM event: load, store, atomic retired */
    uint64_t event4 = (1 << LD_INST_RET) | (1 << ST_INST_RET) | (1 << AT_INST_RET) | 0x0;   
    asm volatile ("csrw mhpmevent3, %[event3];"
                 "csrw mhpmevent4, %[event4];"
                    :: [event3] "r" (event3),
                        [event4] "r" (event4)
                    :);

    /* Retrieve same values into cache */
    for (int j = 0; j < NPASSES; j++) {
        for (int i = 0; i < NITEMS; i++) {
            memset(&key, 0, sizeof(key));
            make_key(i, &key);
            int enclen = encrypt_key(ctx, &key, enckeybuf);
            uint64_t before_i, after_i, before_c, after_c;
            uint64_t before_tlb, after_tlb, before_mem, after_mem;

            RD_CTR(before_tlb, mhpmcounter3);
            RD_CTR(before_mem, mhpmcounter4);
            RDCTR(before_i, instret);
            RDCTR(before_c, cycle);
            cache_get_wrapper(enckeybuf, enclen, encvalbuf, 256);
            RDCTR(after_c, cycle);
            RDCTR(after_i, instret);
            RD_CTR(after_mem, mhpmcounter4);
            RD_CTR(after_tlb, mhpmcounter3);
            counters[j].instret += after_i - before_i;
            counters[j].cycle += after_c - before_c;
            counters[j].tlb += after_tlb - before_tlb;
            counters[j].mem += after_mem - before_mem;
        }
    }

    wrapper_free();


    printf("nitems,passidx,mcycle,minstret,memaccs,tlbmisses\n");
    for (size_t i = 0; i < NPASSES; i++) {
        printf("%10zd,%3zd,%10zd,%10zd,%10zd,%10zd\n", NITEMS, i, 
                        counters[i].cycle, counters[i].instret, 
                        counters[i].mem, counters[i].tlb);
    }

    seL4_TCB_Suspend(seL4_CapInitThreadTCB);

    return 0;
}

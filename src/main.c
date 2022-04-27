#include <sel4/sel4.h>
#include <sel4platsupport/platsupport.h>
#include <stdio.h>
#include <string.h>

#include "cache.h"

#define NITEMS 16 * 1024
#define NCHARS 5
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

int main(int argc, char *argv[]) {
    /* Setup serial output via seL4_Debug_PutChar */
    if (platsupport_serial_setup_bootinfo_failsafe()) {
        /* Error occured during setup => terminate */
        return 1;
    }

    cache_init();
    char key[16], value[64];

    for (int i = 0; i < NITEMS; i++) {
        int tmp = i;
        int j;
        for (j = 0; j < NCHARS; j++) {
            key[j] = whichchar(tmp % ALPHABETSIZE);
            tmp /= ALPHABETSIZE;
        }
        key[j] = '\0';

        cache_set(key, j, "hell1hell2hell3hell4hell5", 25);
    }

    dump_cache();

    seL4_TCB_Suspend(seL4_CapInitThreadTCB);

    return 0;
}
